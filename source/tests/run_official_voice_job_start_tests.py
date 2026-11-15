"""Actual ARDUINO Job allocation/start code with deterministic SDK doubles.

No device or network. The scheduler models task allocation and lifetime, not
Xtensa stack use; the real target still requires stack high-water observation.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++17 host compiler required")
with tempfile.TemporaryDirectory(prefix="arduino-voice-job-start-") as directory:
    temporary = Path(directory)
    common = [compiler, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
              "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
              "-I", str(firmware / "include")]
    objects = []
    # The job's complete Arduino branch is compiled directly, not copied into
    # the test. Only network/decoder portability uses the existing host branch.
    for name, arduino in [("OfficialStudioVoice", False), ("OfficialVoiceJob", True)]:
        output = temporary / f"{name}.o"
        flags = ["-DARDUINO", "-I", str(firmware / "tests/fakes/voice_job")] if arduino else []
        subprocess.run([*common, *flags, "-c", str(firmware / f"src/{name}.cpp"), "-o", str(output)], check=True)
        objects.append(str(output))
    executable = temporary / "arduino-voice-job-start"
    subprocess.run([*common, "-DARDUINO", "-I", str(firmware / "tests/fakes/voice_job"),
                    str(firmware / "tests/test_arduino_voice_job_start.cpp"), *objects,
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
