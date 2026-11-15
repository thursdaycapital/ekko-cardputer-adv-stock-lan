#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

enum class NativeChatKeyKind : uint8_t {
  None, Insert, Send, Newline, Backspace, Delete, Left, Right, ScrollUp, ScrollDown, Home, End, Refresh,
  ToggleHistory, HistoryOlder, HistoryNewer, ToggleChinese, AcknowledgeSend
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
  if (base == 0x28 && !mods.alt && (mods.fn || mods.shift)) {
    key.kind = historyMode ? NativeChatKeyKind::ToggleHistory : NativeChatKeyKind::Newline;
    return key;
  }
  if (base == 0x28 && mods.ctrl && !mods.alt) {
    key.kind = NativeChatKeyKind::AcknowledgeSend;
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
  static constexpr uint32_t kChordGraceMs = 20;
  void update(const std::array<NativeChatPhysicalKey, 56>& keys,
              const NativeChatModifiers& mods, bool enabled, uint32_t now, bool prime = false,
              bool historyMode = false) {
    const bool modeChanged = historyMode_ != historyMode;
    uint64_t claimed = 0;
    if (!enabled || prime || modeChanged) {
      head_ = count_ = pendingCount_ = 0;
    } else if (pendingCount_) {
      // Delay only ambiguous printable keys, never mutate a draft and then
      // attempt to undo it. A fast release or a newer ordinary key resolves
      // the original character first, preserving normal typing order.
      bool flush = false;
      for (size_t i = 0; i < pendingCount_; ++i)
        if (!keys[pending_[i].key].down) flush = true;
      for (size_t i = 0; i < keys.size(); ++i) {
        if (!keys[i].down || held_[i]) continue;
        const auto next = nativeChatKey(keys[i].base, keys[i].character, mods, historyMode);
        if (next.kind != NativeChatKeyKind::None && !deferPrintable(keys[i].base, next)) flush = true;
      }
      size_t index = 0;
      while (index < pendingCount_) {
        const Pending pending = pending_[index];
        const auto mapped = nativeChatKey(pending.base, static_cast<uint8_t>(pending.event.character), mods, historyMode);
        const bool modifier = mods.fn || mods.ctrl || mods.alt;
        if (!modifier && !flush && uint32_t(now - pending.startedAt) < kChordGraceMs) { ++index; continue; }
        if (modifier) {
          // Fn+Space is deliberately None: voice exclusively owns the chord.
          if (mapped.kind != NativeChatKeyKind::None) enqueue(mapped);
          claimed |= uint64_t(1) << pending.key;
        } else {
          enqueue(pending.event);
          printableConsumed_ |= uint64_t(1) << pending.key;
        }
        for (size_t j = index; j + 1 < pendingCount_; ++j) pending_[j] = pending_[j + 1];
        --pendingCount_;
      }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
      const NativeChatKey event = nativeChatKey(keys[i].base, keys[i].character, mods, historyMode);
      const bool repeatable = event.kind == NativeChatKeyKind::Backspace || event.kind == NativeChatKeyKind::Delete ||
          event.kind == NativeChatKeyKind::Left || event.kind == NativeChatKeyKind::Right ||
          event.kind == NativeChatKeyKind::ScrollUp || event.kind == NativeChatKeyKind::ScrollDown;
      if (keys[i].down && (!held_[i] || modeChanged)) repeatAt_[i] = now + 420;
      const bool repeat = repeatable && held_[i] && !modeChanged &&
          !(printableConsumed_ & (uint64_t(1) << i)) && static_cast<int32_t>(now - repeatAt_[i]) >= 0;
      const bool chineseChord = event.kind == NativeChatKeyKind::ToggleChinese && !ctrlHeld_ &&
          !(printableConsumed_ & (uint64_t(1) << i));
      if (enabled && !prime && !(claimed & (uint64_t(1) << i)) && keys[i].down &&
          (!held_[i] || repeat || chineseChord) && event.kind != NativeChatKeyKind::None) {
        if (deferPrintable(keys[i].base, event)) defer(i, keys[i].base, event, now);
        else enqueue(event);
        if (repeat) repeatAt_[i] = now + 90;
      }
      held_[i] = keys[i].down;
      if (!keys[i].down) printableConsumed_ &= ~(uint64_t(1) << i);
    }
    historyMode_ = historyMode;
    ctrlHeld_ = mods.ctrl;
    if (!enabled || prime) { head_ = count_ = pendingCount_ = 0; }
  }
  bool pop(NativeChatKey& key) {
    if (!count_) return false;
    key = queue_[head_]; head_ = (head_ + 1) % queue_.size(); --count_; return true;
  }
  bool pending() const { return count_ != 0 || pendingCount_ != 0; }
  uint32_t pressed() const { return pressed_; }
  uint32_t dropped() const { return dropped_; }
 private:
  struct Pending { NativeChatKey event; uint32_t startedAt = 0; uint8_t key = 0, base = 0; };
  static bool deferPrintable(uint8_t base, const NativeChatKey& event) {
    return event.kind == NativeChatKeyKind::Insert &&
        (base == ' ' || base == ',' || base == '/' || base == ';' || base == '.');
  }
  void defer(size_t key, uint8_t base, const NativeChatKey& event, uint32_t now) {
    if (pendingCount_ == pending_.size()) {
      // Even a synthetic scan with duplicate physical mappings stays ordered
      // and bounded. Real Cardputer hardware has five ambiguous base keys.
      for (size_t i = 0; i < pendingCount_; ++i) {
        enqueue(pending_[i].event);
        printableConsumed_ |= uint64_t(1) << pending_[i].key;
      }
      pendingCount_ = 0;
    }
    auto& pending = pending_[pendingCount_++];
    pending.key = static_cast<uint8_t>(key); pending.base = base; pending.event = event; pending.startedAt = now;
  }
  void enqueue(const NativeChatKey& event) {
    ++pressed_;
    if (count_ < queue_.size()) { queue_[(head_ + count_) % queue_.size()] = event; ++count_; }
    else ++dropped_;
  }
  std::array<bool, 56> held_{};
  std::array<uint32_t, 56> repeatAt_{};
  std::array<NativeChatKey, 24> queue_{};
  std::array<Pending, 5> pending_{};
  size_t head_ = 0, count_ = 0, pendingCount_ = 0;
  uint64_t printableConsumed_ = 0;
  uint32_t pressed_ = 0, dropped_ = 0;
  bool historyMode_ = false, ctrlHeld_ = false;
};
