"""Compile the actual Arduino GET transports with counting host SDK doubles."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
json_headers = firmware / ".pio/libdeps/cardputer-adv/ArduinoJson/src"
if not compiler or not (json_headers / "ArduinoJson.h").is_file():
    raise SystemExit("C++17 compiler and pinned PlatformIO ArduinoJson headers required")
with tempfile.TemporaryDirectory(prefix="stock-http-transport-") as temporary:
    temp = Path(temporary)
    for module in ("Selection", "History"):
        source = (firmware / f"src/OfficialStudio{module}.cpp").read_text()
        start = source.index("#ifdef ARDUINO\nnamespace {") + len("#ifdef ARDUINO\n")
        end = source.index("\n#endif", start)
        # The region is production code, not a duplicate transport policy.
        (temp / "arduino_transport_under_test.inc").write_text(source[start:end] + "\n")
        binary = temp / module.lower()
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-DTEST_SELECTION_TRANSPORT=" + ("1" if module == "Selection" else "0"),
            "-I", str(firmware / "include"), "-I", str(json_headers), "-I", str(temp),
            str(firmware / "tests/test_arduino_http_transport.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
