"""Validate display boundaries, protocol invariants and actual Fusion coverage."""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
main = (firmware / "src/main.cpp").read_text()
hardware = (firmware / "src/CardputerHardware.cpp").read_text()
renderer = (firmware / "include/ChatRenderer.h").read_text()
assert re.search(r'#define HERMES_MCU_FIRMWARE_VERSION "cardputer-adv-v[^"\n]+"', main)
assert 'String(native_ui::interactionLabel(mcuInteractionStatus.c_str()))' in main
assert 'appendInfoRow(html, F("设备状态"), interactionStatusLabel())' in main
assert 'native_ui::statusLabel(title.c_str())' in main
assert 'native_ui::statusLabel(hint.c_str())' in main
assert 'userFacingDetail(lastAudioDetail' in main and 'userFacingDetail(lastMcuLoginDetail' in main
assert 'setChatNotice(chatFailureNotice(' in main  # Standard run.failed projection.
assert 'return String(official_run_failure::notice(error.c_str()).text);' in main
assert '"NATIVE / ONLINE"' not in hardware and '"DISPLAY RAM LOW"' not in hardware
assert 'glyph5x7' not in main
assert '"中文 Ctrl+Space"' in renderer and '"Fn+空格说话"' in renderer
assert '"我"' not in renderer and '"AI"' not in renderer
# A default profile is translated only in the display half of an HTML option.
assert '''html += escapeHtml(loginProfiles[i]);
      html += F("'>");
      html += escapeHtml(native_ui::profileLabel(loginProfiles[i].c_str()));''' in main
# Display translations must not replace the actual Profile stored or sent.
assert re.search(r'official_connection::statusAuth\([^;]+selectedProfile\.c_str\(\)', main)
assert 'requestedProfile = server.arg(F("profile"))' in main
assert 'selectedProfile = requestedProfile' in main
assert 'prefs.getString("cur_profile", "")' in main
assert '&ekko_pixel_font::kFont' in hardware
assert '&fonts::efontCN_16' not in hardware
assert 'selectionMessage(selection, selectionError, chatNotice)' in hardware

# Every static status argument must have a display translation, including
# conditional/multiline calls. Dynamic IPs and interaction labels stay dynamic.
localization = (firmware / "include/UiLocalization.h").read_text()
status_keys = set(re.findall(r'\{"([A-Z /]+)",', localization))
status_arguments = set()
for match in re.finditer(r'setOledStatus\(', main):
    position, depth, quoted, escaped = match.end(), 1, False, False
    while depth:
        character = main[position]
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
        elif character == '"':
            quoted = True
        elif character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        position += 1
    status_arguments.update(re.findall(r'F\("([A-Z /]+)"\)', main[match.end():position]))
assert not status_arguments - status_keys - {"AUTO "}, status_arguments - status_keys

codepoints = {int(value.removeprefix("U+"), 16) for value in
              (firmware / "assets/fusion-pixel/codepoints.txt").read_text().splitlines()}
labels = []
for relative in ("src/main.cpp", "src/CardputerHardware.cpp", "include/ChatRenderer.h",
                 "include/ChatStatus.h", "include/ChatSymbols.h", "include/UiLocalization.h",
                 "include/OfficialStudioSelectionNotice.h", "include/OfficialRunFailureNotice.h"):
    text = (firmware / relative).read_text()
    labels.extend(re.findall(r'"((?:[^"\\]|\\.)*)"', text))
characters = {character for label in labels for character in label if ord(character) >= 0x80}
missing = sorted(character for character in characters if ord(character) not in codepoints)
assert not missing, "Fusion missing UI glyphs: " + repr(missing)
web_directory = firmware / "assets/fusion-pixel/web"
web_codepoints = {int(value.removeprefix("U+"), 16) for value in
                  (web_directory / "codepoints.txt").read_text().splitlines()}
assert all(ord(character) in web_codepoints for character in characters), "Webfont missing fixed UI glyphs"
web_bytes = (web_directory / "CardputerPixelWeb.woff2").read_bytes()
web_provenance = json.loads((web_directory / "provenance.json").read_text())
embedded_bytes = bytes(int(value, 16) for value in re.findall(
    r'0x([0-9a-fA-F]{2})', (firmware / "src/FusionWebFont.cpp").read_text()))
web_header = (firmware / "include/FusionWebFont.h").read_text()
web_size = int(re.search(r'kSize = (\d+)', web_header).group(1))
assert web_bytes[:4] == b"wOF2" and web_bytes == embedded_bytes
assert len(web_bytes) == web_size == web_provenance["bytes"]
assert hashlib.sha256(web_bytes).hexdigest() == web_provenance["sha256"]
assert 'server.on(F("/font/fusion-pixel.woff2"), HTTP_GET' in main
assert 'server.send_P(200, PSTR("font/woff2")' in main
assert 'reinterpret_cast<const char*>(fusion_web_font::kData), fusion_web_font::kSize)' in main
assert "@font-face{font-family:CardputerPixel;src:url('/font/fusion-pixel.woff2') format('woff2')" in main
assert "font-family:CardputerPixel," in main
page_style = main[main.index("String pageStart("):main.index("String pageEnd(")]
for font in re.findall(r'font:([^;{}"]+);', page_style):
    assert "CardputerPixel," in font, "Non-Fusion CSS font: " + font
compiler = shutil.which("clang++") or shutil.which("g++")
assert compiler, "C++11 compiler required"
with tempfile.TemporaryDirectory(prefix="cardputer-localization-tests-") as temporary:
    binary = Path(temporary) / "localization"
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(firmware / "include"),
                    str(firmware / "tests/test_ui_localization.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    # Exercise the actual firmware truncator with a minimal Arduino String
    # boundary. This catches half-character truncation independently of fonts.
    start = main.index("String fitOledText(")
    end = main.index("\nuint8_t clampUiValue", start)
    selection_start = hardware.index("const char* selectionMessage(")
    selection_end = hardware.index("\n}\n}", selection_start) + 2
    truncate_source = Path(temporary) / "truncate.cpp"
    truncate_source.write_text(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include "AgentSelection.h"
using std::min;
#define F(value) value
struct String {
  std::string value;
  String(const char* text = "") : value(text) {}
  String(std::string text) : value(text) {}
  void trim() {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { value.clear(); return; }
    value = value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
  }
  size_t length() const { return value.size(); }
  const char* c_str() const { return value.c_str(); }
  bool operator==(const char* other) const { return value == other; }
  char operator[](size_t index) const { return value[index]; }
  String substring(size_t start, size_t end) const { return value.substr(start, end - start); }
  String operator+(const char* tail) const { return value + tail; }
};
''' + main[start:end] + hardware[selection_start:selection_end] + r'''
int main() {
  assert(fitOledText(" 中文状态等待连接 ", 4).value == "中文状…");
  assert(fitOledText("中文", 2).value == "中文");
  assert(fitOledText("中文", 1).value == "…");
  assert(fitOledText("中文", 0).value.empty());
  assert(fitOledText("Fn+空格说话", 6).value == "Fn+空格…");
  assert(fitOledText("Ekko", 8).value == "Ekko");
  NativeAgentSelection selection;
  String error("standard_agent_unavailable"), notice("CLI 未安装，请在电脑安装");
  assert(std::string(selectionMessage(selection, error, notice)) == notice.value);
  notice.value = "Agent 未授权，请在电脑登录";
  assert(std::string(selectionMessage(selection, error, notice)) == notice.value);
  selection.pending = true;
  assert(std::string(selectionMessage(selection, error, notice)) == "正在读取配置");
  selection.pending = false; notice.value.clear();
  assert(std::string(selectionMessage(selection, error, notice)) == "请检查智能体配置");
  notice.value = "CLI 未安装"; error.value = "standard_session_storage_failed";
  assert(std::string(selectionMessage(selection, error, notice)) == "会话存储不可用");
  error.value = "factory_unavailable";
  assert(std::string(selectionMessage(selection, error, notice)) == "原厂固件不可用");
}
''')
    truncate_binary = Path(temporary) / "truncate"
    subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(firmware / "include"),
                    str(truncate_source), "-o", str(truncate_binary)], check=True)
    subprocess.run([str(truncate_binary)], check=True)
print(f"Fusion LCD/Webfont UI coverage: {len(characters)} non-ASCII characters, zero missing; embedded WOFF2 {web_size} bytes; actual display boundaries and protocol invariants passed")
