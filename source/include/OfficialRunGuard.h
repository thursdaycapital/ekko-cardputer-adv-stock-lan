#pragma once

#include <array>
#include <cstddef>
#include <cstring>

// Correlation only, not a chat reducer. The caller validates the immutable
// account/machine/profile/session/connection epoch before calling this guard.
// It never emits, retries, clears a draft, or interprets transcript text.
namespace official_run_guard {

enum class Decision { Accept, Reject, Reconcile };

class Guard {
 public:
  static constexpr size_t kPendingQueueBytes = 64;
  static constexpr size_t kActiveQueueBytes = 128;
  static constexpr size_t kRunBytes = 128;

  bool startPending(const char* queue) {
    if (hasPending() || hasActive() || !identifier(queue, kPendingQueueBytes, false)) return false;
    copy(pendingQueue_, queue);
    // Starting a NEW explicitly chosen request consumes the prior result.
    // The caller must preserve any old uncertain draft before doing this.
    unconfirmed_ = false;
    return true;
  }

  Decision acceptStarted(const char* run, const char* queue = nullptr) {
    if (!identifier(run, kRunBytes, true) || !identifier(queue, kActiveQueueBytes, true)) return Decision::Reject;
    const bool explicitQueue = present(queue);
    if (hasPending() && explicitQueue && !same(queue, pendingQueue_.data())) return Decision::Reject;
    if (hasActive() && explicitQueue && !same(queue, activeQueue_.data())) return Decision::Reject;
    if (!present(run)) return Decision::Reconcile;
    if (hasActive()) {
      if (!same(run, activeRun_.data())) return Decision::Reject;
      // An idempotent duplicate may strengthen a previously uncorrelated
      // coding start, but only with this locally owned queue identity.
      if (activeOwned_ && explicitQueue) unconfirmed_ = false;
      return Decision::Accept;
    }
    activeOwned_ = hasPending();
    copy(activeRun_, run);
    if (hasPending()) {
      copy(activeQueue_, pendingQueue_.data());
      pendingQueue_[0] = 0;
      // Official coding events omit queue_id: observing their run is NOT an
      // acknowledgement that our particular pending input caused that run.
      unconfirmed_ = !explicitQueue;
    } else if (explicitQueue) {
      copy(activeQueue_, queue);  // Observed server work, not a local send ACK.
    }
    return Decision::Accept;
  }

  Decision acceptTerminal(const char* run = nullptr, const char* queue = nullptr) {
    if (!identifier(run, kRunBytes, true) || !identifier(queue, kActiveQueueBytes, true)) return Decision::Reject;
    const bool explicitRun = present(run), explicitQueue = present(queue);
    if (explicitQueue && !matchesCurrentQueue(queue)) return Decision::Reject;
    if (explicitRun && hasActive() && !same(run, activeRun_.data())) return Decision::Reject;
    if (!explicitRun && !explicitQueue) return Decision::Reconcile;
    // A run-only failure cannot identify a still-pending request whose start
    // was never observed. Preserve it and ask the caller to resume/reload.
    if (!hasActive() && (!hasPending() || !explicitQueue)) return Decision::Reconcile;

    if (explicitQueue && (hasPending() || activeOwned_)) unconfirmed_ = false;
    // A run-only terminal can end the observed run, but cannot retroactively
    // prove ownership of a coding start which omitted queue_id.
    clearAssociations();
    return Decision::Accept;
  }

  Decision acceptAbort(const char* run = nullptr, const char* queue = nullptr) {
    if (!identifier(run, kRunBytes, true) || !identifier(queue, kActiveQueueBytes, true)) return Decision::Reject;
    if (present(queue) && !matchesCurrentQueue(queue)) return Decision::Reject;
    // Official abort.completed can carry the managed process ID, while
    // run.started carried a response ID. Missing/different IDs are not proof
    // of staleness OR proof of completion; resume must confirm isWorking.
    if (!present(run) || !hasActive() || !same(run, activeRun_.data())) return Decision::Reconcile;
    return acceptTerminal(run, queue);
  }

  // No association survives a transport boundary. Return AND retain whether
  // a local send lacked a request-correlated ACK; the caller owns its draft
  // and must not automatically replay it. An unrelated server run is not a
  // local uncertain send.
  bool disconnect() {
    const bool had = hadUnconfirmed();
    clearAssociations();
    unconfirmed_ = had;
    return had;
  }

  bool hasPending() const { return pendingQueue_[0] != 0; }
  bool hasActive() const { return activeRun_[0] != 0; }
  bool hadUnconfirmed() const { return hasPending() || unconfirmed_; }
  const char* pendingQueue() const { return pendingQueue_.data(); }
  const char* activeQueue() const { return activeQueue_.data(); }
  const char* activeRun() const { return activeRun_.data(); }

 private:
  std::array<char, kPendingQueueBytes + 1> pendingQueue_{};
  std::array<char, kActiveQueueBytes + 1> activeQueue_{};
  std::array<char, kRunBytes + 1> activeRun_{};
  bool activeOwned_ = false, unconfirmed_ = false;

  static bool present(const char* value) { return value && *value; }
  static bool same(const char* left, const char* right) {
    return present(left) && present(right) && std::strcmp(left, right) == 0;
  }
  static bool identifier(const char* value, size_t maximum, bool optional) {
    if (!present(value)) return optional;
    for (size_t i = 0; i <= maximum; ++i) {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      if (!c) return i != 0;
      if (c < 0x20 || c == 0x7f) return false;
    }
    return false;  // Reject whole identifiers; never compare truncated prefixes.
  }
  template<size_t N> static void copy(std::array<char, N>& target, const char* value) {
    const size_t bytes = std::strlen(value);  // Validated before every call.
    std::memcpy(target.data(), value, bytes + 1);
  }
  bool matchesCurrentQueue(const char* queue) const {
    return same(queue, pendingQueue_.data()) || same(queue, activeQueue_.data());
  }
  void clearAssociations() {
    pendingQueue_[0] = activeQueue_[0] = activeRun_[0] = 0;
    activeOwned_ = false;
  }
};

}  // namespace official_run_guard
