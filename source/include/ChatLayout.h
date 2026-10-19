#pragma once
#include "ChatUtf8.h"
#include <algorithm>

constexpr size_t kNativeChatLineBytes = 96;
struct NativeChatLine {
  size_t offset;
  size_t bytes;
  uint16_t width;
  uint16_t index;
  NativeChatLine(size_t start, size_t length, uint16_t pixels, uint16_t number)
      : offset(start), bytes(length), width(pixels), index(number) {}
};

// Valid UTF-8, measured codepoint wrapping, bounded drawString scratch size.
// Newlines (including CRLF) preserve blank lines. measure() receives one
// NUL-terminated codepoint; visit() receives byte spans into the source.
template <class Measure, class Visit>
uint16_t nativeChatWrappedLines(const char* text, size_t length, uint16_t width,
                                Measure measure, Visit visit, uint16_t firstIndent = 0) {
  if (!native_chat::validUtf8(text, length)) return 0;
  size_t start = 0, position = 0;
  uint16_t pixels = 0, line = 0;
  while (position < length) {
    if (text[position] == '\n' || text[position] == '\r') {
      visit(NativeChatLine(start, position - start, pixels, line++));
      const bool crlf = text[position] == '\r' && position + 1 < length && text[position + 1] == '\n';
      position += crlf ? 2 : 1;
      start = position; pixels = 0;
      continue;
    }
    const size_t bytes = native_chat::codepointBytes(static_cast<uint8_t>(text[position]));
    char scalar[5]{};
    std::memcpy(scalar, text + position, bytes);
    const int measured = measure(scalar);
    const uint16_t scalarWidth = static_cast<uint16_t>(measured > 0 ? measured : 6);
    const uint16_t indent = line == 0 ? firstIndent : 0;
    const uint16_t available = width > indent ? width - indent : 1;
    if (position > start && (uint32_t(pixels) + scalarWidth > available ||
        position - start + bytes > kNativeChatLineBytes)) {
      visit(NativeChatLine(start, position - start, pixels, line++));
      start = position; pixels = 0;
      continue;
    }
    pixels = static_cast<uint16_t>(std::min<uint32_t>(65535, uint32_t(pixels) + scalarWidth));
    position += bytes;
  }
  visit(NativeChatLine(start, position - start, pixels, line++));
  return line;
}

struct NativeChatViewport {
  uint16_t totalLines = 0;
  uint16_t visibleLines = 0;
  uint16_t maxBottomOffset = 0;
  uint16_t bottomOffset = 0;
  uint16_t firstLine = 0;
};
inline NativeChatViewport nativeChatViewport(uint16_t total, uint16_t visible, uint16_t offset) {
  NativeChatViewport result;
  result.totalLines = total; result.visibleLines = visible;
  result.maxBottomOffset = total > visible ? total - visible : 0;
  result.bottomOffset = std::min(offset, result.maxBottomOffset);
  result.firstLine = result.maxBottomOffset - result.bottomOffset;
  return result;
}
