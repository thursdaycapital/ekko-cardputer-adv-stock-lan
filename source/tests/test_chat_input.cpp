#include "ChatInput.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main() {
  NativeChatInput input;
  assert(input.empty() && input.cursor() == 0 && !input.hasContent());
  assert(!input.backspace() && !input.deleteForward() && !input.moveLeft() && !input.moveRight());
  assert(input.insert("你好😀a") == NativeChatEditResult::Ok);
  assert(input.size() == 11 && input.cursor() == 11);
  assert(input.moveLeft() && input.cursor() == 10);
  assert(input.backspace() && std::strcmp(input.data(), "你好a") == 0 && input.cursor() == 6);
  assert(input.moveLeft() && input.cursor() == 3);
  assert(input.deleteForward() && std::strcmp(input.data(), "你a") == 0);
  assert(!input.setCursor(1) && !input.setCursor(2));
  assert(input.insert("好") == NativeChatEditResult::Ok);
  assert(std::strcmp(input.data(), "你好a") == 0);
  assert(input.home() && input.insert(' ') == NativeChatEditResult::Ok);
  assert(input.end() && input.insert(" \n下一行") == NativeChatEditResult::Ok);
  assert(input.lineHome() && std::strcmp(input.data() + input.cursor(), "下一行") == 0);
  assert(input.lineEnd() && input.cursor() == input.size());
  assert(input.hasContent());
  input.clear();
  assert(input.insert(" \t\n\r") == NativeChatEditResult::Ok && !input.hasContent());
  assert(input.set("x") == NativeChatEditResult::Ok);
  const uint32_t unchanged = input.revision();
  assert(input.set("x") == NativeChatEditResult::Ok && input.revision() == unchanged);

  const char* invalid[] = {"\x80", "\xC0\x80", "\xC1\xBF", "\xC2", "\xDF\x7F",
      "\xE0\x9F\xBF", "\xED\xA0\x80", "\xEF\xBF", "\xF0\x8F\xBF\xBF",
      "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFF"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    assert(input.insert(invalid[i]) == NativeChatEditResult::InvalidUtf8);
    assert(input.set(invalid[i]) == NativeChatEditResult::InvalidUtf8);
    assert(std::strcmp(input.data(), "x") == 0 && input.revision() == unchanged);
  }
  const char embeddedNul[] = {'a', 0, 'b'};
  assert(input.insert(embeddedNul, sizeof(embeddedNul)) == NativeChatEditResult::InvalidUtf8);

  std::string ascii(kNativeChatDraftBytes - 1, 'a');
  assert(input.set(ascii.c_str()) == NativeChatEditResult::Ok);
  assert(input.insert("中") == NativeChatEditResult::Full && input.size() == 511);
  assert(input.insert("xy中") == NativeChatEditResult::Truncated && input.size() == 512);
  assert(input.data()[511] == 'x' && input.data()[512] == 0);
  assert(input.insert("z") == NativeChatEditResult::Full);
  for (size_t remaining = 0; remaining < 5; ++remaining) {
    const std::string prefix(kNativeChatDraftBytes - remaining, 'a');
    assert(input.set(prefix.c_str()) == NativeChatEditResult::Ok);
    const NativeChatEditResult result = input.insert("😀");
    assert(result == (remaining >= 4 ? NativeChatEditResult::Ok : NativeChatEditResult::Full));
    assert(native_chat::validUtf8(input.data(), input.size()));
  }
  const std::string tooLong = std::string(511, 'x') + "😀";
  assert(input.set(tooLong.c_str()) == NativeChatEditResult::Truncated && input.size() == 511);
  assert(input.set("abc") == NativeChatEditResult::Ok);
  assert(input.home());
  assert(input.insert(input.data(), input.size()) == NativeChatEditResult::Ok);
  assert(std::strcmp(input.data(), "abcabc") == 0);

  // Every valid scalar width can be traversed/deleted without a broken prefix.
  const char* boundaries[] = {"\x7F", "\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80",
      "\xED\x9F\xBF", "\xEE\x80\x80", "\xEF\xBF\xBF", "\xF0\x90\x80\x80", "\xF4\x8F\xBF\xBF"};
  for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); ++i) {
    assert(input.set(boundaries[i]) == NativeChatEditResult::Ok);
    assert(input.moveLeft() && input.cursor() == 0);
    assert(input.moveRight() && input.cursor() == input.size());
    assert(input.backspace() && input.empty());
  }
  std::cout << "Chat input: UTF8/edit/cursor/newline/space/capacity/aliasing passed; sizeof="
            << sizeof(NativeChatInput) << '\n';
}
