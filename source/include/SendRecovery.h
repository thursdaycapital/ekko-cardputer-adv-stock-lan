#pragma once

#include <cstdint>
#include <limits>

// A local user's explicit review of an UNKNOWN send, not delivery tracking.
// No method sends/retries a message, clears a draft, or proves backend success.
// The main-loop owner feeds only authenticated, fully validated observations.
namespace send_recovery {

using Epoch = std::uint32_t;
using Ticket = std::uint64_t;
enum class Phase : std::uint8_t { Uncertain, Synchronizing, Reviewable, Clear };

class Recovery {
 public:
  Phase phase() const { return phase_; }
  Epoch epoch() const { return epoch_; }
  Ticket reviewTicket() const { return review_; }
  bool clear() const { return phase_==Phase::Clear; }

  // Call on a new ambiguous-send event, not on every tick of an unknown flag.
  // Zero epoch is safely uncertain/unreviewable until a valid scope arrives.
  void markUnknown(Epoch epoch) { epoch_=epoch; invalidate(Phase::Uncertain); }
  // Normal scope/transport changes never silently settle an unknown send.
  void scopeChanged(Epoch epoch) {
    if(epoch==epoch_)return;
    epoch_=epoch;invalidate(clear()?Phase::Clear:Phase::Uncertain);
  }
  // Pin this returned ticket to each ACTUAL history request / resume emission.
  // A second refresh resets both proofs. Serialize overlapping resume requests
  // if the official wire event cannot correlate their replies; never re-label
  // an old outstanding response with the newest ticket at receive time.
  Ticket requestReview(Epoch epoch) {
    if(!epoch||epoch!=epoch_||clear())return 0;
    invalidate(Phase::Uncertain);
    if(sequence_==(std::numeric_limits<Ticket>::max)())return 0;
    review_=++sequence_;phase_=Phase::Synchronizing;return review_;
  }
  bool observeHistory(Epoch epoch,Ticket ticket,bool latestPage) {
    if(!matches(epoch,ticket)||!latestPage)return false;
    historySeen_=true;update();return true;
  }
  bool observeResume(Epoch epoch,Ticket ticket,bool working) {
    if(!matches(epoch,ticket))return false;
    idleSeen_=!working;
    // A running observation invalidates the earlier idle/history proof; the
    // run may still append messages. Wait for fresh history and idle again.
    if(working)historySeen_=false;
    update();return true;
  }
  // Only an explicit user confirmation after BOTH proofs can unlock sending.
  // This consumes the review; it is not an ACK of the unknown server request.
  bool acknowledge(Epoch epoch) {
    if(!epoch||epoch!=epoch_||phase_!=Phase::Reviewable)return false;
    invalidate(Phase::Clear);return true;
  }
  // Only the protocol adapter may report a positively correlated terminal
  // outcome / authoritative persisted result in this CURRENT scope. A normal
  // page load, transport reconnect or UI observation is not a settled result.
  void observeSettled(Epoch epoch) {
    if(epoch&&epoch==epoch_)invalidate(Phase::Clear);
  }

 private:
  Phase phase_=Phase::Clear;
  Epoch epoch_=0;
  Ticket sequence_=0,review_=0;
  bool historySeen_=false,idleSeen_=false;
  void invalidate(Phase phase) {
    phase_=phase;review_=0;historySeen_=idleSeen_=false;
  }
  bool matches(Epoch epoch,Ticket ticket)const {
    return epoch&&epoch==epoch_&&ticket&&ticket==review_&&
        (phase_==Phase::Synchronizing||phase_==Phase::Reviewable);
  }
  void update() { phase_=historySeen_&&idleSeen_?Phase::Reviewable:Phase::Synchronizing; }
};

}  // namespace send_recovery
