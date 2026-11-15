#include "ExperienceController.h"
#include <limits>

namespace experience {
namespace {
Event simple(EventKind kind) { Event event; event.kind=kind; return event; }
}
Event Event::browse(int delta) { auto event=simple(EventKind::Browse); event.delta=delta; return event; }
Event Event::open() { return simple(EventKind::Open); }
Event Event::back() { return simple(EventKind::Back); }
Event Event::connection(Connection value) { auto event=simple(EventKind::ConnectionChanged); event.connectionValue=value; return event; }
Event Event::bindingChanged() { return simple(EventKind::BindingChanged); }
Event Event::activationFinished(Generation generation, Outcome outcome) { auto event=simple(EventKind::ActivationFinished); event.generation=generation; event.outcome=outcome; return event; }
Event Event::activationDeferred(Generation generation) { auto event=simple(EventKind::ActivationDeferred); event.generation=generation; return event; }
Event Event::retryDue(Generation generation) { auto event=simple(EventKind::RetryDue); event.generation=generation; return event; }
Event Event::turn(TurnPhase value) { auto event=simple(EventKind::TurnChanged); event.turnValue=value; return event; }
Event Event::factoryFailed() { return simple(EventKind::FactoryFailed); }

Controller::Controller(std::size_t initialAgent) {
  state_.desiredAgent=initialAgent<kNativeAgentCount?initialAgent:0;
}
bool Controller::newGeneration() {
  if(state_.activationGeneration==(std::numeric_limits<Generation>::max)()) {
    state_.openIntent=false;state_.activation=ActivationPhase::Failed;
    state_.notice=Notice::GenerationExhausted;return false;
  }
  ++state_.activationGeneration;return true;
}
bool Controller::turnBusy()const { return state_.turnPhase!=TurnPhase::Idle; }
bool Controller::recoveryActivation()const {
  // Only a known identity can reattach to its existing durable session while
  // an earlier send is unresolved. This grants no permission to resend it.
  return state_.hasActive&&state_.desiredAgent==state_.activeAgent&&!state_.sessionReady;
}
void Controller::connectionNotice() {
  state_.notice=state_.connection==Connection::NeedsBinding?Notice::NeedsBinding:
      state_.connection==Connection::Authenticating?Notice::Authenticating:Notice::WaitingForConnection;
}
void Controller::cancelIntent() {
  const bool invalidates=state_.openIntent||workerGeneration_!=0;
  state_.openIntent=false;
  if(workerGeneration_)cancelPending_=true;
  if(invalidates&&!newGeneration())return;
  state_.activation=state_.sessionReady?ActivationPhase::Ready:ActivationPhase::Idle;
  state_.notice=Notice::None;
}
void Controller::scheduleIntent() {
  if(!state_.openIntent||state_.exitPending)return;
  if(state_.connection!=Connection::Ready) {
    state_.activation=ActivationPhase::Waiting;connectionNotice();return;
  }
  if(turnBusy()&&!recoveryActivation()) {
    state_.activation=ActivationPhase::Waiting;
    state_.notice=state_.turnPhase==TurnPhase::Reconciling||state_.turnPhase==TurnPhase::Uncertain?
        Notice::ReconcileTurn:Notice::TurnBusy;
    return;
  }
  state_.activation=ActivationPhase::Queued;state_.notice=Notice::Activating;
}
bool Controller::dispatch(const Event& event) {
  if(state_.exitPending&&event.kind!=EventKind::ActivationFinished&&
      event.kind!=EventKind::ActivationDeferred&&event.kind!=EventKind::FactoryFailed)return false;
  switch(event.kind) {
    case EventKind::Browse: {
      if(state_.page!=Page::Home)return false;
      const auto count=static_cast<int>(kNativeAgentCount);
      const auto wanted=static_cast<std::size_t>((static_cast<int>(state_.desiredAgent)+event.delta%count+count)%count);
      if(wanted!=state_.desiredAgent) {cancelIntent();state_.desiredAgent=wanted;}
      return true;
    }
    case EventKind::Open:
      if(turnBusy()&&(!state_.hasActive||state_.desiredAgent!=state_.activeAgent)) {
        state_.notice=state_.turnPhase==TurnPhase::Reconciling||state_.turnPhase==TurnPhase::Uncertain?
            Notice::ReconcileTurn:Notice::TurnBusy;return false;
      }
      if(state_.hasActive&&state_.desiredAgent==state_.activeAgent&&state_.sessionReady) {
        state_.page=Page::Chat;state_.openIntent=false;
        state_.activation=ActivationPhase::Ready;state_.notice=Notice::None;return true;
      }
      if(state_.openIntent)return true; // Repeated OK never creates another job.
      if(!newGeneration())return false;
      state_.openIntent=true;
      // A known conversation remains readable while it reconnects. A new
      // Agent opens only after its own successful activation, never beforehand.
      if(state_.hasActive&&state_.desiredAgent==state_.activeAgent)state_.page=Page::Chat;
      scheduleIntent();return true;
    case EventKind::Back:
      if(state_.page==Page::Chat||state_.openIntent) {
        cancelIntent();state_.page=Page::Home;return true;
      }
      cancelIntent();state_.exitPending=factoryPending_=true;return true;
    case EventKind::ConnectionChanged:
      if(static_cast<unsigned>(event.connectionValue)>static_cast<unsigned>(Connection::Ready))return false;
      if(event.connectionValue==state_.connection)return true;
      state_.connection=event.connectionValue;
      if(state_.connection!=Connection::Ready) {
        state_.sessionReady=false;
        if(workerGeneration_)cancelPending_=true;
        if(state_.page==Page::Chat&&state_.hasActive) {
          state_.desiredAgent=state_.activeAgent;state_.openIntent=true;
        }
        if(state_.openIntent&&!newGeneration())return false;
        if(state_.openIntent)scheduleIntent();
        else {state_.activation=ActivationPhase::Idle;connectionNotice();}
      } else if(state_.openIntent) scheduleIntent();
      else if(state_.notice==Notice::WaitingForConnection||state_.notice==Notice::NeedsBinding||
              state_.notice==Notice::Authenticating)state_.notice=Notice::None;
      return true;
    case EventKind::BindingChanged:
      cancelIntent();state_.page=Page::Home;
      state_.hasActive=state_.sessionReady=false;state_.turnPhase=TurnPhase::Idle;
      state_.activation=ActivationPhase::Idle;state_.connection=Connection::Connecting;
      state_.notice=Notice::WaitingForConnection;return true;
    case EventKind::ActivationFinished: {
      if(!event.generation||event.generation!=workerGeneration_||
          static_cast<unsigned>(event.outcome)>static_cast<unsigned>(Outcome::Cancelled))return false;
      workerGeneration_=0;cancelPending_=false;
      if(event.generation!=state_.activationGeneration||!state_.openIntent||
          state_.connection!=Connection::Ready||state_.exitPending) {
        scheduleIntent();return false;
      }
      if(event.outcome==Outcome::Succeeded) {
        state_.activeAgent=state_.desiredAgent;state_.hasActive=state_.sessionReady=true;
        state_.page=Page::Chat;state_.openIntent=false;
        state_.activation=ActivationPhase::Ready;state_.notice=Notice::None;
      } else if(event.outcome==Outcome::PermanentFailure) {
        state_.openIntent=false;state_.activation=ActivationPhase::Failed;
        state_.notice=Notice::ActivationFailed;
      } else {
        state_.activation=ActivationPhase::RetryWait;state_.notice=Notice::Retrying;
      }
      return true;
    }
    case EventKind::ActivationDeferred:
      if(!event.generation||event.generation!=workerGeneration_)return false;
      workerGeneration_=0;cancelPending_=false;
      if(event.generation!=state_.activationGeneration||!state_.openIntent||state_.exitPending) {
        scheduleIntent();return false;
      }
      state_.activation=ActivationPhase::RetryWait;state_.notice=Notice::Retrying;return true;
    case EventKind::RetryDue:
      if(!event.generation||event.generation!=state_.activationGeneration||!state_.openIntent||
          state_.activation!=ActivationPhase::RetryWait)return false;
      scheduleIntent();return true;
    case EventKind::TurnChanged:
      if(static_cast<unsigned>(event.turnValue)>static_cast<unsigned>(TurnPhase::Uncertain))return false;
      if(event.turnValue==state_.turnPhase)return true;
      state_.turnPhase=event.turnValue;
      if(state_.openIntent&&turnBusy()&&!recoveryActivation()) {
        if(workerGeneration_)cancelPending_=true;
        if(!newGeneration())return false;
        scheduleIntent();
      } else if(state_.openIntent&&state_.activation==ActivationPhase::Waiting)scheduleIntent();
      else if(!turnBusy()&&(state_.notice==Notice::TurnBusy||state_.notice==Notice::ReconcileTurn))state_.notice=Notice::None;
      return true;
    case EventKind::FactoryFailed:
      if(!state_.exitPending)return false;
      state_.exitPending=factoryPending_=false;state_.notice=Notice::FactoryUnavailable;return true;
  }
  return false;
}
bool Controller::takeEffect(Effect& effect) {
  if(cancelPending_&&workerGeneration_) {
    cancelPending_=false;effect.kind=EffectKind::CancelActivation;
    effect.agent=workerAgent_;effect.generation=workerGeneration_;return true;
  }
  if(factoryPending_&&!workerGeneration_) {
    factoryPending_=false;effect.kind=EffectKind::ReturnFactory;
    effect.agent=state_.desiredAgent;effect.generation=0;return true;
  }
  if(state_.exitPending||workerGeneration_||!state_.openIntent||
      state_.activation!=ActivationPhase::Queued||state_.connection!=Connection::Ready||
      (turnBusy()&&!recoveryActivation()))return false;
  workerGeneration_=state_.activationGeneration;workerAgent_=state_.desiredAgent;
  // The production selection adapter invalidates its old confirmation before
  // starting. Keep only the identity, never an unusable cached-ready claim.
  state_.sessionReady=false;
  state_.activation=ActivationPhase::InFlight;
  effect.kind=EffectKind::Activate;effect.agent=workerAgent_;effect.generation=workerGeneration_;return true;
}
}  // namespace experience
