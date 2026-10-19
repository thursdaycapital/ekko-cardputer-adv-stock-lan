"""Generate a bounded Flash-only lexicon from pinned Rime/AOSP input data.

No network access or third-party Python packages are needed. Source files are
kept verbatim; generated data is modified, compacted and frequency-pruned.
"""
import argparse
from collections import defaultdict
import hashlib
import itertools
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
BUDGET_BYTES = 760 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_source():
    sources = json.loads((HERE / "sources.json").read_text())
    for name, expected in sources["files"].items():
        if digest((HERE / "upstream" / name).read_bytes()) != expected:
            raise ValueError(f"Pinned source hash mismatch: {name}")
    rows = {}
    for line in (HERE / "upstream/pinyin_simp.dict.yaml").read_text().splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            continue
        text, code, weight_text = fields
        if not re.fullmatch(r"[a-z]+(?: [a-z]+)*", code):
            raise ValueError(f"Invalid upstream pinyin: {code!r}")
        syllables = code.split()
        if len(syllables) != len(text) or len(text.encode()) >= 49:
            raise ValueError("Unsupported upstream text/syllable length")
        if len(syllables) > 4 or len("".join(syllables)) > 31:
            raise ValueError("Packed syllable boundaries would overflow")
        key = text, tuple(syllables)
        rows[key] = max(rows.get(key, 0), int(weight_text))
    entries = [(text, code, weight) for (text, code), weight in rows.items()]
    return sources, entries


def boundaries(syllables):
    packed, position = 0, 0
    for index, syllable in enumerate(syllables[:-1]):
        position += len(syllable)
        packed |= position << (index * 5)
    return packed


def spellings(syllables):
    # Expand only actual complete syllables. Replacing concatenated "nve"
    # would wrongly change 女儿 nv er into nuer and break its full-word match.
    pairs = {"nue": ("nue", "nve"), "nve": ("nue", "nve"),
             "lue": ("lue", "lve"), "lve": ("lue", "lve")}
    return itertools.product(*(pairs.get(syllable, (syllable,)) for syllable in syllables))


def pack(entries):
    groups = defaultdict(dict)
    for text, syllables, weight in entries:
        for spelling in spellings(syllables):
            rows = groups["".join(spelling)]
            key = text, spelling
            rows[key] = max(rows.get(key, 0), weight)
    keys, candidates, index = bytearray(), bytearray(), bytearray()
    for key, rows in sorted(groups.items()):
        index.extend(len(keys).to_bytes(3, "little"))
        index.extend(len(candidates).to_bytes(3, "little"))
        index.extend(len(rows).to_bytes(2, "little"))
        keys.extend(key.encode() + b"\0")
        values = [(text, syllables, weight) for (text, syllables), weight in rows.items()]
        for text, syllables, weight in sorted(values, key=lambda row: (-row[2], row[0], row[1])):
            data = text.encode()
            candidates.append(len(data))
            candidates.extend(boundaries(syllables).to_bytes(2, "little"))
            candidates.extend(data)
    return keys, candidates, index, len(groups), sum(len(rows) for rows in groups.values())


def cpp_bytes(name, data):
    # A single trailing compiler NUL is counted in the resource budget.
    lines = [f"const char {name}[] NATIVE_PINYIN_FLASH ="]
    for offset in range(0, len(data), 48):
        lines.append('    "' + "".join(f"\\x{value:02x}" for value in data[offset:offset + 48]) + '"')
    if not data:
        lines.append('    ""')
    lines[-1] += ";"
    return "\n".join(lines)


def generate():
    sources, entries = read_source()
    original_entries = entries
    font_manifest = json.loads((HERE / "font_coverage.json").read_text())
    coverage_data = (HERE / "font_codepoints.txt").read_bytes()
    if digest(coverage_data) != font_manifest["coverage_sha256"]:
        raise ValueError("Pinned font coverage hash mismatch")
    covered = {int(code, 16) for code in coverage_data.decode().splitlines()}
    entries = [row for row in entries if all(ord(character) in covered for character in row[0])]
    singles = [row for row in entries if len(row[0]) == 1]
    words = sorted((row for row in entries if len(row[0]) > 1),
                   key=lambda row: (-row[2], "".join(row[1]), row[0], row[1]))
    syllables = sorted({syllable for _, code, _ in entries for syllable in code})
    # Preserve all single-character readings. Add the highest-frequency words
    # in a strict prefix of their deterministic ranking until the budget fills.
    selected = list(singles)
    present = defaultdict(set)
    for text, code, _ in singles:
        for spelling in spellings(code):
            present["".join(spelling)].add((text, spelling))
    base_keys, base_candidates, base_index, _, _ = pack(singles)
    size = len(base_keys) + len(base_candidates) + len(base_index)
    size += 3  # three compiler string NULs
    for row in words:
        text, code, weight = row
        expanded = [("".join(spelling), (text, spelling)) for spelling in spellings(code)]
        new_keys = {key for key, _ in expanded if key not in present}
        new_entries = [(key, entry) for key, entry in expanded if entry not in present.get(key, ())]
        additional = len(new_entries) * (3 + len(text.encode()))
        additional += sum(len(key) + 1 + 8 for key in new_keys)
        if size + additional > BUDGET_BYTES:
            break
        selected.append(row)
        for key, entry in expanded:
            present[key].add(entry)
        size += additional
    keys, candidates, index, group_count, entry_count = pack(selected)
    arrays = [("kKeys", keys), ("kCandidates", candidates), ("kGroups", index)]
    total_bytes = sum(len(data) + 1 for _, data in arrays)
    assert total_bytes == size and total_bytes <= BUDGET_BYTES
    header = """// Generated by assets/pinyin/generate.py. Do not edit.
// Modified, compacted subset of Rime pinyin-simp / AOSP PinyinIME data.
// Original upstream data: Apache-2.0. See upstream/LICENSE and NOTICE.
#pragma once
#include <cstddef>
#include <cstdint>
namespace native_pinyin_data {
"""
    header += f"static constexpr size_t kGroupCount = {group_count};\n"
    header += f"static constexpr size_t kEntryCount = {entry_count};\n"
    header += f"static constexpr size_t kSyllableCount = {len(syllables)};\n"
    header += f"static constexpr size_t kDataBytes = {total_bytes};\n"
    header += "\n".join(f"extern const char {name}[{len(data) + 1}];" for name, data in arrays)
    header += "\n}  // namespace native_pinyin_data\n"
    output = header.encode()
    implementation = """// Generated by assets/pinyin/generate.py. Do not edit.
// Modified Rime/AOSP dictionary data; see assets/pinyin/NOTICE and upstream/LICENSE.
#include "../assets/pinyin/PinyinDictionaryData.h"
#if defined(ARDUINO)
#include <pgmspace.h>
#define NATIVE_PINYIN_FLASH PROGMEM
#else
#define NATIVE_PINYIN_FLASH
#endif
namespace native_pinyin_data {
"""
    implementation += "\n\n".join(cpp_bytes(name, data) for name, data in arrays)
    implementation += "\n}  // namespace native_pinyin_data\n#undef NATIVE_PINYIN_FLASH\n"
    manifest = {
        "schema_version": 1,
        "source_commit": sources["commit"],
        "source_license": sources["license"],
        "generation": {
            "budget_bytes": BUDGET_BYTES,
            "single_character_readings": len(singles),
            "single_characters": len({text for text, _, _ in singles}),
            "words": len(selected) - len(singles),
            "entries": entry_count,
            "original_entries": len(selected),
            "additional_syllable_aliases": entry_count - len(selected),
            "groups": group_count,
            "syllables": len(syllables),
            "max_word_characters": max(len(text) for text, _, _ in selected),
            "word_weight_minimum": min(weight for text, _, weight in selected if len(text) > 1),
            "font": font_manifest["font"],
            "font_coverage_sha256": font_manifest["coverage_sha256"],
            "excluded_unsupported_character_readings": sum(len(row[0]) == 1 for row in original_entries) - len(singles),
            "excluded_unsupported_words": sum(len(row[0]) > 1 for row in original_entries) - len(words),
            "data_bytes": total_bytes,
            "arrays": {name: {"data_bytes": len(data) + 1, "sha256": digest(data)} for name, data in arrays},
        },
        "generated_header_sha256": digest(output),
        "generated_implementation_sha256": digest(implementation.encode()),
        "selection_policy": "all efontCN_12-renderable single-character readings; descending frequency renderable words until 760 KiB; ties by pinyin/text",
        "modifications": "UTF-8 compact records, concatenated key groups, retained syllable boundaries, per-syllable ue/ve aliases, efontCN_12 glyph filtering, frequency-pruned words; no manual phrases",
    }
    return {"PinyinDictionaryData.h": output,
            "../../src/PinyinDictionaryData.cpp": implementation.encode(),
            "manifest.json": (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify checked-in output without modifying it")
    options = parser.parse_args()
    for name, data in generate().items():
        path = HERE / name
        if options.check:
            if not path.is_file() or path.read_bytes() != data:
                raise SystemExit(f"Generated pinyin data is stale: {name}")
        else:
            path.write_bytes(data)
    print("Pinyin source hashes / deterministic output verified" if options.check else "Pinyin data generated")


if __name__ == "__main__":
    main()
