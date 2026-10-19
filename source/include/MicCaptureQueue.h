#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

// One SDK microphone task produces completed stereo buffers; the application
// task consumes them. Construct/allocate only for a recording, not on its stack.
namespace mic_capture {
constexpr size_t kSlots = 8;
constexpr size_t kStereoFrames = 1024;
constexpr size_t kSamples = kStereoFrames * 2;
constexpr size_t kPcmBytes = kSlots * kSamples * sizeof(int16_t);
static_assert(sizeof(unsigned int) == sizeof(uint32_t) && ATOMIC_INT_LOCK_FREE == 2,
              "The microphone callback requires lock-free 32-bit atomics");

enum class Error : uint32_t { None, Overrun, InvalidClaim, InvalidCallback, InvalidRelease, SdkRejected };
struct Claim {
  int16_t* data = nullptr;
  size_t samples = 0;
  explicit operator bool() const { return data != nullptr; }
};
struct Stats {
  uint32_t producedBlocks = 0, consumedBlocks = 0, maxReadyBlocks = 0, overruns = 0;
  uint32_t invalidReleases = 0, invalidCallbacks = 0, invalidClaims = 0;
  uint32_t readyBlocks = 0, borrowedBlocks = 0, queuedBlocks = 0, freeSlots = 0;
  uint32_t resets = 0, discardedAfterJoin = 0;
};

class Queue;
class Lease {
 public:
  Lease() = default;
  ~Lease();
  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;
  Lease(Lease&& other) noexcept;
  Lease& operator=(Lease&& other) noexcept;
  const int16_t* data() const { return data_; }
  size_t samples() const { return owner_ ? kSamples : 0; }
  explicit operator bool() const { return owner_ != nullptr; }
  void reset(); // Main task only; idempotent for an empty/moved lease.
 private:
  Queue* owner_ = nullptr;
  const int16_t* data_ = nullptr;
  uint32_t slot_ = 0;
  friend class Queue;
};

class Queue {
 public:
  Queue() = default;
  Queue(const Queue&) = delete;
  Queue& operator=(const Queue&) = delete;
  // PRECONDITION for destruction/reset: Mic.end() has joined the SDK task,
  // producer calls have ceased, and every main Lease has been released. Idle
  // driver bits and failed()/cancel flags do not establish that join.
  ~Queue() = default;

  // PRODUCER ONLY. The initial main-task claim may be handed to the SDK before
  // it starts; from then on the callback alone claims/completes requests.
  // At most one Queued buffer exists. Pass exactly samples elements to the
  // SDK, after claim() has returned. Do not retry a failed claim: stop/join.
  Claim claim() {
    if (failed() || stopped()) return {};
    if (queued_ != kNoSlot) {
      invalidClaim_.fetch_add(1, std::memory_order_relaxed);
      fail(Error::InvalidClaim); return {};
    }
    Slot& slot = slots_[write_];
    if (slot.state.load(std::memory_order_acquire) != Free) {
      overrun_.fetch_add(1, std::memory_order_relaxed);
      fail(Error::Overrun); return {};
    }
    queued_ = write_;
    slot.state.store(Queued, std::memory_order_release);
    Claim result; result.data = slot.pcm.data(); result.samples = kSamples;
    return result;
  }

  // SDK CALLBACK ONLY. No allocation, locks, wait, UI, filesystem or SDK calls.
  // Partial, duplicate, interior and foreign pointers fail the entire take;
  // callers must not send a successfully captured prefix after any failure.
  // A pointer-only SDK callback cannot identify a stale duplicate once that
  // SAME address has been submitted again. Exactly one callback per accepted
  // SDK request, and join before reuse/reset, are external driver contracts.
  bool completeExactPointer(const int16_t* data, size_t samples) {
    if (failed() || stopped()) return false;
    if (queued_ == kNoSlot || data != slots_[queued_].pcm.data() || samples != kSamples ||
        slots_[queued_].state.load(std::memory_order_acquire) != Queued) {
      invalidCallback_.fetch_add(1, std::memory_order_relaxed);
      fail(Error::InvalidCallback); return false;
    }
    const uint32_t completed = queued_;
    queued_ = kNoSlot;
    write_ = (write_ + 1) % kSlots;
    // Count before publishing Ready: take() may run immediately after the
    // release store and must never decrement an as-yet-unincremented count.
    const uint32_t ready = ready_.fetch_add(1, std::memory_order_relaxed) + 1;
    uint32_t maximum = maxReady_.load(std::memory_order_relaxed);
    if (ready > maximum) maxReady_.store(ready, std::memory_order_relaxed);
    produced_.fetch_add(1, std::memory_order_relaxed);
    slots_[completed].state.store(Ready, std::memory_order_release);
    return true;
  }

  // SDK rejection is terminal too; leave the Queued buffer owned until join.
  void failSdkSubmission() { fail(Error::SdkRejected); }

  // Any task may request a stop. Subsequent claim/complete calls do no work;
  // already completed Ready blocks remain available to main.
  // This flag does NOT join an already executing producer call or the SDK.
  // Caller first disables its callback, then Mic.end() joins it before drain /
  // reset / destruction. A call overlapping requestStop may finish its full
  // buffer; partial stop-time releases must never call completeExactPointer.
  void requestStop() { stop_.store(1, std::memory_order_release); }
  bool stopped() const { return stop_.load(std::memory_order_acquire) != 0; }
  bool hasLease() const { return borrowed_ != kNoSlot; } // Main task only.

  // MAIN ONLY. A second take while holding a Lease returns empty. Failed takes
  // publish no further prefix; an already borrowed Lease may still be released.
  Lease take() {
    Lease result;
    if (failed() || borrowed_ != kNoSlot ||
        slots_[read_].state.load(std::memory_order_acquire) != Ready) return result;
    const uint32_t index = read_;
    slots_[index].state.store(Borrowed, std::memory_order_relaxed);
    ready_.fetch_sub(1, std::memory_order_relaxed);
    borrowed_ = index;
    result.owner_ = this; result.slot_ = index; result.data_ = slots_[index].pcm.data();
    return result;
  }

  // MAIN ONLY. Reject empty/foreign ownership without reclaiming its buffer.
  bool release(Lease& lease) {
    if (lease.owner_ != this || borrowed_ == kNoSlot || lease.slot_ != borrowed_ ||
        lease.data_ != slots_[borrowed_].pcm.data() ||
        slots_[borrowed_].state.load(std::memory_order_relaxed) != Borrowed) {
      invalidRelease_.fetch_add(1, std::memory_order_relaxed);
      fail(Error::InvalidRelease); return false;
    }
    const uint32_t index = borrowed_;
    lease.owner_ = nullptr; lease.data_ = nullptr;
    borrowed_ = kNoSlot;
    read_ = (read_ + 1) % kSlots;
    consumed_.fetch_add(1, std::memory_order_relaxed);
    slots_[index].state.store(Free, std::memory_order_release);
    return true;
  }

  bool failed() const { return error_.load(std::memory_order_acquire) != uint32_t(Error::None); }
  Error error() const { return static_cast<Error>(error_.load(std::memory_order_acquire)); }
  // Diagnostic, non-transactional snapshot. Do not use it instead of claim(),
  // take() or an SDK join to decide buffer ownership. Counters wrap modulo 2^32.
  Stats stats() const {
    Stats result;
    result.producedBlocks = produced_.load(std::memory_order_relaxed);
    result.consumedBlocks = consumed_.load(std::memory_order_relaxed);
    result.maxReadyBlocks = maxReady_.load(std::memory_order_relaxed);
    result.overruns = overrun_.load(std::memory_order_relaxed);
    result.invalidReleases = invalidRelease_.load(std::memory_order_relaxed);
    result.invalidCallbacks = invalidCallback_.load(std::memory_order_relaxed);
    result.invalidClaims = invalidClaim_.load(std::memory_order_relaxed);
    result.resets = resets_.load(std::memory_order_relaxed);
    result.discardedAfterJoin = discarded_.load(std::memory_order_relaxed);
    for (const auto& slot : slots_) {
      switch (slot.state.load(std::memory_order_acquire)) {
        case Free: ++result.freeSlots; break;
        case Queued: ++result.queuedBlocks; break;
        case Ready: ++result.readyBlocks; break;
        case Borrowed: ++result.borrowedBlocks; break;
      }
    }
    return result;
  }

  // MAIN ONLY, with the destructor's join precondition. False preserves an
  // outstanding consumer Lease. Unfinished Queued/Ready data is discarded,
  // never represented as consumed. Cumulative diagnostics survive a reset.
  bool resetAfterJoin() {
    if (borrowed_ != kNoSlot) return false;
    uint32_t discarded = 0;
    for (auto& slot : slots_) {
      if (slot.state.load(std::memory_order_relaxed) != Free) ++discarded;
      slot.state.store(Free, std::memory_order_relaxed);
    }
    read_ = write_ = 0; queued_ = kNoSlot;
    ready_.store(0, std::memory_order_relaxed);
    discarded_.fetch_add(discarded, std::memory_order_relaxed);
    resets_.fetch_add(1, std::memory_order_relaxed);
    stop_.store(0, std::memory_order_relaxed);
    error_.store(uint32_t(Error::None), std::memory_order_release);
    return true;
  }

 private:
  enum : uint32_t { Free, Queued, Ready, Borrowed, kNoSlot = uint32_t(kSlots) };
  struct Slot {
    alignas(4) std::array<int16_t, kSamples> pcm{};
    std::atomic<uint32_t> state{Free};
  };
  void fail(Error reason) {
    uint32_t expected = uint32_t(Error::None);
    error_.compare_exchange_strong(expected, uint32_t(reason), std::memory_order_release,
                                  std::memory_order_relaxed);
  }
  std::array<Slot, kSlots> slots_{};
  // Each plain cursor has exactly one task owner. Only quiescent reset touches
  // both sides; all cross-task ownership and diagnostics use 32-bit atomics.
  uint32_t write_ = 0, queued_ = kNoSlot; // Producer owned.
  uint32_t read_ = 0, borrowed_ = kNoSlot; // Consumer owned.
  std::atomic<uint32_t> error_{uint32_t(Error::None)}, ready_{0}, stop_{0};
  std::atomic<uint32_t> produced_{0}, consumed_{0}, maxReady_{0}, overrun_{0};
  std::atomic<uint32_t> invalidRelease_{0}, invalidCallback_{0}, invalidClaim_{0};
  std::atomic<uint32_t> resets_{0}, discarded_{0};
};

inline Lease::~Lease() { reset(); }
inline Lease::Lease(Lease&& other) noexcept
    : owner_(other.owner_), data_(other.data_), slot_(other.slot_) {
  other.owner_ = nullptr; other.data_ = nullptr;
}
inline Lease& Lease::operator=(Lease&& other) noexcept {
  if (this != &other) {
    reset(); owner_ = other.owner_; data_ = other.data_; slot_ = other.slot_;
    other.owner_ = nullptr; other.data_ = nullptr;
  }
  return *this;
}
inline void Lease::reset() { if (owner_) owner_->release(*this); }
static_assert(kPcmBytes == 32768 && sizeof(Queue) <= kPcmBytes + 512,
              "Microphone queue must keep its fixed 32 KiB PCM budget");
} // namespace mic_capture
