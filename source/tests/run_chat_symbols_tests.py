"""Pure C++11 display policy tests against every official Unicode code point."""
from pathlib import Path
import json
import hashlib
import re
import shutil
import subprocess
import tempfile

FIRMWARE = Path(__file__).resolve().parents[1]
ASSETS = FIRMWARE / "assets/chat-symbols"
COMPILER = shutil.which("clang++") or shutil.which("g++")
if not COMPILER:
    raise SystemExit("C++11 compiler is required")
subprocess.run(["python3", str(ASSETS / "generate.py"), "--check"], check=True)
coverage_path = FIRMWARE / "assets/pinyin/font_codepoints.txt"
coverage_metadata = json.loads((FIRMWARE / "assets/pinyin/font_coverage.json").read_text())
coverage_bytes = coverage_path.read_bytes()
assert hashlib.sha256(coverage_bytes).hexdigest() == coverage_metadata["coverage_sha256"]
supported = {int(cp, 16) for cp in coverage_bytes.decode().splitlines()}
labels = set(re.findall(r'"(\[[^"\n]+\])"', (FIRMWARE / "include/ChatSymbols.h").read_text()))
missing = {character for label in labels for character in label if ord(character) not in supported}
assert labels and not missing, f"Unrenderable label characters: {missing}"
print(f"All {len(labels)} Chinese fallback labels covered by pinned efontCN_12")

# Build the oracle directly from original Unicode property lines, independently
# of generated merged ranges. Flags cover both property and explicit UI policy.
flags = bytearray(0x110000)
for line in (ASSETS / "upstream/emoji-data.txt").read_text().splitlines():
    record = line.partition("#")[0].strip()
    if not record:
        continue
    interval, property_name = map(str.strip, record.split(";"))
    begin, _, end = interval.partition("..")
    first, last = int(begin, 16), int(end or begin, 16)
    for cp in range(first, last + 1):
        if property_name == "Emoji_Presentation":
            flags[cp] |= 1
        elif property_name == "Emoji_Modifier":
            flags[cp] |= 2
        elif property_name == "Emoji_Component" and 0xE0020 <= cp <= 0xE007F:
            flags[cp] |= 4
        elif property_name == "Emoji" and cp >= 128:
            flags[cp] |= 32
for cp in range(len(flags)):
    if flags[cp] & 2:
        flags[cp] &= ~32
    if flags[cp] & (2 | 4) or cp in (0xFE0E, 0xFE0F, 0x20E3):
        flags[cp] |= 8
    if cp in (0x2795, 0x2796, 0x2797, 0x1F7F0):
        flags[cp] |= 16

with tempfile.TemporaryDirectory(prefix="cardputer-chat-symbols-") as directory:
    temporary = Path(directory)
    oracle = temporary / "unicode-properties.bin"
    oracle.write_bytes(flags)
    second = temporary / "second.cpp"
    second.write_text('''#include "ChatSymbols.h"
bool chatSymbolsSecondTranslationUnit() {
  return nativeChatSymbolLabel(0x1F64B) && !nativeChatSymbolLabel('*');
}
''')
    binary = temporary / "chat-symbols"
    subprocess.run([
        COMPILER, "-std=c++11", "-O1", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(FIRMWARE / "include"),
        str(FIRMWARE / "tests/test_chat_symbols.cpp"), str(second), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary), str(oracle)], check=True)
