"""Extract Unicode coverage facts from the locked efontCN_12 U8g2 table.

Only code-point membership is saved, not glyph pixels. The lookup interpretation
matches M5GFX U8g2font::getGlyph (header offset 23, big-endian Unicode LUT).
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
FONT_SHA256 = "ceb7bae98279b5b196a4742b0814970d095ddcaabea8a9ac3cebe1e1f7add873"


def parse_font(path):
    source = path.read_bytes()
    if hashlib.sha256(source).hexdigest() != FONT_SHA256:
        raise ValueError("Font differs from pinned M5GFX 0.2.33 efontCN_12 source")
    match = re.search(
        r'PROGMEM const uint8_t lgfx_efont_cn_12\[(\d+)\]\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);',
        source.decode())
    if not match:
        raise ValueError("Cannot locate efontCN_12 array")
    data = b"".join(ast.literal_eval("b" + value)
                    for value in re.findall(r'"(?:[^"\\]|\\.)*"', match[2]))
    declared = int(match[1])
    if len(data) > declared:
        raise ValueError("Font array exceeds its declaration")
    return data + bytes(declared - len(data))


def codepoints(data):
    result = set()
    cursor = 23
    while data[cursor + 1]:
        result.add(data[cursor])
        cursor += data[cursor + 1]
    cursor = lut = 23 + int.from_bytes(data[21:23], "big")
    while True:
        cursor += int.from_bytes(data[lut:lut + 2], "big")
        end = int.from_bytes(data[lut + 2:lut + 4], "big")
        lut += 4
        glyph = cursor
        while int.from_bytes(data[glyph:glyph + 2], "big"):
            result.add(int.from_bytes(data[glyph:glyph + 2], "big"))
            size = data[glyph + 2]
            if size < 3:
                raise ValueError("Malformed glyph record")
            glyph += size
        if end == 0xFFFF:
            break
        if lut >= len(data):
            raise ValueError("Missing Unicode LUT terminator")
    return sorted(result)


def generate(path):
    font = parse_font(path)
    coverage = codepoints(font)
    output = "".join(f"{value:04X}\n" for value in coverage).encode()
    manifest = {
        "schema_version": 1,
        "font": "M5GFX 0.2.33 efontCN_12",
        "source_url": "https://github.com/m5stack/M5GFX/blob/0.2.33/src/lgfx/Fonts/efont/lgfx_efont_cn.c",
        "source_sha256": FONT_SHA256,
        "coverage_sha256": hashlib.sha256(output).hexdigest(),
        "glyphs": len(coverage),
        "font_array_bytes": len(font),
        "notice": "Unicode membership facts only; no glyph outlines or pixels copied. Original font copyright 2000-2003 /efont/ The Electronic Font Open Laboratory.",
    }
    return {"font_codepoints.txt": output,
            "font_coverage.json": (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    for name, data in generate(options.font).items():
        destination = HERE / name
        if options.check:
            if destination.read_bytes() != data:
                raise SystemExit(f"Font coverage is stale: {name}")
        else:
            destination.write_bytes(data)
    print("Pinned efontCN_12 coverage verified" if options.check else "Pinned efontCN_12 coverage saved")


if __name__ == "__main__":
    main()
