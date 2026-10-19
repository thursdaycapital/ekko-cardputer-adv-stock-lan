"""Run the actual render state policy without ESP32 hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++17 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-render-tests-") as temp:
    binary = Path(temp) / "render-policy"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_render_policy.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    status = Path(temp) / "chat-status"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"),
        str(firmware / "tests/test_chat_status.cpp"), "-o", str(status),
    ], check=True)
    subprocess.run([str(status)], check=True)
    gfx = firmware / ".pio/libdeps/cardputer-adv/M5GFX/src"
    if not gfx.is_dir():
        raise SystemExit("Run the pinned PlatformIO build to install M5GFX first")
    colors = Path(temp) / "render-colors"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(firmware / "include"), "-isystem", str(gfx),
        str(firmware / "tests/test_render_colors.cpp"), "-o", str(colors),
    ], check=True)
    subprocess.run([str(colors)], check=True)
