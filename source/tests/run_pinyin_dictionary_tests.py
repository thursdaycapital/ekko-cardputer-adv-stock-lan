"""Offline dictionary/source/glyph contract tests, C++11 ASan/UBSan, no device."""
from collections import defaultdict
import importlib.util
import itertools
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

FIRMWARE = Path(__file__).resolve().parents[1]
ASSETS = FIRMWARE / "assets/pinyin"
COMPILER = shutil.which("clang++") or shutil.which("g++")
if not COMPILER:
    raise SystemExit("A C++11 host compiler is required")


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


subprocess.run(["python3", str(ASSETS / "generate.py"), "--check"], check=True)
generator = module("pinyin_generate", ASSETS / "generate.py")
manifest = json.loads((ASSETS / "manifest.json").read_text())
coverage = {int(value, 16) for value in (ASSETS / "font_codepoints.txt").read_text().splitlines()}
_, original = generator.read_source()
# Independent reconstruction from original, unmodified source and policy. No
# generated candidate bytes or runtime lookup participate in this oracle.
renderable = [row for row in original if all(ord(character) in coverage for character in row[0])]
singles = [row for row in renderable if len(row[0]) == 1]
words = sorted((row for row in renderable if len(row[0]) > 1),
               key=lambda row: (-row[2], "".join(row[1]), row[0], row[1]))
selected = singles + words[:manifest["generation"]["words"]]
groups = defaultdict(dict)
for text, syllables, weight in selected:
    variants = [("nue", "nve") if code in ("nue", "nve") else
                ("lue", "lve") if code in ("lue", "lve") else (code,) for code in syllables]
    for spelling in itertools.product(*variants):
        rows = groups["".join(spelling)]
        rows[text, spelling] = max(rows.get((text, spelling), 0), weight)

with tempfile.TemporaryDirectory(prefix="cardputer-pinyin-tests-") as directory:
    temporary = Path(directory)
    oracle = temporary / "oracle.tsv"
    oracle.write_text("".join(
        key + "\t" + "\t".join(value for text, syllables, weight in sorted(
            [(text, syllables, weight) for (text, syllables), weight in rows.items()],
            key=lambda row: (-row[2], row[0], row[1])) for value in (text, str(len(syllables)))) + "\n"
        for key, rows in sorted(groups.items())))
    second = temporary / "second.cpp"
    second.write_text('''#include "PinyinDictionary.h"
bool pinyinSecondTranslationUnit() {
  NativePinyinPage result;
  return NativePinyinDictionary::lookup("nihao", 0, result) && result.count &&
    std::strcmp(result.candidates[0].text, "你好") == 0;
}
''')
    binary = temporary / "pinyin-dictionary"
    subprocess.run([
        COMPILER, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(FIRMWARE / "include"), str(FIRMWARE / "tests/test_pinyin_dictionary.cpp"),
        str(second), str(FIRMWARE / "src/PinyinDictionaryData.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary), str(oracle)], check=True)

    # When the pinned SDK is installed, compare the saved coverage against the
    # actual getGlyph implementation for all 65,536 BMP code points. This is a
    # second implementation, not only a second parse of the saved allow-list.
    gfx = FIRMWARE / ".pio/libdeps/cardputer-adv/M5GFX/src/lgfx"
    font_path = gfx / "Fonts/efont/lgfx_efont_cn.c"
    if font_path.is_file():
        extractor = module("pinyin_font", ASSETS / "font_coverage.py")
        subprocess.run(["python3", str(ASSETS / "font_coverage.py"), "--font", str(font_path), "--check"], check=True)
        font = extractor.parse_font(font_path)
        font_file = temporary / "font.bin"
        font_file.write_bytes(font)
        source = (gfx / "v1/lgfx_fonts.cpp").read_text()
        start = source.index("  const uint8_t* U8g2font::getGlyph(uint16_t encoding) const")
        end = source.index("\n  void U8g2font::getDefaultMetric", start)
        body = source[start:end].replace("U8g2font::getGlyph", "PinnedFont::getGlyph")
        cpp = temporary / "font-coverage.cpp"
        cpp.write_text('''#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <vector>
#define pgm_read_byte(pointer) (*(pointer))
struct PinnedFont {
  const uint8_t* _font;
  uint16_t start_pos_upper_A() const { return uint16_t(_font[17]) << 8 | _font[18]; }
  uint16_t start_pos_lower_a() const { return uint16_t(_font[19]) << 8 | _font[20]; }
  uint16_t start_pos_unicode() const { return uint16_t(_font[21]) << 8 | _font[22]; }
  const uint8_t* getGlyph(uint16_t encoding) const;
};
''' + body + '''
int main(int argc, char** argv) {
  assert(argc == 3);
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  std::ifstream setFile(argv[2]);
  std::set<uint32_t> expected;
  uint32_t code;
  while (setFile >> std::hex >> code) expected.insert(code);
  PinnedFont font{bytes.data()};
  for (uint32_t i = 0; i < 65536; ++i)
    assert((font.getGlyph(static_cast<uint16_t>(i)) != nullptr) == (expected.count(i) != 0));
  std::cout << "Actual locked M5GFX getGlyph oracle: all 65536 BMP code points passed\\n";
}
''')
        font_binary = temporary / "font-coverage"
        subprocess.run([COMPILER, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(cpp), "-o", str(font_binary)], check=True)
        subprocess.run([str(font_binary), str(font_file), str(ASSETS / "font_codepoints.txt")], check=True)
    else:
        print("Actual SDK font oracle not run: install pinned PlatformIO dependencies; saved coverage hash remains verified")
