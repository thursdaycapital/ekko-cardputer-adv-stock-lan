#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

constexpr size_t kAudioStreamSlotCount = 3;
constexpr size_t kAudioStreamSlotBytes = 2048;
constexpr size_t kAudioStreamSlotSamples = kAudioStreamSlotBytes / sizeof(int16_t);

static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_CHAR_LOCK_FREE == 2,
              "Audio release callback requires lock-free integer atomics");

struct AudioStreamClaim {
  int16_t* data = nullptr;
  size_t capacity_bytes = 0;
  uint32_t generation = 0;
  uint8_t slot = 0xFF;
  bool valid() const { return data != nullptr; }
};

struct AudioStreamQueueStats {
  uint32_t queued = 0;
  uint32_t released = 0;
  uint32_t claims = 0;
  uint32_t claim_failures = 0;
  uint32_t cancelled_unqueued = 0;
  uint32_t abandoned = 0;
  uint32_t rejected_releases = 0;
  uint32_t discarded_after_join = 0;
  uint32_t resets = 0;
  uint32_t in_flight = 0;
  uint32_t claimed = 0;
  uint32_t free_slots = 0;
  uint32_t pending_bytes = 0;
  uint32_t buffer_bytes = kAudioStreamSlotCount * kAudioStreamSlotBytes;
};

// Copy caller PCM into a claimed slot, submit BEFORE calling playRaw(), then
// queue the exact slot-start pointer on one SDK virtual channel. A successful
// playRaw may release the buffer before returning. If playRaw returns false,
// cancelUnqueued() rolls the provisional queued count back; it never owned data.
//
// Only releaseExactPointer() or resetAfterJoin() reclaims a submitted slot.
// An isPlaying() bit is not a buffer ownership fence. The SDK's callback must
// occur once per accepted request; a pointer-only callback cannot distinguish
// an erroneous old duplicate after that same slot has been submitted again.
class AudioStreamQueue {
 public:
  AudioStreamQueue() = default;
  AudioStreamQueue(const AudioStreamQueue&) = delete;
  AudioStreamQueue& operator=(const AudioStreamQueue&) = delete;

  bool tryClaim(AudioStreamClaim& result) {
    // A submitted handle belongs to the SDK now; reusing the output variable
    // may claim a different free slot. Only an unsubmitted writer is pinned.
    if (matches(result) && slots_[result.slot].state.load(std::memory_order_acquire) == Claimed) {
      claim_failures_.fetch_add(1, std::memory_order_relaxed); return false;
    }
    for (size_t i = 0; i < slots_.size(); ++i) {
      uint8_t expected = Free;
      if (!slots_[i].state.compare_exchange_strong(expected, Claimed,
          std::memory_order_acquire, std::memory_order_relaxed)) continue;
      result.data = slots_[i].samples;
      result.capacity_bytes = kAudioStreamSlotBytes;
      result.generation = slots_[i].generation.fetch_add(1, std::memory_order_relaxed) + 1;
      result.slot = static_cast<uint8_t>(i);
      claims_.fetch_add(1, std::memory_order_relaxed);
      return true;
    }
    claim_failures_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  bool submit(const AudioStreamClaim& claim, size_t bytes) {
    // The adapter consumes signed stereo int16 frames. Never publish a partial
    // stereo frame or a buffer larger than the fixed storage.
    if (!matches(claim) || !bytes || bytes > kAudioStreamSlotBytes || bytes % 4) return false;
    Slot& slot = slots_[claim.slot];
    if (slot.state.load(std::memory_order_acquire) != Claimed) return false;
    slot.bytes.store(static_cast<uint16_t>(bytes), std::memory_order_relaxed);
    uint8_t expected = Claimed;
    if (!slot.state.compare_exchange_strong(expected, Submitted,
        std::memory_order_release, std::memory_order_relaxed)) return false;
    queued_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Only call after the SDK rejected this request (playRaw == false). Do not
  // cancel an accepted request: stop/end follow the release/join paths below.
  bool cancelUnqueued(const AudioStreamClaim& claim) {
    if (!transitionToFree(claim, Submitted)) return false;
    queued_.fetch_sub(1, std::memory_order_relaxed);
    cancelled_unqueued_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  bool abandon(const AudioStreamClaim& claim) {
    if (!transitionToFree(claim, Claimed)) return false;
    abandoned_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  // Safe in the SDK playback-task callback: at most 3 pointer comparisons,
  // one lock-free CAS and counters. No blocking, allocation, SDK or I/O calls.
  bool releaseExactPointer(const void* data) {
    for (size_t i = 0; i < slots_.size(); ++i) {
      if (data != slots_[i].samples) continue;
      uint8_t expected = Submitted;
      if (slots_[i].state.compare_exchange_strong(expected, Free,
          std::memory_order_acq_rel, std::memory_order_relaxed)) {
        released_.fetch_add(1, std::memory_order_relaxed); return true;
      }
      break;
    }
    rejected_releases_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  // PRECONDITION: producer is quiescent and Speaker.end() has returned after
  // joining its playback task. It drops queued requests without callbacks.
  // Never call on stop() or merely when isPlaying()==false. Counters persist.
  void resetAfterJoin() {
    uint32_t discarded = 0, abandoned = 0;
    for (size_t i = 0; i < slots_.size(); ++i) {
      const uint8_t previous = slots_[i].state.exchange(Free, std::memory_order_relaxed);
      if (previous == Submitted) ++discarded;
      if (previous == Claimed) ++abandoned;
      slots_[i].generation.fetch_add(1, std::memory_order_relaxed);
      slots_[i].bytes.store(0, std::memory_order_relaxed);
    }
    discarded_after_join_.fetch_add(discarded, std::memory_order_relaxed);
    abandoned_.fetch_add(abandoned, std::memory_order_relaxed);
    resets_.fetch_add(1, std::memory_order_relaxed);
  }

  AudioStreamQueueStats stats() const {
    AudioStreamQueueStats result;
    result.queued = queued_.load(std::memory_order_relaxed);
    result.released = released_.load(std::memory_order_relaxed);
    result.claims = claims_.load(std::memory_order_relaxed);
    result.claim_failures = claim_failures_.load(std::memory_order_relaxed);
    result.cancelled_unqueued = cancelled_unqueued_.load(std::memory_order_relaxed);
    result.abandoned = abandoned_.load(std::memory_order_relaxed);
    result.rejected_releases = rejected_releases_.load(std::memory_order_relaxed);
    result.discarded_after_join = discarded_after_join_.load(std::memory_order_relaxed);
    result.resets = resets_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < slots_.size(); ++i) {
      const uint8_t state = slots_[i].state.load(std::memory_order_acquire);
      if (state == Free) ++result.free_slots;
      else if (state == Claimed) ++result.claimed;
      else {
        ++result.in_flight;
        result.pending_bytes += slots_[i].bytes.load(std::memory_order_relaxed);
      }
    }
    // This is a diagnostic snapshot, not a transaction across all atomics.
    return result;
  }

 private:
  static constexpr uint8_t Free = 0, Claimed = 1, Submitted = 2;
  struct Slot {
    alignas(4) int16_t samples[kAudioStreamSlotSamples]{};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint16_t> bytes{0};
    std::atomic<uint8_t> state{Free};
  };
  bool matches(const AudioStreamClaim& claim) const {
    return claim.slot < slots_.size() && claim.data == slots_[claim.slot].samples &&
        claim.generation == slots_[claim.slot].generation.load(std::memory_order_relaxed);
  }
  bool transitionToFree(const AudioStreamClaim& claim, uint8_t from) {
    if (!matches(claim)) return false;
    return slots_[claim.slot].state.compare_exchange_strong(from, Free,
        std::memory_order_acq_rel, std::memory_order_relaxed);
  }
  std::array<Slot, kAudioStreamSlotCount> slots_{};
  std::atomic<uint32_t> queued_{0}, released_{0}, claims_{0}, claim_failures_{0};
  std::atomic<uint32_t> cancelled_unqueued_{0}, abandoned_{0}, rejected_releases_{0};
  std::atomic<uint32_t> discarded_after_join_{0}, resets_{0};
};
