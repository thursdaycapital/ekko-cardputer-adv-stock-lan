"""Standalone stock-Studio protocol/state tests; no device or paid Agent runs."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

firmware = Path(__file__).resolve().parents[1]
catalog = json.loads((firmware / "config/agents.json").read_text())["agents"]
header = (firmware / "include/AgentSelection.h").read_text()
embedded = re.findall(r'\{"([^"\n]+)",\s*"([^"\n]+)",\s*"[^"\n]+"\}', header)
canonical = [(agent["id"], agent["name"]) for agent in catalog]
if embedded != canonical or len(embedded) != 16:
    raise SystemExit("Embedded Agent catalog drifted from pinned config/agents.json")
official = (firmware / "include/OfficialStudioProtocol.h").read_text()
routes = re.findall(r'\{"([^"\n]+)",\s*"([^"\n]+)",\s*(true|false),\s*(true|false)\}', official)
expected_routes = [
    (agent["id"], "cli" if agent["id"] == "hermes" else "builtin_agent" if agent["id"] == "ekko-agent" else "coding_agent",
     str("scoped" in agent["modes"]).lower(), str("global" in agent["modes"]).lower())
    for agent in catalog
]
if routes != expected_routes:
    raise SystemExit("Official route IDs/sources/modes drifted from pinned config/agents.json")
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++17 compiler required")
libdeps = Path(os.environ.get("PLATFORMIO_LIBDEPS_DIR", firmware / ".pio/libdeps/cardputer-adv"))
json_headers = libdeps / "ArduinoJson/src"
if not (json_headers / "ArduinoJson.h").is_file():
    raise SystemExit("Install pinned PlatformIO dependencies first, or set PLATFORMIO_LIBDEPS_DIR to their cardputer-adv directory")

# Legacy MCU selection ACK/envelope, remote fallback and automatic-listening
# tests are deliberately outside this stock-LAN suite. Auth identity, shared
# Wi-Fi and NVS ownership protections remain relevant in the new transport.
cases = {
    "test_navigation_input.cpp": (),
    "test_mcu_reauth.cpp": (),
    "test_mcu_identity.cpp": (),
    "test_shared_wifi.cpp": (),
    "test_wifi_recovery.cpp": (),
    "test_nvs_erase_policy.cpp": (),
    "test_conversation_view.cpp": (),
    "test_chat_keyboard.cpp": (),
    "test_key_click.cpp": (),
    "test_official_studio_connection.cpp": (),
    "test_official_studio_protocol.cpp": (),
    "test_official_studio_models.cpp": (),
    "test_bounded_json_reader.cpp": (),
    "test_official_studio_incoming.cpp": (),
    "test_websocket_frame_reader.cpp": (),
    "test_official_run_guard.cpp": (),
    "test_official_run_failure_notice.cpp": (),
    "test_official_session_store.cpp": (),
    "test_official_studio_selection.cpp": ("OfficialStudioSelection.cpp",),
    "test_official_studio_history.cpp": ("OfficialStudioHistory.cpp",),
    "test_official_speech_plan.cpp": (),
    "test_official_recording_store.cpp": ("OfficialRecordingStore.cpp",),
    "test_ram_recording_store.cpp": ("RamRecordingStore.cpp",),
    "test_mic_capture_queue.cpp": (),
}
with tempfile.TemporaryDirectory(prefix="cardputer-stock-lan-tests-") as temp:
    for source, implementations in cases.items():
        binary = Path(temp) / Path(source).stem
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(firmware / "include"), "-I", str(json_headers),
            str(firmware / "tests" / source),
            *(str(firmware / "src" / name) for name in implementations),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
subprocess.run([sys.executable, str(firmware / "tests/run_exclusive_recording_file_test.py")], check=True)
print(f"Stock-LAN protocol/state suite: {len(cases) + 1} programs passed; all 16 canonical routes match")
