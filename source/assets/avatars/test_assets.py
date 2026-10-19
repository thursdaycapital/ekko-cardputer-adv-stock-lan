"""Verify original artwork, canonical coverage and generated firmware data offline."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import tempfile

base = Path(__file__).resolve().parent
firmware = base.parents[1]
manifest = json.loads((base / "manifest.json").read_text())
catalog = json.loads((firmware / "config/agents.json").read_text())["agents"]
assert [a["id"] for a in manifest["assets"]] == [a["id"] for a in catalog]
assert len(manifest["assets"]) == 16
assert len({a["file"] for a in manifest["assets"]}) == 16
for asset in manifest["assets"]:
    original = (base / asset["file"]).resolve()
    assert original.is_relative_to(base)
    data = original.read_bytes()
    assert hashlib.sha256(data).hexdigest() == asset["sha256"], asset["id"]
    if "downloaded_source_sha256" in asset:
        assert re.fullmatch(r"[a-f0-9]{64}", asset["downloaded_source_sha256"]), asset["id"]
    assert asset["source_url"].startswith("https://") and asset["license"]
    if asset["original_format"] == "svg":
        assert b"<svg" in data
        assert not re.search(rb"<image\b|data:image|<script\b|<text\b", data, re.I), asset["id"]
    else:
        assert asset["original_format"] == "png" and data.startswith(b"\x89PNG\r\n\x1a\n")
subprocess.run(["node", str(base / "generate.mjs"), "--check"], cwd=firmware, check=True)
compiler = shutil.which("clang++") or shutil.which("g++")
if not compiler:
    raise SystemExit("C++17 compiler required")
program = r'''
#include "AgentAvatarAssets.h"
#include <cassert>
#include <set>
int main() {
  static_assert(kAgentAvatarCount == 16);
  std::set<const uint16_t*> sources;
  for (size_t index = 0; index < 16; ++index) {
    const auto* avatar = nativeAgentLogo(index);
    assert(avatar && avatar->width == 44 && avatar->height == 44);
    assert(sources.insert(avatar->pixels).second);
    size_t visible = 0;
    for (size_t y = 0; y < 44; ++y) for (size_t x = 0; x < 44; ++x) {
      uint16_t pixel = avatar->pixels[y * 44 + x];
      if (x < 4 || y < 4 || x >= 40 || y >= 40) assert(pixel == 0xEF3A);
      else visible += pixel != 0xEF3A;
    }
    assert(visible > 40);
  }
  assert(!nativeAgentLogo(16));
  assert(!nativeAgentLogo(static_cast<size_t>(-1)));
}
'''
with tempfile.TemporaryDirectory(prefix="native-agent-avatar-test-") as temp:
    executable = Path(temp) / "avatars"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-x", "c++",
                    "-fsanitize=address,undefined", "-I", str(firmware / "include"), "-", "-o", str(executable)],
                   input=program, text=True, check=True)
    subprocess.run([str(executable)], check=True)
print("16 canonical avatars: original hashes, vector/raster provenance, deterministic pixels and C++ bounds passed")
