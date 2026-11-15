#pragma once

#include "AgentSelection.h"
#include <cstddef>
#include <cstdint>

// Pure application policy. No hardware, network, clock, credential, String or
// allocation dependency. The owner serially dispatches validated observations,
// drains effects into the existing production adapters, and renders snapshot().
namespace experience {

using Generation = std::uint64_t;
enum class Page : std::uint8_t { Home, Chat };
enum class Connection : std::uint8_t { Offline, NeedsBinding, Authenticating, Connecting, Ready };
enum class TurnPhase : std::uint8_t { Idle, Submitting, Running, Reconciling, Uncertain };
enum class ActivationPhase : std::uint8_t { Idle, Waiting, Queued, InFlight, RetryWait, Failed, Ready };
enum class Outcome : std::uint8_t { Succeeded, RetryableFailure, PermanentFailure, Cancelled };
enum class Notice : std::uint8_t {
  None, WaitingForConnection, NeedsBinding, Authenticating, Activating, Retrying,
  TurnBusy, ReconcileTurn, ActivationFailed, FactoryUnavailable, GenerationExhausted
};

struct Snapshot {
  Page page = Page::Home;
  Connection connection = Connection::Offline;
  TurnPhase turnPhase = TurnPhase::Idle;
  ActivationPhase activation = ActivationPhase::Idle;
  Notice notice = Notice::None;
  std::size_t desiredAgent = 0, activeAgent = 0;
  Generation activationGeneration = 0;
  bool hasActive = false, sessionReady = false, openIntent = false, exitPending = false;
};

enum class EventKind : std::uint8_t {
  Browse, Open, Back, ConnectionChanged, BindingChanged, ActivationFinished,
  ActivationDeferred, RetryDue, TurnChanged, FactoryFailed
};
struct Event {
  EventKind kind = EventKind::Open;
  int delta = 0;
  Generation generation = 0;
  Connection connectionValue = Connection::Offline;
  TurnPhase turnValue = TurnPhase::Idle;
  Outcome outcome = Outcome::Succeeded;

  static Event browse(int delta);
  static Event open();
  static Event back();
  static Event connection(Connection value);
  // A real machine/account/Profile identity change, never a temporary outage.
  static Event bindingChanged();
  static Event activationFinished(Generation generation, Outcome outcome);
  // Adapter could not start a worker (e.g. Busy): no operation was accepted.
  static Event activationDeferred(Generation generation);
  static Event retryDue(Generation generation);
  // Owner projects the CURRENT validated conversation's run/voice occupancy.
  // Home/Back never aborts it. Late foreign-context observations are rejected
  // by the protocol adapter before entering this pure application interface.
  static Event turn(TurnPhase value);
  static Event factoryFailed();
};

enum class EffectKind : std::uint8_t { Activate, CancelActivation, ReturnFactory };
struct Effect {
  EffectKind kind = EffectKind::Activate;
  std::size_t agent = 0;
  Generation generation = 0;
};

class Controller {
 public:
  // Restores only the local carousel position. Construction never activates.
  explicit Controller(std::size_t initialAgent = 0);
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;
  // True means the event was accepted. In particular, apply a worker's session
  // result to transport/model mirrors ONLY when its ActivationFinished event
  // was accepted; a stale/cancelled generation must never change that context.
  bool dispatch(const Event& event);
  const Snapshot& snapshot() const { return state_; }
  // Fixed/coalesced effect storage, no fallible queue and no dropped Cancel.
  // Taking Activate reserves the single activation slot. The adapter MUST
  // report Finished (including cancelled late work), or Deferred if it did not
  // start. Cancel is cooperative: the next activation waits for that report.
  bool takeEffect(Effect& effect);

 private:
  Snapshot state_;
  Generation workerGeneration_ = 0;
  std::size_t workerAgent_ = 0;
  bool cancelPending_ = false, factoryPending_ = false;
  bool newGeneration();
  bool turnBusy() const;
  bool recoveryActivation() const;
  void cancelIntent();
  void scheduleIntent();
  void connectionNotice();
};

}  // namespace experience
