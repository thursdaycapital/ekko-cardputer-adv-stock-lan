#pragma once
#include "DeviceGesture.h"
#include <array>
#include <cstddef>
#include <cstdint>

// One owner classifies every physical scan, including scans performed inside
// bounded audio/network waits. The outer loop only consumes semantic events.
// No UI, network, microphone or session state belongs in this mailbox.
class DeviceGestureMailbox {
 public:
  static constexpr size_t kCapacity = 8;

  void update(uint32_t now, bool bootDown, bool voiceChordDown) {
    const uint8_t events = gesture_.update(now, bootDown, voiceChordDown);
    if (events & DeviceGesture::ShortBack) enqueue(DeviceGesture::ShortBack);
    if (events & DeviceGesture::VoiceStart) enqueue(DeviceGesture::VoiceStart);
    if (events & DeviceGesture::VoiceRelease) enqueue(DeviceGesture::VoiceRelease);
  }

  uint8_t pop() {
    if (!count_) return DeviceGesture::None;
    const uint8_t event = queue_[0].event;
    if (queue_[0].repeats > 1) { --queue_[0].repeats; return event; }
    for (size_t i = 0; i + 1 < count_; ++i) queue_[i] = queue_[i + 1];
    --count_;
    return event;
  }

  void suppressUntilReleased() {
    count_ = 0;
    gesture_.suppressUntilReleased();
  }

  bool pending() const { return count_ != 0; }
  size_t queuedEntries() const { return count_; }

 private:
  struct Entry {
    uint64_t repeats = 0;
    uint8_t event = DeviceGesture::None;
  };

  void enqueue(uint8_t event) {
    if (event == DeviceGesture::ShortBack && count_ && queue_[count_ - 1].event == event) {
      ++queue_[count_ - 1].repeats;
      return;
    }
    if (count_ == queue_.size()) {
      // A slow consumer may miss complete voice gestures. Drop only pending
      // voice edges, never reconstruct Start from a still-held raw key, and
      // preserve the exact number of Back events in one bounded counter.
      uint64_t backs = 0;
      for (size_t i = 0; i < count_; ++i)
        if (queue_[i].event == DeviceGesture::ShortBack) backs += queue_[i].repeats;
      count_ = 0;
      if (backs) {
        queue_[count_].event = DeviceGesture::ShortBack;
        queue_[count_++].repeats = backs;
      }
      if (event == DeviceGesture::ShortBack && count_) {
        ++queue_[0].repeats;
        return;
      }
    }
    queue_[count_].event = event;
    queue_[count_++].repeats = 1;
  }

  DeviceGesture gesture_;
  std::array<Entry, kCapacity> queue_{};
  size_t count_ = 0;
};
