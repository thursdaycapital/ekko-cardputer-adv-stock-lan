#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace native_chat {

inline size_t codepointBytes(uint8_t lead) {
  return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
}

// Reject embedded NUL, overlong forms, surrogates and codepoints > U+10FFFF.
// Buffers remain valid UTF-8 C strings; cursor positions are byte boundaries.
inline bool validUtf8(const char* text, size_t length) {
  if (!text) return length == 0;
  for (size_t i = 0; i < length;) {
    const uint8_t lead = static_cast<uint8_t>(text[i]);
    if (lead == 0) return false;
    if (lead < 0x80) { ++i; continue; }
    if (lead < 0xC2 || lead > 0xF4) return false;
    const size_t width = codepointBytes(lead);
    if (width > length - i) return false;
    const uint8_t second = static_cast<uint8_t>(text[i + 1]);
    if (second < 0x80 || second > 0xBF ||
        (lead == 0xE0 && second < 0xA0) ||
        (lead == 0xED && second > 0x9F) ||
        (lead == 0xF0 && second < 0x90) ||
        (lead == 0xF4 && second > 0x8F)) return false;
    for (size_t j = 2; j < width; ++j) {
      const uint8_t continuation = static_cast<uint8_t>(text[i + j]);
      if (continuation < 0x80 || continuation > 0xBF) return false;
    }
    i += width;
  }
  return true;
}

// Call after validUtf8(). Never returns a partial codepoint.
inline size_t prefixBytes(const char* text, size_t length, size_t limit) {
  size_t end = 0;
  while (end < length) {
    const size_t width = codepointBytes(static_cast<uint8_t>(text[end]));
    if (width > limit - end) break;
    end += width;
  }
  return end;
}

inline bool boundedIdentifier(const char* text, size_t capacity) {
  if (!text || !*text) return false;
  size_t length = 0;
  while (length <= capacity && text[length]) ++length;
  if (length > capacity || !validUtf8(text, length)) return false;
  for (size_t i = 0; i < length; ++i)
    if (static_cast<uint8_t>(text[i]) < 0x20 || text[i] == 0x7F) return false;
  return true;
}

} // namespace native_chat
