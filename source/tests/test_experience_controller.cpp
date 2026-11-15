#include "ExperienceController.h"
#include <cassert>
#include <cstdio>
#include <climits>
#include <initializer_list>
#include <type_traits>

using namespace experience;
namespace {
void none(Controller& app) { Effect effect; assert(!app.takeEffect(effect)); }
Effect next(Controller& app, EffectKind kind) {
  Effect effect;assert(app.takeEffect(effect)&&effect.kind==kind);return effect;
}
Generation start(Controller& app) {
  assert(app.dispatch(Event::connection(Connection::Ready)));
  assert(app.dispatch(Event::open()));
  const auto effect=next(app,EffectKind::Activate);
  assert(effect.agent==app.snapshot().desiredAgent&&effect.generation!=0);
  return effect.generation;
}
void activate(Controller& app) {
  assert(app.dispatch(Event::activationFinished(start(app),Outcome::Succeeded)));
  assert(app.snapshot().page==Page::Chat&&app.snapshot().sessionReady);
}
void onceOk() {
  Controller app(3); Effect effect;
  assert(app.snapshot().desiredAgent==3&&!app.takeEffect(effect));
  app.dispatch(Event::browse(-4));
  assert(app.snapshot().desiredAgent==15&&!app.takeEffect(effect));
  app.dispatch(Event::connection(Connection::Ready));
  assert(!app.takeEffect(effect));
  app.dispatch(Event::open());
  assert(app.snapshot().openIntent&&app.snapshot().page==Page::Home);
  assert(app.takeEffect(effect)&&effect.kind==EffectKind::Activate&&effect.agent==15);
  const auto generation=effect.generation;
  app.dispatch(Event::open());app.dispatch(Event::connection(Connection::Ready));
  assert(app.snapshot().activationGeneration==generation);none(app);
  assert(app.dispatch(Event::activationFinished(generation,Outcome::Succeeded)));
  assert(app.snapshot().page==Page::Chat&&app.snapshot().activeAgent==15);
  assert(!app.snapshot().openIntent);none(app);
}
void offlineBrowsing() {
  Controller app(200);assert(app.snapshot().desiredAgent==0);
  for(unsigned i=0;i!=32;++i) {app.dispatch(Event::browse(1));none(app);}
  assert(app.snapshot().desiredAgent==0&&!app.snapshot().hasActive);
  app.dispatch(Event::browse(INT_MIN));assert(app.snapshot().desiredAgent==0);
  app.dispatch(Event::browse(INT_MAX));assert(app.snapshot().desiredAgent==15);none(app);
  app.dispatch(Event::open());assert(app.snapshot().activation==ActivationPhase::Waiting);none(app);
  app.dispatch(Event::connection(Connection::Ready));next(app,EffectKind::Activate);
}
void backBeforeAndAfterWorker() {
  {
    Controller app;app.dispatch(Event::connection(Connection::Ready));app.dispatch(Event::open());
    const auto generation=app.snapshot().activationGeneration;
    app.dispatch(Event::back());none(app);
    assert(!app.dispatch(Event::activationFinished(generation,Outcome::Succeeded)));
    assert(app.snapshot().page==Page::Home&&!app.snapshot().hasActive);
  }
  {
    Controller app;const auto generation=start(app);app.dispatch(Event::back());
    const auto cancel=next(app,EffectKind::CancelActivation);assert(cancel.generation==generation);none(app);
    assert(!app.dispatch(Event::activationFinished(generation,Outcome::Succeeded)));
    assert(app.snapshot().page==Page::Home&&!app.snapshot().hasActive);none(app);
  }
}
void latestExplicitIntentWins() {
  Controller app;const auto old=start(app);
  app.dispatch(Event::browse(5));assert(!app.snapshot().openIntent&&app.snapshot().desiredAgent==5);
  app.dispatch(Event::open());const auto current=app.snapshot().activationGeneration;
  assert(current!=old&&app.snapshot().openIntent);
  assert(next(app,EffectKind::CancelActivation).generation==old);none(app);
  assert(!app.dispatch(Event::activationFinished(old,Outcome::Succeeded)));
  const auto wanted=next(app,EffectKind::Activate);assert(wanted.agent==5&&wanted.generation==current);
  assert(app.dispatch(Event::activationFinished(current,Outcome::Succeeded)));
  assert(app.snapshot().activeAgent==5&&app.snapshot().page==Page::Chat);none(app);
}
void deferredAndTransientKeepIntent() {
  for(auto outcome:{Outcome::RetryableFailure,Outcome::Cancelled}) {
    Controller app(8);const auto generation=start(app);
    assert(app.dispatch(Event::activationFinished(generation,outcome)));
    assert(app.snapshot().desiredAgent==8&&app.snapshot().openIntent&&app.snapshot().activation==ActivationPhase::RetryWait);
    none(app);assert(!app.dispatch(Event::retryDue(generation+1)));none(app);
    assert(app.dispatch(Event::retryDue(generation)));
    auto effect=next(app,EffectKind::Activate);assert(effect.agent==8&&effect.generation==generation);
    assert(app.dispatch(Event::activationDeferred(generation)));
    assert(app.snapshot().openIntent&&app.snapshot().desiredAgent==8);none(app);
    app.dispatch(Event::retryDue(generation));effect=next(app,EffectKind::Activate);
    assert(effect.generation==generation);
    assert(app.dispatch(Event::activationFinished(generation,Outcome::Succeeded)));
  }
}
void backDuringRetryStopsRetry() {
  Controller app;auto generation=start(app);
  app.dispatch(Event::activationDeferred(generation));app.dispatch(Event::back());
  assert(!app.dispatch(Event::retryDue(generation)));none(app);
  assert(app.snapshot().page==Page::Home&&!app.snapshot().openIntent);
}
void failedTargetDoesNotLeaveOldSessionReady() {
  Controller app(2);activate(app);app.dispatch(Event::back());
  app.dispatch(Event::browse(1));app.dispatch(Event::open());
  auto effect=next(app,EffectKind::Activate);
  assert(!app.snapshot().sessionReady&&app.snapshot().hasActive&&app.snapshot().activeAgent==2);
  app.dispatch(Event::activationFinished(effect.generation,Outcome::PermanentFailure));
  assert(app.snapshot().page==Page::Home&&app.snapshot().notice==Notice::ActivationFailed);
  app.dispatch(Event::browse(-1));app.dispatch(Event::open());
  effect=next(app,EffectKind::Activate);assert(effect.agent==2);
  assert(app.dispatch(Event::activationFinished(effect.generation,Outcome::Succeeded)));
}
void cachedSessionReopensWithoutWork() {
  Controller app(6);activate(app);app.dispatch(Event::back());none(app);
  const auto generation=app.snapshot().activationGeneration;
  app.dispatch(Event::open());none(app);
  assert(app.snapshot().page==Page::Chat&&app.snapshot().activationGeneration==generation);
}
void connectionRecoveryOnlyForOpenChat() {
  {
    Controller app(4);activate(app);
    app.dispatch(Event::connection(Connection::Offline));
    assert(app.snapshot().page==Page::Chat&&app.snapshot().openIntent&&!app.snapshot().sessionReady);none(app);
    app.dispatch(Event::connection(Connection::Ready));
    const auto effect=next(app,EffectKind::Activate);assert(effect.agent==4);
    app.dispatch(Event::activationFinished(effect.generation,Outcome::Succeeded));
    assert(app.snapshot().page==Page::Chat&&app.snapshot().sessionReady);
  }
  {
    Controller app(4);activate(app);app.dispatch(Event::back());
    app.dispatch(Event::connection(Connection::Offline));app.dispatch(Event::connection(Connection::Ready));
    assert(app.snapshot().page==Page::Home&&!app.snapshot().openIntent);none(app);
  }
}
void connectionChangesRetireOldWorker() {
  Controller app;const auto old=start(app);
  app.dispatch(Event::connection(Connection::Offline));
  app.dispatch(Event::connection(Connection::Ready));
  assert(next(app,EffectKind::CancelActivation).generation==old);none(app);
  assert(!app.dispatch(Event::activationFinished(old,Outcome::Succeeded)));
  const auto nextGeneration=next(app,EffectKind::Activate).generation;
  assert(nextGeneration!=old);
  app.dispatch(Event::activationFinished(nextGeneration,Outcome::Succeeded));
  assert(app.snapshot().page==Page::Chat);
}
void recoverUncertainSameSession() {
  for(auto phase:{TurnPhase::Submitting,TurnPhase::Running,TurnPhase::Reconciling,TurnPhase::Uncertain}) {
    Controller app(1);activate(app);app.dispatch(Event::turn(phase));
    app.dispatch(Event::connection(Connection::Offline));app.dispatch(Event::connection(Connection::Ready));
    const auto effect=next(app,EffectKind::Activate);assert(effect.agent==1);
    assert(app.dispatch(Event::activationFinished(effect.generation,Outcome::Succeeded)));
    assert(app.snapshot().turnPhase==phase); // Reattaching is not a send ACK or replay.
    app.dispatch(Event::back());app.dispatch(Event::browse(1));
    assert(!app.dispatch(Event::open()));none(app);
    assert(app.snapshot().page==Page::Home&&app.snapshot().activeAgent==1);
    assert(app.snapshot().notice==(phase==TurnPhase::Reconciling||phase==TurnPhase::Uncertain?Notice::ReconcileTurn:Notice::TurnBusy));
    app.dispatch(Event::turn(TurnPhase::Idle));app.dispatch(Event::open());
    assert(next(app,EffectKind::Activate).agent==2);
  }
}
void busyNeverActivatesUnknownIdentity() {
  Controller app;app.dispatch(Event::connection(Connection::Ready));app.dispatch(Event::turn(TurnPhase::Uncertain));
  assert(!app.dispatch(Event::open()));none(app);assert(!app.snapshot().openIntent);
}
void newTurnHoldsDifferentTargetWithoutLosingIntent() {
  Controller app(3);activate(app);app.dispatch(Event::back());
  app.dispatch(Event::browse(1));app.dispatch(Event::open());
  const auto interrupted=next(app,EffectKind::Activate).generation;
  app.dispatch(Event::turn(TurnPhase::Running));
  assert(app.snapshot().desiredAgent==4&&app.snapshot().openIntent&&app.snapshot().activeAgent==3);
  assert(next(app,EffectKind::CancelActivation).generation==interrupted);none(app);
  assert(!app.dispatch(Event::activationFinished(interrupted,Outcome::Succeeded)));none(app);
  app.dispatch(Event::turn(TurnPhase::Idle));
  const auto effect=next(app,EffectKind::Activate);
  assert(effect.agent==4&&effect.generation!=interrupted);
  app.dispatch(Event::activationFinished(effect.generation,Outcome::Succeeded));
  assert(app.snapshot().activeAgent==4&&app.snapshot().page==Page::Chat);
}
void backPreservesRunningTurn() {
  Controller app;activate(app);app.dispatch(Event::turn(TurnPhase::Running));
  app.dispatch(Event::back());assert(app.snapshot().turnPhase==TurnPhase::Running);none(app);
  app.dispatch(Event::open());assert(app.snapshot().page==Page::Chat);none(app);
}
void bindingChangeRequiresNewIntent() {
  Controller app(7);activate(app);app.dispatch(Event::turn(TurnPhase::Running));
  app.dispatch(Event::bindingChanged());
  assert(app.snapshot().page==Page::Home&&!app.snapshot().hasActive&&!app.snapshot().sessionReady);
  assert(app.snapshot().desiredAgent==7&&app.snapshot().turnPhase==TurnPhase::Idle);none(app);
  app.dispatch(Event::connection(Connection::Ready));none(app);
  app.dispatch(Event::open());assert(next(app,EffectKind::Activate).agent==7);
}
void factoryWaitsForActivationCancellation() {
  Controller app;const auto generation=start(app);app.dispatch(Event::back());app.dispatch(Event::back());
  assert(app.snapshot().exitPending);
  assert(next(app,EffectKind::CancelActivation).generation==generation);none(app);
  assert(!app.dispatch(Event::open()));
  assert(!app.dispatch(Event::activationFinished(generation,Outcome::Succeeded)));
  next(app,EffectKind::ReturnFactory);none(app);
  assert(app.dispatch(Event::factoryFailed()));
  assert(!app.snapshot().exitPending&&app.snapshot().notice==Notice::FactoryUnavailable);
}
void foreignAndDuplicateCompletionsAreIgnored() {
  Controller app;const auto generation=start(app);
  assert(!app.dispatch(Event::activationFinished(generation+1,Outcome::Succeeded)));none(app);
  assert(app.dispatch(Event::activationFinished(generation,Outcome::Succeeded)));
  assert(!app.dispatch(Event::activationFinished(generation,Outcome::PermanentFailure)));
  assert(app.snapshot().page==Page::Chat&&app.snapshot().sessionReady);none(app);
}
void invalidObservationsAreIgnored() {
  Controller app;
  assert(!app.dispatch(Event::connection(static_cast<Connection>(255))));
  assert(!app.dispatch(Event::turn(static_cast<TurnPhase>(255))));
  Event invalid;invalid.kind=static_cast<EventKind>(255);assert(!app.dispatch(invalid));none(app);
}
} // namespace
int main() {
  static_assert(!std::is_copy_constructible<Controller>::value,"Effect ownership cannot be copied");
  static_assert(sizeof(Controller)<=128,"Controller must remain small on a no-PSRAM board");
  onceOk();offlineBrowsing();backBeforeAndAfterWorker();latestExplicitIntentWins();deferredAndTransientKeepIntent();
  backDuringRetryStopsRetry();failedTargetDoesNotLeaveOldSessionReady();cachedSessionReopensWithoutWork();
  connectionRecoveryOnlyForOpenChat();connectionChangesRetireOldWorker();recoverUncertainSameSession();
  busyNeverActivatesUnknownIdentity();newTurnHoldsDifferentTargetWithoutLosingIntent();backPreservesRunningTurn();bindingChangeRequiresNewIntent();
  factoryWaitsForActivationCancellation();foreignAndDuplicateCompletionsAreIgnored();invalidObservationsAreIgnored();
  std::printf("Experience Controller: 18 scenario groups passed through dispatch/snapshot/takeEffect; state=%zu bytes; no hardware execution\n",sizeof(Controller));
}
