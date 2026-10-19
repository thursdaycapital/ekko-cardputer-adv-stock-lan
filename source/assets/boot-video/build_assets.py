#!/usr/bin/env python3
"""Encode rendered PNG frames as one bounded, baseline-JPEG firmware asset.

The source renderer owns picture content and timing. This exporter keeps every
input frame, validates the supplied duration/fps, and selects the highest common
JPEG quality that fits the complete embedded budget (payload plus uint32 table).
Pillow is needed only for regeneration; firmware has no Pillow dependency.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import re
import struct
import tempfile
from fractions import Fraction
from pathlib import Path

from PIL import Image, __version__ as pillow_version

DEFAULT_WIDTH, DEFAULT_HEIGHT = 240, 135
DEFAULT_BUDGET_BYTES = 300000
MAX_FIRMWARE_FRAMES = 180


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def natural_key(path: Path):
    return [(1, int(part)) if part.isdigit() else (0, part.casefold())
            for part in re.split(r"(\d+)", path.name)]


def png_sources(source_dir: Path) -> list[Path]:
    if not source_dir.is_dir():
        raise ValueError("Source frame directory does not exist")
    sources = sorted((p for p in source_dir.iterdir()
                      if p.is_file() and p.suffix.casefold() == ".png"), key=natural_key)
    if not sources:
        raise ValueError("Source frame directory has no PNG frames")
    if len(sources) > MAX_FIRMWARE_FRAMES:
        raise ValueError(f"Firmware supports at most {MAX_FIRMWARE_FRAMES} frames")
    indices = [re.search(r"(\d+)(?=\.png$)", p.name, re.IGNORECASE) for p in sources]
    if any(indices):
        if not all(indices):
            raise ValueError("Numbered and unnumbered source frames cannot be mixed")
        values = [int(match.group(1)) for match in indices if match]
        if values != list(range(values[0], values[0] + len(values))):
            raise ValueError("Source frame numbers must be unique and consecutive")
    return sources


def baseline_jpeg_dimensions(data: bytes, width: int, height: int) -> None:
    """Require SOF0/8-bit baseline geometry and independently full-decode it."""
    if len(data) < 4 or not data.startswith(b"\xff\xd8") or not data.endswith(b"\xff\xd9"):
        raise ValueError("JPEG frame lacks SOI/EOI")
    cursor, baseline = 2, False
    sof_markers = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                   0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}
    while cursor < len(data):
        if data[cursor] != 0xFF:
            raise ValueError("Malformed JPEG header marker")
        while cursor < len(data) and data[cursor] == 0xFF:
            cursor += 1
        if cursor >= len(data):
            raise ValueError("Truncated JPEG header")
        marker = data[cursor]
        cursor += 1
        if marker == 0xDA:  # Entropy-coded scan follows; Pillow validates it below.
            break
        if marker in {0x01, 0xD8, 0xD9} or 0xD0 <= marker <= 0xD7:
            continue
        if cursor + 2 > len(data):
            raise ValueError("Truncated JPEG segment length")
        size = int.from_bytes(data[cursor:cursor + 2], "big")
        if size < 2 or cursor + size > len(data):
            raise ValueError("JPEG segment exceeds frame")
        if marker in sof_markers:
            if marker != 0xC0 or baseline or size < 8:
                raise ValueError("Frame must contain exactly one baseline SOF0")
            precision = data[cursor + 2]
            actual_height = int.from_bytes(data[cursor + 3:cursor + 5], "big")
            actual_width = int.from_bytes(data[cursor + 5:cursor + 7], "big")
            if precision != 8 or (actual_width, actual_height) != (width, height):
                raise ValueError("JPEG baseline geometry/precision does not match target")
            baseline = True
        cursor += size
    if not baseline:
        raise ValueError("JPEG frame has no baseline SOF0")
    with Image.open(io.BytesIO(data)) as decoded:
        decoded.load()
        if decoded.format != "JPEG" or decoded.info.get("progressive"):
            raise ValueError("Frame is not baseline JPEG")
        if decoded.size != (width, height):
            raise ValueError("Decoded JPEG dimensions do not match target")


def atomic_write(path: Path, data: bytes) -> None:
    """Do not leave a truncated generated file if a write fails."""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix="." + path.name + ".", dir=path.parent,
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        temporary.chmod(0o644)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def build(source_dir: Path, output_dir: Path, *, duration_ms: int, fps: Fraction,
          width: int = DEFAULT_WIDTH, height: int = DEFAULT_HEIGHT,
          budget_bytes: int = DEFAULT_BUDGET_BYTES, quality_max: int = 88,
          quality_min: int = 1, source_description: str = "rendered PNG sequence") -> dict:
    if not 1 <= duration_ms <= 10000:
        raise ValueError("Duration must be between 1 and 10000 milliseconds")
    if fps <= 0:
        raise ValueError("FPS must be positive")
    if not 1 <= width <= 65535 or not 1 <= height <= 65535:
        raise ValueError("Target dimensions must fit uint16_t")
    if not 1 <= quality_min <= quality_max <= 95:
        raise ValueError("JPEG qualities must satisfy 1 <= min <= max <= 95")
    if not 1 <= budget_bytes <= 0xFFFFFFFF:
        raise ValueError("Budget must be positive and fit uint32_t")
    sources = png_sources(source_dir)
    frame_count = len(sources)
    expected_count = fps * Fraction(duration_ms, 1000)
    if expected_count.denominator != 1 or expected_count.numerator != frame_count:
        raise ValueError(f"Found {frame_count} frames but duration/fps requires {expected_count}")
    table_bytes = (frame_count + 1) * 4
    if table_bytes >= budget_bytes:
        raise ValueError("Offset table alone exhausts the embedded budget")
    output_dir = output_dir.resolve()
    frame_dir = output_dir / "frames"
    if frame_dir.exists() and any(not p.is_file() or not re.fullmatch(r"frame-\d+\.jpg", p.name)
                                  for p in frame_dir.iterdir()):
        raise ValueError("Output frame folder contains unrelated files")
    prepared, source_records = [], []
    for path in sources:
        source_data = path.read_bytes()
        with Image.open(io.BytesIO(source_data)) as image:
            image.load()
            if image.format != "PNG" or getattr(image, "n_frames", 1) != 1:
                raise ValueError(f"Source {path.name} must be a single PNG frame")
            if image.width * height != image.height * width:
                raise ValueError(f"Source frame {path.name} aspect ratio differs from target")
            source_size = list(image.size)
            if "A" in image.getbands() or "transparency" in image.info:
                rgba = image.convert("RGBA")
                if rgba.getextrema()[3] != (255, 255):
                    raise ValueError(f"Source {path.name} has transparency; render an opaque frame first")
            rgb = image.convert("RGB")
            if rgb.size != (width, height):
                rgb = rgb.resize((width, height), Image.Resampling.LANCZOS)
            prepared.append(rgb)
        source_records.append({"file": path.name, "bytes": len(source_data),
                               "size": source_size, "sha256": sha256(source_data)})
    chosen = None
    for quality in range(quality_max, quality_min - 1, -1):
        encoded = []
        for image in prepared:
            stream = io.BytesIO()
            image.save(stream, format="JPEG", quality=quality, subsampling=0,
                       optimize=True, progressive=False)
            encoded.append(stream.getvalue())
        if sum(map(len, encoded)) + table_bytes <= budget_bytes:
            chosen = quality, encoded
            break
    if chosen is None:
        raise ValueError(f"Complete asset exceeds {budget_bytes} bytes even at quality {quality_min}")
    quality, frames = chosen
    offsets = [0]
    for frame in frames:
        baseline_jpeg_dimensions(frame, width, height)
        offsets.append(offsets[-1] + len(frame))
    if not all(a < b for a, b in zip(offsets, offsets[1:])):
        raise ValueError("Frame offsets must increase strictly")
    payload = b"".join(frames)
    offset_data = struct.pack("<" + "I" * len(offsets), *offsets)
    embedded_bytes = len(payload) + len(offset_data)
    if embedded_bytes > budget_bytes or offsets[-1] != len(payload):
        raise ValueError("Embedded data violates the budget or offset contract")
    frame_records = [{"file": f"frames/frame-{i:03d}.jpg", "offset": offsets[i],
                      "bytes": len(frame), "sha256": sha256(frame),
                      "presentation_time_ms": (i * duration_ms) // frame_count}
                     for i, frame in enumerate(frames)]
    header = f"""// Generated by build_assets.py. Do not edit encoded data by hand.
#pragma once
#include <stdint.h>

namespace native_boot_video {{
constexpr uint16_t kWidth = {width};
constexpr uint16_t kHeight = {height};
constexpr uint16_t kFrameCount = {frame_count};
constexpr uint32_t kDurationMs = {duration_ms};
constexpr uint32_t kDataBytes = {len(payload)};
constexpr uint32_t kOffsetBytes = {table_bytes};
constexpr uint32_t kEmbeddedBytes = {embedded_bytes};
extern const uint8_t kJpegData[kDataBytes];
extern const uint32_t kFrameOffsets[kFrameCount + 1];
}}  // namespace native_boot_video
"""
    data_lines = ["// Generated by build_assets.py; include exactly once from a .cpp file.",
                  '#include "BootVideoAssets.h"', '#include <pgmspace.h>', "",
                  "namespace native_boot_video {",
                  "const uint8_t kJpegData[kDataBytes] PROGMEM = {"]
    for start in range(0, len(payload), 16):
        data_lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in payload[start:start + 16]) + ",")
    data_lines.extend(["};", "const uint32_t kFrameOffsets[kFrameCount + 1] PROGMEM = {"])
    for start in range(0, len(offsets), 10):
        data_lines.append("  " + ", ".join(str(value) for value in offsets[start:start + 10]) + ",")
    data_lines.extend(["};", "}  // namespace native_boot_video", ""])
    manifest = {
        "format": "baseline-jpeg-sequence", "width": width, "height": height,
        "fps": int(fps) if fps.denominator == 1 else float(fps),
        "fps_ratio": f"{fps.numerator}/{fps.denominator}",
        "duration_ms": duration_ms, "frame_count": frame_count,
        "jpeg_quality": quality, "jpeg_subsampling": "4:4:4",
        "payload_bytes": len(payload), "offset_table_bytes": len(offset_data),
        "embedded_bytes": embedded_bytes, "embedded_budget_bytes": budget_bytes,
        "payload_sha256": sha256(payload), "offset_table_le_sha256": sha256(offset_data),
        "payload_then_offsets_le_sha256": sha256(payload + offset_data),
        "frame_offsets": offsets, "encoder": "Pillow " + pillow_version,
        "source": source_description, "source_frames": source_records,
        "frames": frame_records, "audio": "none",
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    frame_dir.mkdir(exist_ok=True)
    expected_names = {Path(record["file"]).name for record in frame_records}
    for record, frame in zip(frame_records, frames):
        atomic_write(output_dir / record["file"], frame)
    for old in frame_dir.iterdir():
        if old.name not in expected_names:  # Only previously generated frame-N.jpg, checked above.
            old.unlink()
    atomic_write(output_dir / "BootVideoAssets.h", header.encode())
    atomic_write(output_dir / "BootVideoAssetsData.inc", "\n".join(data_lines).encode())
    atomic_write(output_dir / "asset-manifest.json",
                 (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode())
    summary = {key: manifest[key] for key in ["width", "height", "frame_count", "duration_ms",
               "fps", "jpeg_quality", "payload_bytes", "offset_table_bytes",
               "embedded_bytes", "embedded_budget_bytes", "payload_sha256"]}
    print(json.dumps(summary))
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_frames", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--duration-ms", type=int, required=True)
    parser.add_argument("--fps", type=Fraction, required=True,
                        help="Integer, decimal, or rational frame rate (e.g. 15 or 30000/1001)")
    parser.add_argument("--width", type=int, default=DEFAULT_WIDTH)
    parser.add_argument("--height", type=int, default=DEFAULT_HEIGHT)
    parser.add_argument("--budget-bytes", type=int, default=DEFAULT_BUDGET_BYTES)
    parser.add_argument("--quality-max", type=int, default=88)
    parser.add_argument("--quality-min", type=int, default=1)
    parser.add_argument("--source-description", default="rendered PNG sequence")
    args = parser.parse_args()
    try:
        build(args.source_frames, args.output, duration_ms=args.duration_ms, fps=args.fps,
              width=args.width, height=args.height, budget_bytes=args.budget_bytes,
              quality_max=args.quality_max, quality_min=args.quality_min,
              source_description=args.source_description)
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
