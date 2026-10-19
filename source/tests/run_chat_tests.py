"""C++11 fixed-memory chat/editor checks, independent of ESP32 or a paid run."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++11 compiler required")
with tempfile.TemporaryDirectory(prefix="cardputer-chat-tests-") as temp:
    for source in ("test_chat_input.cpp", "test_chat_model.cpp", "test_chat_layout.cpp", "test_chat_renderer.cpp", "test_chat_browser.cpp", "test_pixel_font.cpp"):
        binary = Path(temp) / Path(source).stem
        include_args = []
        if source in ("test_chat_renderer.cpp", "test_chat_browser.cpp", "test_pixel_font.cpp"):
            gfx = firmware / ".pio/libdeps/cardputer-adv/M5GFX/src"
            if not gfx.is_dir():
                raise SystemExit("Run the pinned PlatformIO build to install M5GFX first")
            include_args = ["-I", str(firmware / "tests/fakes"), "-isystem", str(gfx)]
        subprocess.run([
            compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(firmware / "include"),
            *include_args,
            str(firmware / "tests" / source),
            str(firmware / "src/PinyinDictionaryData.cpp"),
            *([str(firmware / "src/EkkoPixelFont12.cpp")] if include_args else []),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
