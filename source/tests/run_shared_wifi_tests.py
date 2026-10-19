"""Shared, dependency-free Wi-Fi record tests; never reads device credentials."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-shared-wifi-tests-") as temp:
    binary = Path(temp) / "shared-wifi"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_shared_wifi.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
