#!/usr/bin/env python3
"""Local synthetic audio only. No Studio, model, STT or TTS network calls."""
from pathlib import Path
import argparse
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--wav-fixture', type=Path, help='Optional existing WAV from Studio; no network request is made')
args = parser.parse_args()
if args.wav_fixture and not args.wav_fixture.is_file():
    parser.error('WAV fixture does not exist')
compiler = shutil.which("clang++") or shutil.which("g++")
ffmpeg = shutil.which("ffmpeg")
if not compiler or not ffmpeg:
    raise SystemExit("host C++ compiler and ffmpeg are required")
with tempfile.TemporaryDirectory(prefix="official-voice-") as directory:
    temp = Path(directory)
    fixtures = []
    for rate, channels, bitrate in [(16000, 2, "64k"), (44100, 1, "128k"), (8000, 1, "24k")]:
        fixture = temp / f"sine-{rate}.mp3"
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        f"sine=frequency=1000:sample_rate={rate}:duration=1", "-ac", str(channels),
                        "-c:a", "libmp3lame", "-b:a", bitrate, "-write_xing", "0", "-id3v2_version", "0",
                        str(fixture)], check=True)
        fixtures.append(str(fixture))
    executable = temp / "voice-tests"
    subprocess.run([compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(root / "include"), str(root / "src/OfficialStudioVoice.cpp"),
                    str(root / "src/RamRecordingStore.cpp"),
                    str(root / "tests/test_official_studio_voice.cpp"), "-o", str(executable)], check=True)
    supplied = [str(args.wav_fixture.resolve())] if args.wav_fixture else []
    subprocess.run([str(executable), *fixtures, *supplied], check=True)
    executable = temp / "voice-job-tests"
    subprocess.run([compiler, "-std=c++11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread",
                    "-I", str(root / "include"), str(root / "src/OfficialStudioVoice.cpp"),
                    str(root / "src/OfficialVoiceJob.cpp"),
                    str(root / "tests/test_official_voice_job.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable), fixtures[0]], check=True)
