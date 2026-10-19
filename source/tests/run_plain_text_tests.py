"""C++11 bounded Markdown display checks, including ASan/UBSan and heap guard."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-plain-text-tests-") as temp:
    binary = Path(temp) / "plain-text"
    subprocess.run([
        compiler, "-std=c++11", "-O1", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_chat_plain_text.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
