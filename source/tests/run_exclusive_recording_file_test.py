#!/usr/bin/env python3
"""Shared production stdio factory regression; no device, Studio or SD access."""
from pathlib import Path
import argparse
import hashlib
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--expect-red", action="store_true")
parser.add_argument("--evidence", type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="recording-exclusive-") as directory:
    temporary = Path(directory)
    files = temporary / "files"
    files.mkdir()
    executable = temporary / "test"
    subprocess.run([shutil.which("clang++") or "g++", "-std=c++11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-I", str(root / "include"),
                    str(root / "tests/test_exclusive_recording_file.cpp"), "-o", str(executable)], check=True)
    result = subprocess.run([str(executable), str(files)], capture_output=True, text=True)
    evidence = "Shared SdBackend creation algorithm; target libc capability replay, actual host filesystem I/O.\n"
    evidence += "Not a claim of physical target execution. C++11 -Werror ASan UBSan.\n"
    for name in ["include/ExclusiveRecordingFile.h", "src/OfficialRecordingStore.cpp", "tests/test_exclusive_recording_file.cpp"]:
        evidence += f"{name} SHA256 {hashlib.sha256((root / name).read_bytes()).hexdigest()}\n"
    evidence += f"exit={result.returncode}\n{result.stdout}{result.stderr}"
    print(evidence, end="")
    if args.evidence:
        args.evidence.write_text(evidence)
    if (result.returncode != 0) != args.expect_red:
        raise SystemExit("Unexpected regression verdict")
