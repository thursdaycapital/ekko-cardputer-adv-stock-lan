#pragma once
#include <cstdint>

// Physical input only. No page, transport, microphone or session side effects.
// In particular a short G0 press is never a voice cancellation/new-session
// command, and holding both voice shortcuts produces a single recording.
class DeviceGesture {
 public:
  enum Event : uint8_t { None = 0, ShortBack = 1, VoiceStart = 2, VoiceRelease = 4 };
  static constexpr uint32_t kDebounceMs = 30;
  static constexpr uint32_t kLongPressMs = 360;

  uint8_t update(uint32_t now, bool bootDown, bool voiceChordDown) {
    const bool bootChanged = boot_.update(now, bootDown);
    chord_.update(now, voiceChordDown);
    if (!armed_) {
      if (bootDown || voiceChordDown || boot_.down || chord_.down) {
        releaseTracked_ = false;
      } else if (!releaseTracked_) {
        releaseTracked_ = true;
        releasedAt_ = now;
      } else if (uint32_t(now - releasedAt_) >= kDebounceMs) {
        armed_ = true;
        bootConsumed_ = false;
      }
      return None;
    }
    if (bootChanged && boot_.down) bootPressedAt_ = now;

    if (voice_) {
      if (boot_.down) bootConsumed_ = true;
      if (!boot_.down && !chord_.down) {
        voice_ = false;
        return VoiceRelease;
      }
      return None;
    }
    if (chord_.down || (boot_.down && bootDown && !bootConsumed_ &&
                       uint32_t(now - bootPressedAt_) >= kLongPressMs)) {
      voice_ = true;
      bootConsumed_ = boot_.down;
      return VoiceStart;
    }
    if (bootChanged && !boot_.down) {
      const bool back = !bootConsumed_;
      bootConsumed_ = false;
      return back ? ShortBack : None;
    }
    return None;
  }

  bool activeVoice() const { return voice_; }
  bool armed() const { return armed_; }

  // The five-second cap (or a page change) ends the current action. A held
  // shortcut must be fully released before it can start another recording.
  void suppressUntilReleased() {
    armed_ = voice_ = releaseTracked_ = false;
    bootConsumed_ = true;
  }

 private:
  struct Key {
    bool down = false, candidate = false;
    uint32_t changedAt = 0;
    bool update(uint32_t now, bool raw) {
      if (candidate != raw) { candidate = raw; changedAt = now; }
      if (candidate == down || uint32_t(now - changedAt) < kDebounceMs) return false;
      down = candidate;
      return true;
    }
  };
  Key boot_, chord_;
  bool armed_ = false, voice_ = false, bootConsumed_ = false, releaseTracked_ = false;
  uint32_t bootPressedAt_ = 0, releasedAt_ = 0;
};
