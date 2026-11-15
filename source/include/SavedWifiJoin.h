#pragma once
#include <cstddef>
#include <cstdint>

namespace native_wifi {
// Saved credentials stay in the production owner. This policy issues one
// attempt at a time; it never scans, clears settings, sleeps or restarts.
class SavedWifiJoin {
 public:
  enum class Result : uint8_t { Idle, Waiting, Attempt, Connected, Exhausted };
  void begin(size_t count, uint32_t now) {
    count_ = count; index_ = 0; started_ = now; attemptPending_ = count != 0;
    active_ = true;
  }
  Result poll(uint32_t now, bool connected) {
    if (!active_) return Result::Idle;
    // Check connection only after an actual attempt: a stale SDK CONNECTED
    // from the network being replaced must not claim the new candidate.
    if (attemptPending_) {
      attemptPending_ = false; started_ = now; return Result::Attempt;
    }
    if (!count_) { active_ = false; return Result::Exhausted; }
    if (connected) { active_ = false; return Result::Connected; }
    if (uint32_t(now - started_) < kAttemptMs) return Result::Waiting;
    if (++index_ >= count_) { active_ = false; return Result::Exhausted; }
    started_ = now; return Result::Attempt;
  }
  void cancel() { active_ = attemptPending_ = false; }
  bool active() const { return active_; }
  size_t index() const { return index_; }
  static constexpr uint32_t kAttemptMs = 18000;
 private:
  size_t count_ = 0, index_ = 0;
  uint32_t started_ = 0;
  bool active_ = false, attemptPending_ = false;
};
}  // namespace native_wifi
