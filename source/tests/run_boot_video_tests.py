"""Bounded boot playback contracts without device/network/codec substitution."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-boot-tests-") as temp:
    binary = Path(temp) / "boot-video"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_boot_video.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
