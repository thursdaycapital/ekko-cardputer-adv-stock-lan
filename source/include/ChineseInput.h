#pragma once
#include "ChatInput.h"
#include "ChatKeyboard.h"
#include "PinyinDictionary.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

enum class NativeChineseInputFeedback : uint8_t {
  None, NoCandidate, PreeditFull, DraftFull, NoPreviousPage, NoNextPage, InvalidCandidate
};

struct NativeChineseInputResult {
  bool handled;
  NativeChatEditResult edit;
  NativeChineseInputResult(bool consumed = false, NativeChatEditResult result = NativeChatEditResult::Ok)
      : handled(consumed), edit(result) {}
};

// One instance belongs to one Agent/profile draft. All composition and the
// current candidate page are owned fixed storage; no global composing state.
class NativeChineseInput {
 public:
  bool enabled() const { return enabled_; }
  bool composing() const { return length_ != 0; }
  const char* preedit() const { return preedit_; }
  size_t page() const { return candidates_.page; }
  uint32_t revision() const { return revision_; }
  const NativePinyinPage& candidates() const { return candidates_; }
  NativeChineseInputFeedback feedback() const { return feedback_; }

  // Intentional context reset only: callers must preserve a composing context
  // when switching Agents, just as they preserve its unsent draft.
  void reset() {
    const bool changed = !enabled_ || composing() || feedback_ != NativeChineseInputFeedback::None;
    enabled_ = true; length_ = 0; preedit_[0] = 0;
    candidates_ = NativePinyinPage{}; feedback_ = NativeChineseInputFeedback::None;
    if (changed) ++revision_;
  }

  NativeChineseInputResult handle(const NativeChatKey& key, NativeChatInput& draft) {
    if (key.kind == NativeChatKeyKind::ToggleChinese) {
      if (composing()) {
        const NativeChineseInputResult result = commitRaw(draft);
        if (result.edit != NativeChatEditResult::Ok) return result;
      }
      enabled_ = !enabled_; setFeedback(NativeChineseInputFeedback::None); ++revision_;
      return NativeChineseInputResult(true);
    }
    if (!enabled_) return NativeChineseInputResult();

    if (key.kind == NativeChatKeyKind::Insert) {
      const char character = key.character;
      if (isLetter(character) || (character == '\'' && composing())) return append(character);
      if (!composing()) return NativeChineseInputResult();
      if (character == ' ') {
        if (!candidates_.count) return noCandidate();
        return commitCandidate(0, draft);
      }
      if (character >= '1' && character <= '5')
        return commitCandidate(static_cast<size_t>(character - '1'), draft);
      if (character == '[') return previousPage();
      if (character == ']') return nextPage();
      // Punctuation is never discarded or inserted before pending spelling.
      // A complete first candidate may be committed with it; otherwise keep
      // the exact ASCII spelling. Both pieces must fit before either changes.
      if ((character >= 0x20 && character < 0x7f) || character == '\t')
        return commitWithPunctuation(character, draft);
      return NativeChineseInputResult();
    }
    if (!composing()) return NativeChineseInputResult();
    switch (key.kind) {
      case NativeChatKeyKind::Send:
      case NativeChatKeyKind::Newline:
        // This press only confirms composition; a later Enter may send.
        return candidates_.count ? commitCandidate(0, draft) : commitRaw(draft);
      case NativeChatKeyKind::Backspace:
        preedit_[--length_] = 0; refresh(); ++revision_;
        return NativeChineseInputResult(true);
      case NativeChatKeyKind::Delete:
        // Composition has its cursor at the end, so forward Delete is a no-op.
        // It must not delete text from the underlying draft instead.
        setFeedback(NativeChineseInputFeedback::None);
        return NativeChineseInputResult(true);
      case NativeChatKeyKind::Left: return previousPage();
      case NativeChatKeyKind::Right: return nextPage();
      case NativeChatKeyKind::Home:
      case NativeChatKeyKind::End: {
        // Resolve spelling before handing cursor movement to the draft editor.
        const NativeChineseInputResult result = commitRaw(draft);
        return result.edit == NativeChatEditResult::Ok ? NativeChineseInputResult() : result;
      }
      default: return NativeChineseInputResult(); // History controls retain composition.
    }
  }

 private:
  static bool isLetter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
  }
  void setFeedback(NativeChineseInputFeedback value) {
    if (feedback_ != value) { feedback_ = value; ++revision_; }
  }
  void refresh(size_t page = 0) {
    if (composing()) NativePinyinDictionary::lookup(preedit_, page, candidates_);
    else candidates_ = NativePinyinPage{};
    setFeedback(NativeChineseInputFeedback::None);
  }
  NativeChineseInputResult append(char character) {
    if (length_ >= kNativePinyinMaxInputBytes) {
      setFeedback(NativeChineseInputFeedback::PreeditFull);
      return NativeChineseInputResult(true, NativeChatEditResult::Full);
    }
    preedit_[length_++] = character; preedit_[length_] = 0;
    refresh(); ++revision_;
    return NativeChineseInputResult(true);
  }
  NativeChineseInputResult noCandidate() {
    setFeedback(NativeChineseInputFeedback::NoCandidate);
    return NativeChineseInputResult(true);
  }
  NativeChineseInputResult fullDraft() {
    setFeedback(NativeChineseInputFeedback::DraftFull);
    return NativeChineseInputResult(true, NativeChatEditResult::Full);
  }
  NativeChineseInputResult invalidCandidate() {
    setFeedback(NativeChineseInputFeedback::InvalidCandidate);
    return NativeChineseInputResult(true, NativeChatEditResult::InvalidUtf8);
  }
  bool validCandidate(const NativePinyinCandidate& candidate, size_t& textBytes) const {
    textBytes = 0;
    while (textBytes < sizeof(candidate.text) && candidate.text[textBytes]) ++textBytes;
    return textBytes && textBytes < sizeof(candidate.text) && candidate.consumed_bytes &&
        candidate.consumed_bytes <= length_ && native_chat::validUtf8(candidate.text, textBytes);
  }
  void consume(size_t bytes) {
    std::memmove(preedit_, preedit_ + bytes, length_ - bytes + 1);
    length_ -= static_cast<uint8_t>(bytes); refresh(); ++revision_;
  }
  NativeChineseInputResult commitCandidate(size_t index, NativeChatInput& draft) {
    if (index >= candidates_.count || index >= kNativePinyinPageSize) return noCandidate();
    const NativePinyinCandidate& candidate = candidates_.candidates[index];
    size_t textBytes = 0;
    if (!validCandidate(candidate, textBytes)) return invalidCandidate();
    if (textBytes > kNativeChatDraftBytes - draft.size()) return fullDraft();
    // NativeChatInput::insert can truncate in general. The preflight above
    // makes this insertion atomic, including at a multibyte UTF-8 boundary.
    const NativeChatEditResult result = draft.insert(candidate.text, textBytes);
    if (result != NativeChatEditResult::Ok) return NativeChineseInputResult(true, result);
    const size_t consumed = candidate.consumed_bytes;
    consume(consumed);
    return NativeChineseInputResult(true);
  }
  NativeChineseInputResult commitRaw(NativeChatInput& draft) {
    if (length_ > kNativeChatDraftBytes - draft.size()) return fullDraft();
    const NativeChatEditResult result = draft.insert(preedit_, length_);
    if (result == NativeChatEditResult::Ok) consume(length_);
    return NativeChineseInputResult(true, result);
  }
  NativeChineseInputResult commitWithPunctuation(char character, NativeChatInput& draft) {
    const char* text = preedit_;
    size_t bytes = length_;
    if (candidates_.count && candidates_.count <= kNativePinyinPageSize) {
      const NativePinyinCandidate& candidate = candidates_.candidates[0];
      size_t candidateBytes = 0;
      if (validCandidate(candidate, candidateBytes) && candidate.consumed_bytes == length_) {
        text = candidate.text; bytes = candidateBytes;
      }
    }
    if (bytes + 1 > kNativeChatDraftBytes - draft.size()) return fullDraft();
    char committed[kNativePinyinMaxInputBytes + 2]{};
    std::memcpy(committed, text, bytes); committed[bytes] = character;
    const NativeChatEditResult result = draft.insert(committed, bytes + 1);
    if (result == NativeChatEditResult::Ok) consume(length_);
    return NativeChineseInputResult(true, result);
  }
  NativeChineseInputResult previousPage() {
    if (!candidates_.page) {
      setFeedback(NativeChineseInputFeedback::NoPreviousPage);
      return NativeChineseInputResult(true);
    }
    refresh(candidates_.page - 1); ++revision_;
    return NativeChineseInputResult(true);
  }
  NativeChineseInputResult nextPage() {
    if (!candidates_.has_next) {
      setFeedback(NativeChineseInputFeedback::NoNextPage);
      return NativeChineseInputResult(true);
    }
    refresh(candidates_.page + 1); ++revision_;
    return NativeChineseInputResult(true);
  }

  char preedit_[kNativePinyinMaxInputBytes + 1]{};
  NativePinyinPage candidates_{};
  uint32_t revision_ = 0;
  uint8_t length_ = 0;
  bool enabled_ = true;
  NativeChineseInputFeedback feedback_ = NativeChineseInputFeedback::None;
};

static_assert(sizeof(NativeChineseInput) <= 384, "Chinese input exceeds per-context memory budget");
