"""Ownership and asynchronous buffer-release tests; no audio/device calls."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-audio-queue-") as temp:
    binary = Path(temp) / "audio-stream-queue"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_audio_stream_queue.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
