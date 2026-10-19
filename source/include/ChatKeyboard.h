#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

enum class NativeChatKeyKind : uint8_t {
  None, Insert, Send, Newline, Backspace, Delete, Left, Right, ScrollUp, ScrollDown, Home, End, Refresh,
  ToggleHistory, HistoryOlder, HistoryNewer, ToggleChinese
};
struct NativeChatKey {
  NativeChatKeyKind kind = NativeChatKeyKind::None;
  char character = 0;
};
struct NativeChatPhysicalKey {
  bool down = false;
  uint8_t base = 0, character = 0;
};
struct NativeChatModifiers { bool fn = false, shift = false, ctrl = false, alt = false; };

// Decode the physical base key before its printable character. The SDK's HID
// Enter/Delete/Tab values also happen to be printable ASCII punctuation.
inline NativeChatKey nativeChatKey(uint8_t base, uint8_t character,
                                   const NativeChatModifiers& mods, bool historyMode = false) {
  NativeChatKey key;
  if (base == ' ' && mods.fn) return key; // Voice owns Fn+Space, even with Ctrl held.
  if (base == ' ' && mods.ctrl && !mods.alt) {
    key.kind = NativeChatKeyKind::ToggleChinese;
    return key;
  }
  if (mods.ctrl || mods.alt) return key;
  if (base == 0x28) key.kind = historyMode ? NativeChatKeyKind::ToggleHistory :
      mods.fn || mods.shift ? NativeChatKeyKind::Newline : NativeChatKeyKind::Send;
  else if (base == 0x2a) key.kind = mods.fn ? NativeChatKeyKind::Delete : NativeChatKeyKind::Backspace;
  else if (base == 0x2b) {
    key.kind = mods.fn ? NativeChatKeyKind::Insert : NativeChatKeyKind::ToggleHistory;
    if (mods.fn) key.character = '\t';
  }
  else if (historyMode && !mods.fn && base == ';') key.kind = NativeChatKeyKind::ScrollUp;
  else if (historyMode && !mods.fn && base == '.') key.kind = NativeChatKeyKind::ScrollDown;
  else if (historyMode && !mods.fn && base == ',') key.kind = NativeChatKeyKind::HistoryOlder;
  else if (historyMode && !mods.fn && base == '/') key.kind = NativeChatKeyKind::HistoryNewer;
  else if (historyMode && !mods.fn && base == 'r') key.kind = NativeChatKeyKind::Refresh;
  else if (mods.fn && base == ',') key.kind = NativeChatKeyKind::Left;
  else if (mods.fn && base == '/') key.kind = NativeChatKeyKind::Right;
  else if (mods.fn && base == ';') key.kind = NativeChatKeyKind::ScrollUp;
  else if (mods.fn && base == '.') key.kind = NativeChatKeyKind::ScrollDown;
  else if (mods.fn && base == '[') key.kind = NativeChatKeyKind::Home;
  else if (mods.fn && base == ']') key.kind = NativeChatKeyKind::End;
  else if (mods.fn && base == 'r') key.kind = NativeChatKeyKind::Refresh;
  else if (base == '`' && !mods.fn) return key; // Physical Esc is navigation.
  else if ((!mods.fn || base == '`') && base < 0x80 && character >= 0x20 && character < 0x7f) {
    key.kind = NativeChatKeyKind::Insert; key.character = static_cast<char>(character);
  }
  return key;
}

class NativeChatKeyboard {
 public:
  void update(const std::array<NativeChatPhysicalKey, 56>& keys,
              const NativeChatModifiers& mods, bool enabled, uint32_t now, bool prime = false,
              bool historyMode = false) {
    const bool modeChanged = historyMode_ != historyMode;
    // Give a modifier arriving on the next physical scan a chance to claim
    // Space before it changes the draft. Flush before newer keys to preserve
    // normal typing order. No editing rollback is needed by the caller.
    if (spacePending_) {
      if (enabled && !prime && !modeChanged && !mods.ctrl && !mods.fn && !mods.alt) {
        NativeChatKey space; space.kind = NativeChatKeyKind::Insert; space.character = ' ';
        enqueue(space);
      }
      spacePending_ = false;
    }
    for (size_t i = 0; i < keys.size(); ++i) {
      const NativeChatKey event = nativeChatKey(keys[i].base, keys[i].character, mods, historyMode);
      const bool repeatable = event.kind == NativeChatKeyKind::Backspace || event.kind == NativeChatKeyKind::Delete ||
          event.kind == NativeChatKeyKind::Left || event.kind == NativeChatKeyKind::Right ||
          event.kind == NativeChatKeyKind::ScrollUp || event.kind == NativeChatKeyKind::ScrollDown;
      if (keys[i].down && (!held_[i] || modeChanged)) repeatAt_[i] = now + 420;
      const bool repeat = repeatable && held_[i] && !modeChanged && static_cast<int32_t>(now - repeatAt_[i]) >= 0;
      // Fn can arrive a scan after the physical arrow. Recognize that chord
      // immediately instead of requiring another press or the 420ms repeat.
      // A mode change alone never replays an already held key into the draft.
      const bool scrollChord = held_[i] && !modeChanged && kinds_[i] != event.kind &&
          (event.kind == NativeChatKeyKind::ScrollUp || event.kind == NativeChatKeyKind::ScrollDown);
      const bool chineseChord = event.kind == NativeChatKeyKind::ToggleChinese && !ctrlHeld_;
      if (enabled && !prime && keys[i].down && (!held_[i] || repeat || scrollChord || chineseChord) && event.kind != NativeChatKeyKind::None) {
        if (event.kind == NativeChatKeyKind::Insert && event.character == ' ') spacePending_ = true;
        else enqueue(event);
        if (repeat) repeatAt_[i] = now + 90;
      }
      held_[i] = keys[i].down;
      kinds_[i] = event.kind;
    }
    historyMode_ = historyMode;
    ctrlHeld_ = mods.ctrl;
    if (!enabled || prime) { head_ = count_ = 0; spacePending_ = false; }
  }
  bool pop(NativeChatKey& key) {
    if (!count_) return false;
    key = queue_[head_]; head_ = (head_ + 1) % queue_.size(); --count_; return true;
  }
  bool pending() const { return count_ != 0 || spacePending_; }
  uint32_t pressed() const { return pressed_; }
  uint32_t dropped() const { return dropped_; }
 private:
  void enqueue(const NativeChatKey& event) {
    ++pressed_;
    if (count_ < queue_.size()) { queue_[(head_ + count_) % queue_.size()] = event; ++count_; }
    else ++dropped_;
  }
  std::array<bool, 56> held_{};
  std::array<NativeChatKeyKind, 56> kinds_{};
  std::array<uint32_t, 56> repeatAt_{};
  std::array<NativeChatKey, 24> queue_{};
  size_t head_ = 0, count_ = 0;
  uint32_t pressed_ = 0, dropped_ = 0;
  bool historyMode_ = false;
  bool ctrlHeld_ = false, spacePending_ = false;
};
