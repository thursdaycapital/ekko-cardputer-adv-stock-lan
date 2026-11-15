"""Exercise the production asynchronous authentication pipeline and guard.

Native binding remains on-device. Fixtures contain no real credentials.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-native-auth-tests-") as temporary:
    binary = Path(temporary) / "native-auth"
    subprocess.run([
        compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_studio_auth.cpp"),
        str(firmware / "src/StudioAuthJob.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
