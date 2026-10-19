#include "ChatLayout.h"
#include <array>
#include <cassert>
#include <iostream>
#include <string>

static int measure(const char* codepoint) { return static_cast<uint8_t>(*codepoint) < 0x80 ? 6 : 12; }

int main() {
  std::array<size_t, 2048> starts{}, lengths{};
  const auto collect = [&starts, &lengths](const NativeChatLine& line) {
    assert(line.index < starts.size()); starts[line.index] = line.offset; lengths[line.index] = line.bytes;
  };
  assert(nativeChatWrappedLines("你好abc", 9, 24, measure, collect) == 2);
  assert(starts[0] == 0 && lengths[0] == 6 && starts[1] == 6 && lengths[1] == 3);
  assert(nativeChatWrappedLines("abc", 3, 24, measure, collect, 18) == 2);
  assert(lengths[0] == 1 && starts[1] == 1 && lengths[1] == 2);
  assert(nativeChatWrappedLines("", 0, 200, measure, collect) == 1 && lengths[0] == 0);
  assert(nativeChatWrappedLines("a\r\nb\n", 5, 200, measure, collect) == 3);
  assert(starts[0] == 0 && lengths[0] == 1 && starts[1] == 3 && lengths[1] == 1 && starts[2] == 5 && lengths[2] == 0);
  const std::string newlines(1024, '\n');
  assert(nativeChatWrappedLines(newlines.c_str(), newlines.size(), 200, measure, collect) == 1025);
  const std::string many(1024, 'x');
  assert(nativeChatWrappedLines(many.c_str(), many.size(), 65535,
      [](const char*) { return 0; }, collect) == 11);
  for (size_t i = 0; i < 11; ++i) assert(lengths[i] <= kNativeChatLineBytes);
  std::string mixed;
  for (int i = 0; i < 100; ++i) mixed += "A你😀 ";
  size_t bytes = 0;
  nativeChatWrappedLines(mixed.c_str(), mixed.size(), 37, measure,
      [&mixed, &bytes](const NativeChatLine& line) {
        assert(native_chat::validUtf8(mixed.c_str() + line.offset, line.bytes));
        assert(line.bytes <= kNativeChatLineBytes && line.width <= 37);
        bytes += line.bytes;
      });
  assert(bytes == mixed.size());
  assert(nativeChatWrappedLines("\xF0\x80", 2, 200, measure, collect) == 0);
  NativeChatViewport view = nativeChatViewport(14, 5, 0);
  assert(view.firstLine == 9 && view.maxBottomOffset == 9 && view.bottomOffset == 0);
  view = nativeChatViewport(14, 5, 65535);
  assert(view.firstLine == 0 && view.bottomOffset == 9);
  view = nativeChatViewport(2, 5, 65535);
  assert(view.firstLine == 0 && view.maxBottomOffset == 0 && view.bottomOffset == 0);
  std::cout << "Chat layout: measured UTF8 wrap/newlines/bounds/viewport passed\n";
}
