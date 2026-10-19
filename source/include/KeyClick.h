#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace native_key_click {

constexpr size_t kSampleCount = 1920;
constexpr uint32_t kPlaybackRate = 44100;
constexpr uint32_t kRequestLifetimeMs = 80;

// Match the factory's input PCM and original playRaw parameter defaults:
// 960 frames synthesized at 48 kHz with duplicate L/R samples, submitted as
// 1920 mono samples at 44.1 kHz. Explicit stereo/48 kHz playback sounds different.
// SDK resampling, hardware packing and codec startup can still affect output;
// matching this source buffer does not prove identical acoustic playback.
// The caller owns this buffer until the SDK releases it or Speaker.end() joins.
inline void render(uint8_t noteIndex, int16_t* out) {
  if (!out) return;
  constexpr int frequencies[] = {4186, 4698, 5274, 5587, 6271, 7040, 7902};
  constexpr size_t frames = kSampleCount / 2;
  constexpr size_t fadeFrames = 200;
  constexpr double pi = 3.14159265358979323846;
  constexpr float amplitude = 32767.0f / 5;
  const int frequency = frequencies[noteIndex % 7];
  for (size_t i = 0; i < frames; ++i) {
    float amp = amplitude;
    if (i >= frames - fadeFrames)
      amp *= static_cast<float>(frames - i) / fadeFrames;
    const int16_t sample = static_cast<int16_t>(amp * std::sin(2.0 * pi * frequency * i / 48000));
    out[i * 2] = sample;
    out[i * 2 + 1] = sample;
  }
}

struct Request { uint8_t noteIndex = 0; uint32_t atMs = 0; };
struct InputStats { uint32_t pressed = 0, suppressed = 0; };

class Input {
 public:
  // Sample physical keys, including modifiers, rather than repeated UI events.
  // All calls advance the held state, even when audio ownership blocks sound.
  template<class Keys, class Random>
  void update(const Keys& physical, uint32_t now, Random random, bool accept = true) {
    if (!accept) discard();
    for (size_t i = 0; i < held_.size(); ++i) {
      const bool down = i < physical.size() && physical[i].down;
      if (primed_ && down && !held_[i]) {
        ++stats_.pressed;
        if (!accept) ++stats_.suppressed;
        else {
          const uint8_t base = physical[i].base;
          const uint8_t note = base >= '1' && base <= '7'
              ? static_cast<uint8_t>(base - '1') : static_cast<uint8_t>(random() % 7);
          discard(); // At most the newest physical press may wait for audio.
          pending_.noteIndex = note;
          pending_.atMs = now;
          hasPending_ = true;
        }
      }
      held_[i] = down;
    }
    primed_ = true;
  }

  bool take(uint32_t now, bool allowed, Request& out) {
    if (!hasPending_) return false;
    if (!allowed || static_cast<uint32_t>(now - pending_.atMs) > kRequestLifetimeMs) {
      discard();
      return false;
    }
    out = pending_;
    hasPending_ = false;
    return true;
  }

  void discard() {
    if (hasPending_) ++stats_.suppressed;
    hasPending_ = false;
  }

  InputStats stats() const { return stats_; }

 private:
  std::array<bool, 56> held_{};
  Request pending_{};
  InputStats stats_{};
  bool primed_ = false, hasPending_ = false;
};

} // namespace native_key_click
