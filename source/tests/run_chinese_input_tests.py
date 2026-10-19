"""C++11 Chinese composition checks using the actual bundled dictionary."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
if not (firmware / "include/PinyinDictionary.h").is_file():
    raise SystemExit("PinyinDictionary.h must be generated before Chinese input tests")
with tempfile.TemporaryDirectory(prefix="cardputer-chinese-input-tests-") as temp:
    binary = Path(temp) / "chinese-input"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_chinese_input.cpp"),
        str(firmware / "src/PinyinDictionaryData.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
