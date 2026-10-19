#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../assets/chat-symbols/ChatSymbolRanges.h"

// Decode exactly one UTF-8 scalar. Invalid, overlong, surrogate or incomplete
// encodings return U+FFFD; no read extends beyond the supplied byte count.
inline uint32_t nativeChatCodepoint(const char* utf8, size_t bytes) {
  if (!utf8 || !bytes || bytes > 4) return 0xFFFD;
  const auto* data = reinterpret_cast<const uint8_t*>(utf8);
  const uint8_t first = data[0];
  const size_t width = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 :
      first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
  if (!width || width != bytes) return 0xFFFD;
  uint32_t result = width == 1 ? first : first & (0x7F >> width);
  for (size_t i = 1; i < width; ++i) {
    if ((data[i] & 0xC0) != 0x80) return 0xFFFD;
    result = (result << 6) | (data[i] & 0x3F);
  }
  if ((width == 2 && result < 0x80) || (width == 3 && result < 0x800) ||
      (width == 4 && result < 0x10000) || result > 0x10FFFF ||
      (result >= 0xD800 && result <= 0xDFFF)) return 0xFFFD;
  return result;
}

namespace native_chat_symbols_detail {
inline bool hiddenEmojiComponent(uint32_t value) {
  return value == 0xFE0E || value == 0xFE0F || value == 0x20E3 ||
      emojiModifier(value) || emojiTag(value);
}
inline bool preserveOperator(uint32_t value) {
  // Unicode gives these heavy operators Emoji_Presentation=Yes. Preserve
  // their mathematical content rather than changing them to a generic label.
  return (value >= 0x2795 && value <= 0x2797) || value == 0x1F7F0;
}
inline size_t utf8Width(const char* text, size_t available) {
  const uint8_t first = static_cast<uint8_t>(text[0]);
  const size_t width = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 :
      first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 1;
  return width <= available ? width : available;
}
inline bool emojiAfter(const char* text, size_t available) {
  for (size_t offset = 0; offset < available;) {
    const size_t width = utf8Width(text + offset, available - offset);
    const uint32_t value = nativeChatCodepoint(text + offset, width);
    if (!hiddenEmojiComponent(value)) return emojiJoinerBase(value) && !preserveOperator(value);
    offset += width;
  }
  return false;
}
}  // namespace native_chat_symbols_detail

// A display-time label, never a replacement stored in the message/DB. Default
// text-presentation symbols (e.g. ×, ♪, ⚠, ☀), ordinary text and ASCII return
// nullptr. No broad Unicode block test and no Emoji=Yes test (which includes
// ASCII #/*/digits). Hidden sequence controls are removed separately below.
inline const char* nativeChatSymbolLabel(uint32_t codepoint) {
  if (native_chat_symbols_detail::hiddenEmojiComponent(codepoint) ||
      native_chat_symbols_detail::preserveOperator(codepoint)) return nullptr;
  switch (codepoint) {
    case 0x1F44B: return "[招手]";
    case 0x1F600:
    case 0x1F604:
    case 0x1F60A:
    case 0x1F642: return "[微笑]";
    case 0x1F605: return "[汗笑]";
    case 0x1F64B: return "[举手]";
    case 0x1FAE1: return "[敬礼]";
    case 0x2705: return "[勾选]";
    default: return native_chat_symbols_detail::emojiPresentation(codepoint) ? "[表情]" : nullptr;
  }
}

// The caller supplies a writable bytes+1 buffer. This bounded in-place display
// normalization only removes independent-unrenderable emoji composition
// controls. It never expands labels, removes base text, or allocates memory.
// Preserve malformed bytes verbatim; upstream validation owns error policy.
inline size_t nativeChatNormalizeSymbols(char* text, size_t bytes) {
  if (!text) return 0;
  size_t read = 0, written = 0;
  uint32_t previousBase = 0;
  while (read < bytes) {
    const size_t width = native_chat_symbols_detail::utf8Width(text + read, bytes - read);
    const uint32_t codepoint = nativeChatCodepoint(text + read, width);
    const bool hidden = native_chat_symbols_detail::hiddenEmojiComponent(codepoint);
    const bool emojiJoiner = codepoint == 0x200D &&
        native_chat_symbols_detail::emojiJoinerBase(previousBase) &&
        !native_chat_symbols_detail::preserveOperator(previousBase) &&
        native_chat_symbols_detail::emojiAfter(text + read + width, bytes - read - width);
    if (!hidden && !emojiJoiner) {
      std::memmove(text + written, text + read, width);
      written += width;
    }
    if (!hidden && !emojiJoiner) previousBase = codepoint;
    read += width;
  }
  text[written] = 0;
  return written;
}
