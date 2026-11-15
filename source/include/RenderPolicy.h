#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

constexpr uint32_t kNativeFrameWidth = 240;
constexpr uint32_t kNativeFrameHeight = 135;
constexpr uint32_t kNativeFramePixelBytes = kNativeFrameWidth * kNativeFrameHeight * 2;
// Pinned M5GFX allocates pixel bytes + one 16-bit tail pixel, aligned to 4.
constexpr uint32_t kNativeFrameAllocationBytes = (kNativeFramePixelBytes + 2 + 3) & ~uint32_t(3);
constexpr uint32_t kNativeRenderHeapReserve = 96 * 1024;
constexpr uint32_t kNativeDynamicFrameIntervalMs = 80;
constexpr size_t kNativeMonochromeBytes = 128 * 64 / 8;

inline bool nativeSpriteMemoryAvailable(size_t internalFree, size_t largestDma) {
  return largestDma >= kNativeFrameAllocationBytes &&
      internalFree >= kNativeFrameAllocationBytes &&
      internalFree - kNativeFrameAllocationBytes >= kNativeRenderHeapReserve;
}

struct NativeRenderInput {
  bool conversation = false;
  bool interaction = false;
  bool online = false;
  size_t active = 0;
  size_t browse = 0;
  const char* message = "";
  const char* title = "";
  const char* hint = "";
  const char* address = "";
  const char* mode_label = "";
  const char* error = "";
  const uint8_t* monochrome = nullptr;
  uint32_t chat_revision = 0, editor_revision = 0;
  size_t chat_scroll = 0;
  const char* chat_status = "";
  const char* chat_notice = "";
  bool chat_history_mode = false;
  uint32_t input_method_revision = 0;
  bool recording = false;
  bool send_uncertain = false, send_reviewable = false;
  uint32_t recording_elapsed_tenths = 0, recording_limit_ms = 5000;
};

enum class NativeRenderDecision { Draw, SameState, Throttled };

// Compare exact inputs, not a hash. In particular, the obsolete OLED eye blink
// is irrelevant on the carousel and must never cause a physical LCD update.
class NativeRenderPolicy {
 public:
  NativeRenderDecision consider(const NativeRenderInput& input, uint32_t now) const {
    if (!hasFrame_) return NativeRenderDecision::Draw;
    const bool sameControls = conversation_ == input.conversation &&
        interaction_ == input.interaction && online_ == input.online &&
        active_ == input.active && browse_ == input.browse &&
        message_ == text(input.message) && title_ == text(input.title) &&
        hint_ == text(input.hint) && address_ == text(input.address) && error_ == text(input.error) &&
        modeLabel_ == text(input.mode_label) &&
        editorRevision_ == input.editor_revision && chatScroll_ == input.chat_scroll &&
        chatStatus_ == text(input.chat_status) && chatNotice_ == text(input.chat_notice) &&
        chatHistoryMode_ == input.chat_history_mode && inputMethodRevision_ == input.input_method_revision &&
        recording_ == input.recording && sendUncertain_ == input.send_uncertain &&
        sendReviewable_ == input.send_reviewable && recordingElapsedTenths_ == input.recording_elapsed_tenths &&
        recordingLimitMs_ == input.recording_limit_ms;
    bool sameBitmap = true;
    if (input.interaction) {
      sameBitmap = bitmapPresent_ == (input.monochrome != nullptr) &&
          (!bitmapPresent_ || std::memcmp(bitmap_.data(), input.monochrome, bitmap_.size()) == 0);
    }
    if (sameControls && sameBitmap && chatRevision_ == input.chat_revision) return NativeRenderDecision::SameState;
    // Control/error/navigation transitions remain immediate. Only changing
    // interaction pixels are rate-limited; commit() happens after LCD DMA ends.
    if (sameControls && uint32_t(now - lastFrameAtMs_) < kNativeDynamicFrameIntervalMs)
      return NativeRenderDecision::Throttled;
    return NativeRenderDecision::Draw;
  }

  void commit(const NativeRenderInput& input, uint32_t now) {
    conversation_ = input.conversation;
    interaction_ = input.interaction; online_ = input.online;
    active_ = input.active; browse_ = input.browse;
    message_ = text(input.message); title_ = text(input.title);
    hint_ = text(input.hint); address_ = text(input.address); error_ = text(input.error);
    modeLabel_ = text(input.mode_label);
    chatRevision_ = input.chat_revision; editorRevision_ = input.editor_revision;
    chatScroll_ = input.chat_scroll; chatStatus_ = text(input.chat_status); chatNotice_ = text(input.chat_notice);
    chatHistoryMode_ = input.chat_history_mode;
    inputMethodRevision_ = input.input_method_revision;
    recording_ = input.recording; recordingElapsedTenths_ = input.recording_elapsed_tenths;
    sendUncertain_ = input.send_uncertain; sendReviewable_ = input.send_reviewable;
    recordingLimitMs_ = input.recording_limit_ms;
    bitmapPresent_ = input.interaction && input.monochrome;
    if (bitmapPresent_) std::memcpy(bitmap_.data(), input.monochrome, bitmap_.size());
    lastFrameAtMs_ = now; hasFrame_ = true;
  }

 private:
  static const char* text(const char* value) { return value ? value : ""; }
  bool hasFrame_ = false, conversation_ = false, interaction_ = false, online_ = false, bitmapPresent_ = false;
  size_t active_ = 0, browse_ = 0;
  uint32_t lastFrameAtMs_ = 0;
  uint32_t chatRevision_ = 0, editorRevision_ = 0;
  size_t chatScroll_ = 0;
  bool chatHistoryMode_ = false;
  uint32_t inputMethodRevision_ = 0;
  bool recording_ = false;
  bool sendUncertain_ = false, sendReviewable_ = false;
  uint32_t recordingElapsedTenths_ = 0, recordingLimitMs_ = 5000;
  std::string message_, title_, hint_, address_, error_;
  std::string modeLabel_;
  std::string chatStatus_, chatNotice_;
  std::array<uint8_t, kNativeMonochromeBytes> bitmap_{};
};
