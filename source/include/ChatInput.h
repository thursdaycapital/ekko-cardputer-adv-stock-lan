#pragma once
#include "ChatUtf8.h"
#include <algorithm>

constexpr size_t kNativeChatDraftBytes = 512;
enum class NativeChatEditResult { Ok, Truncated, Full, InvalidUtf8 };

// Fixed storage, no heap allocation. This editor treats a Unicode codepoint
// as a cursor unit (not a complete Unicode grapheme cluster).
class NativeChatInput {
 public:
  const char* data() const { return text_; }
  size_t size() const { return length_; }
  size_t cursor() const { return cursor_; }
  bool empty() const { return length_ == 0; }
  uint32_t revision() const { return revision_; }

  bool hasContent() const {
    for (size_t i = 0; i < length_; ++i)
      if (text_[i] != ' ' && text_[i] != '\t' && text_[i] != '\n' && text_[i] != '\r') return true;
    return false;
  }

  NativeChatEditResult set(const char* text, size_t length) {
    if (!native_chat::validUtf8(text, length)) return NativeChatEditResult::InvalidUtf8;
    const size_t bytes = native_chat::prefixBytes(text, length, kNativeChatDraftBytes);
    const bool changed = length_ != bytes || cursor_ != bytes ||
        (bytes && std::memcmp(text_, text, bytes) != 0);
    if (bytes) std::memmove(text_, text, bytes);
    length_ = cursor_ = static_cast<uint16_t>(bytes);
    text_[bytes] = '\0';
    if (changed) ++revision_;
    return bytes == length ? NativeChatEditResult::Ok : NativeChatEditResult::Truncated;
  }
  NativeChatEditResult set(const char* text) {
    return set(text, text ? std::strlen(text) : 0);
  }

  NativeChatEditResult insert(const char* text, size_t length) {
    if (!native_chat::validUtf8(text, length)) return NativeChatEditResult::InvalidUtf8;
    if (!length) return NativeChatEditResult::Ok;
    const size_t remaining = kNativeChatDraftBytes - length_;
    const size_t bytes = native_chat::prefixBytes(text, length, remaining);
    if (!bytes) return NativeChatEditResult::Full;
    // Snapshot before moving: insertion from this editor's own text is safe.
    char inserted[kNativeChatDraftBytes];
    std::memcpy(inserted, text, bytes);
    std::memmove(text_ + cursor_ + bytes, text_ + cursor_, length_ - cursor_ + 1);
    std::memcpy(text_ + cursor_, inserted, bytes);
    cursor_ += static_cast<uint16_t>(bytes);
    length_ += static_cast<uint16_t>(bytes);
    ++revision_;
    return bytes == length ? NativeChatEditResult::Ok : NativeChatEditResult::Truncated;
  }
  NativeChatEditResult insert(const char* text) {
    return insert(text, text ? std::strlen(text) : 0);
  }
  NativeChatEditResult insert(char ascii) { return insert(&ascii, 1); }

  bool moveLeft() {
    if (!cursor_) return false;
    --cursor_;
    while (cursor_ && (static_cast<uint8_t>(text_[cursor_]) & 0xC0) == 0x80) --cursor_;
    ++revision_; return true;
  }
  bool moveRight() {
    if (cursor_ == length_) return false;
    cursor_ += static_cast<uint16_t>(native_chat::codepointBytes(static_cast<uint8_t>(text_[cursor_])));
    ++revision_; return true;
  }
  bool setCursor(size_t cursor) {
    if (cursor > length_ || (cursor < length_ &&
        (static_cast<uint8_t>(text_[cursor]) & 0xC0) == 0x80)) return false;
    if (cursor == cursor_) return false;
    cursor_ = static_cast<uint16_t>(cursor); ++revision_; return true;
  }
  bool home() { return setCursor(0); }
  bool end() { return setCursor(length_); }
  bool lineHome() {
    size_t position = cursor_;
    while (position && text_[position - 1] != '\n') --position;
    return setCursor(position);
  }
  bool lineEnd() {
    size_t position = cursor_;
    while (position < length_ && text_[position] != '\n') ++position;
    return setCursor(position);
  }
  bool backspace() {
    if (!cursor_) return false;
    const size_t oldCursor = cursor_;
    moveLeft();
    std::memmove(text_ + cursor_, text_ + oldCursor, length_ - oldCursor + 1);
    length_ -= static_cast<uint16_t>(oldCursor - cursor_);
    return true;
  }
  bool deleteForward() {
    if (cursor_ == length_) return false;
    const size_t bytes = native_chat::codepointBytes(static_cast<uint8_t>(text_[cursor_]));
    std::memmove(text_ + cursor_, text_ + cursor_ + bytes, length_ - cursor_ - bytes + 1);
    length_ -= static_cast<uint16_t>(bytes); ++revision_; return true;
  }
  void clear() {
    if (!length_ && !cursor_) return;
    text_[0] = '\0'; length_ = cursor_ = 0; ++revision_;
  }

 private:
  char text_[kNativeChatDraftBytes + 1]{};
  uint16_t length_ = 0, cursor_ = 0;
  uint32_t revision_ = 0;
};
