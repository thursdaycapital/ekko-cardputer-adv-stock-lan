#include "McuReauth.h"
#include "RemoteFallback.h"
#include "AgentSelection.h"
#include "AudioStreamQueue.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

using native_reauth::Conditions;
using native_reauth::Outcome;
using native_reauth::RetryPolicy;
using native_reauth::Target;
using native_reauth::loginOutcome;
using native_reauth::BootstrapAction;
using native_reauth::BootstrapState;
using native_reauth::NamespaceAction;

namespace {
const Target kTarget = {"machine|desktop|8748", "http://desktop:8748", "https://relay.test", "default", true};
const Conditions kReady = {true, false, false};

void classifyCurrentAttempt() {
  for (int stale : {-11, -1, 0, 200, 401, 403, 503}) {
    assert(loginOutcome(false, false, stale) == Outcome::NoAttempt);
    assert(loginOutcome(false, true, stale) == Outcome::NoAttempt);
  }
  for (int failure : {-11, -1, 0, 200, 302, 400, 404, 408, 425, 429, 500, 503})
    assert(loginOutcome(true, false, failure) == Outcome::TransientFailure);
  for (int denied : {401, 403}) {
    assert(loginOutcome(true, false, denied) == Outcome::Rejected);
    assert(loginOutcome(true, true, denied) == Outcome::Rejected);
  }
  for (int ok : {200, 201, 204, 299}) assert(loginOutcome(true, true, ok) == Outcome::Success);
  assert(loginOutcome(true, true, 500) == Outcome::TransientFailure);
}

void pauseAndBackoff() {
  RetryPolicy policy;
  assert(!policy.blocksSocket());
  assert(policy.request(kTarget));
  assert(policy.blocksSocket());
  assert(policy.mayBegin(0, kReady));
  assert(!policy.inFlight());
  assert(policy.begin(0, kReady));
  assert(!policy.begin(1, kReady));
  uint32_t failedAt = 1000;
  for (uint32_t delay : {30000U, 60000U, 120000U, 120000U}) {
    assert(policy.complete(Outcome::TransientFailure, failedAt));
    assert(!policy.complete(Outcome::TransientFailure, failedAt + 1));
    assert(policy.retryDelayMs() == delay);
    assert(policy.request(kTarget)); // Another auth.invalid must not defeat this deadline.
    assert(!policy.mayBegin(failedAt + delay - 1, kReady));
    assert(policy.blocksSocket());
    assert(policy.begin(failedAt + delay, kReady));
    failedAt += delay + 500;
  }
}

void noAttemptIsRetryable() {
  RetryPolicy policy;
  assert(policy.request(kTarget));
  assert(policy.begin(100, kReady));
  assert(policy.complete(Outcome::NoAttempt, 200));
  assert(!policy.rejected());
  assert(!policy.begin(30199, kReady));
  assert(policy.begin(30200, kReady));
  assert(policy.complete(Outcome::NoAttempt, 30300));
  assert(policy.retryDelayMs() == 60000);
}

void deniedRequiresExplicitAction() {
  for (int denied : {401, 403}) {
    RetryPolicy policy;
    assert(policy.request(kTarget));
    assert(policy.begin(100, kReady));
    assert(policy.complete(loginOutcome(true, false, denied), 200));
    assert(policy.rejected());
    assert(policy.blocksSocket());
    assert(policy.request(kTarget));
    assert(!policy.begin(1000000, kReady));
    assert(!policy.complete(Outcome::Success, 1000000));
    policy.cancel();
    assert(!policy.blocksSocket());
    assert(!policy.rejected());
    assert(policy.request(kTarget));
    assert(policy.begin(1000001, kReady));
  }
}

void conditionsNeverConsumeAttempts() {
  for (const Conditions blocked : {Conditions{false, false, false}, Conditions{true, true, false},
                                   Conditions{true, false, true}}) {
    RetryPolicy policy;
    assert(policy.request(kTarget));
    assert(!policy.begin(100, blocked));
    assert(!policy.inFlight());
    assert(policy.retryDelayMs() == 0);
    assert(policy.blocksSocket());
    assert(policy.begin(101, kReady));
    assert(policy.complete(Outcome::Success, 102));
    assert(!policy.blocksSocket());
  }
}

void targetBinding() {
  RetryPolicy policy;
  assert(!policy.request({nullptr, kTarget.deviceUrl, kTarget.relayUrl, kTarget.profile, true}));
  assert(!policy.request({kTarget.deviceKey, "", kTarget.relayUrl, kTarget.profile, true}));
  assert(!policy.request({kTarget.deviceKey, kTarget.deviceUrl, kTarget.relayUrl, "", true}));
  assert(!policy.active());
  char key[] = "machine|desktop|8748";
  assert(policy.request({key, kTarget.deviceUrl, kTarget.relayUrl, kTarget.profile, true}));
  key[0] = 'X'; // The queued identity is owned, rather than an alias to discovery's mutable list.
  assert(policy.matches(kTarget));
  const Target changed[] = {
    {"other|desktop|8748", kTarget.deviceUrl, kTarget.relayUrl, kTarget.profile, true},
    {"machine|web|8748", kTarget.deviceUrl, kTarget.relayUrl, kTarget.profile, true},
    {"machine|desktop|8648", kTarget.deviceUrl, kTarget.relayUrl, kTarget.profile, true},
    {kTarget.deviceKey, "http://other:8748", kTarget.relayUrl, kTarget.profile, true},
    {kTarget.deviceKey, kTarget.deviceUrl, "https://other-relay.test", kTarget.profile, true},
    {kTarget.deviceKey, kTarget.deviceUrl, kTarget.relayUrl, "other-profile", true},
    {kTarget.deviceKey, kTarget.deviceUrl, kTarget.relayUrl, kTarget.profile, false},
  };
  for (const Target& target : changed) assert(!policy.matches(target));
  assert(policy.begin(1, kReady));
  policy.cancel(); // Caller cancels on any identity/profile/source change before it may begin again.
  assert(!policy.matches(kTarget));
  assert(!policy.complete(Outcome::Rejected, 2));
  assert(!policy.blocksSocket());
}

void cancellationAndSuccessReset() {
  RetryPolicy policy;
  assert(policy.request(kTarget));
  assert(policy.begin(10, kReady));
  assert(policy.complete(Outcome::TransientFailure, 20));
  policy.cancel();
  assert(!policy.active());
  assert(policy.retryDelayMs() == 0);
  assert(!policy.begin(100000, kReady));
  assert(policy.request(kTarget));
  assert(policy.begin(100001, kReady));
  assert(policy.complete(Outcome::Success, 100002));
  assert(!policy.active());
  assert(!policy.blocksSocket());
  assert(policy.retryDelayMs() == 0);
  assert(!policy.complete(Outcome::TransientFailure, 100003));
}

void timerWraparound() {
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  RetryPolicy policy;
  assert(policy.request(kTarget));
  assert(policy.begin(maximum - 10000, kReady));
  assert(policy.complete(Outcome::TransientFailure, maximum - 9999));
  assert(!policy.begin(19999, kReady));
  assert(policy.begin(20000, kReady));
  assert(policy.complete(Outcome::TransientFailure, 20001));
  assert(!policy.begin(80000, kReady));
  assert(policy.begin(80001, kReady));
}

void newInvalidEventAfterLoginSuccess() {
  RetryPolicy policy;
  assert(!policy.blocksSocket()); // Restoring a saved Profile does not request reauth.
  assert(policy.request(kTarget));
  assert(policy.begin(1, kReady));
  assert(policy.complete(Outcome::Success, 2));
  assert(!policy.blocksSocket());
  // The caller completes HTTP login before connecting the new namespace.
  // A rejection received immediately by that connect is a fresh request.
  assert(policy.request(kTarget));
  assert(policy.blocksSocket());
  assert(policy.begin(3, kReady));
  assert(policy.complete(Outcome::TransientFailure, 4));
  assert(!policy.begin(30003, kReady));
  assert(policy.begin(30004, kReady));
}

void bootstrapDoesNotRotateCachedToken() {
  BootstrapState saved{true, true, true, true, true, false, false};
  assert(native_reauth::bootstrapAction(saved) == BootstrapAction::ResumeSocket);
  saved.hasSavedCredentials = true;
  assert(native_reauth::bootstrapAction(saved) == BootstrapAction::ResumeSocket);
  saved.hasToken = false;
  assert(native_reauth::bootstrapAction(saved) == BootstrapAction::QueueReauth);
  saved.hasSavedCredentials = false;
  assert(native_reauth::bootstrapAction(saved) == BootstrapAction::RequireManual);
  const BootstrapState blocked[] = {
    {true, false, true, true, true, true, false}, // Missing machine binding.
    {true, true, false, true, true, true, false}, // No selected Profile.
    {true, true, true, true, false, true, false}, // LAN/relay source disagrees.
    {true, true, true, true, true, true, true}, // Relay replacement is never auto-unlocked.
    {true, true, true, false, false, true, false},
    {true, true, true, false, true, true, true},
  };
  for (const auto& state : blocked)
    assert(native_reauth::bootstrapAction(state) == BootstrapAction::RequireManual);
  saved.wifiReady = false;
  assert(native_reauth::bootstrapAction(saved) == BootstrapAction::WaitForWifi);
  // A queued missing-token bootstrap uses the same transient retry policy.
  RetryPolicy retry;
  assert(retry.request(kTarget));
  assert(retry.begin(0, kReady));
  assert(retry.complete(loginOutcome(true, false, -11), 100));
  assert(!retry.begin(30099, kReady));
  assert(retry.begin(30100, kReady));
  assert(retry.complete(loginOutcome(false, false, 401), 30200));
  assert(!retry.rejected()); // No attempt cannot inherit a historical 401.
  assert(retry.retryDelayMs() == 60000);
}

void completeBindingSyntax() {
  for (const char* key : {"machine|desktop|8748", "machine|web|1", "machine|custom|65535"})
    assert(native_reauth::completeDeviceBinding(key));
  for (const char* key : {static_cast<const char*>(nullptr), "", "machine", "|desktop|8748", "machine||8748",
      "machine|desktop|", "machine|desktop|0", "machine|desktop|65536", "machine|desktop|-1",
      "machine|desktop|8748suffix", "machine|desktop|999999999999999999999999", "machine|desktop|8748|other"})
    assert(!native_reauth::completeDeviceBinding(key));
}

void namespaceErrorClassification() {
  for (const char* code : {"unauthorized", "invalid_token", "token_invalid", "token_expired", "jwt_expired", "auth.invalid"})
    assert(native_reauth::namespaceAction(code, "") == NamespaceAction::Reauthenticate);
  for (const char* message : {"Unauthorized", " token expired \n", "invalid token", "jwt expired",
      "Authentication failed", "  AUTHENTICATION FAILED\r\n"})
    assert(native_reauth::namespaceAction("", message) == NamespaceAction::Reauthenticate);
  assert(native_reauth::namespaceAction("UNAUTHORIZED", "Profile access denied") == NamespaceAction::RequireManual);
  assert(native_reauth::namespaceAction("device_code_not_allowed", "Unauthorized") == NamespaceAction::RequireManual);
  assert(native_reauth::namespaceAction("unofficial_device_code", "") == NamespaceAction::RequireManual);
  assert(native_reauth::namespaceAction("", "此设备为非官方设备码") == NamespaceAction::RequireManual);
  assert(native_reauth::namespaceAction("relay.replaced", "Unauthorized") == NamespaceAction::RelayReplaced);
  for (const char* message : {static_cast<const char*>(nullptr), "", "websocket timeout", "server unavailable",
      "unexpected unauthorized response from upstream", "token endpoint unavailable", "{\"message\":\"Unauthorized\"",
      "App device authentication failed", "Upstream authentication failed", "Authentication failed: private detail"})
    assert(native_reauth::namespaceAction("unknown", message) == NamespaceAction::Reconnect);
  const NamespaceAction priority[] = {NamespaceAction::Reconnect, NamespaceAction::Reauthenticate,
      NamespaceAction::RequireManual, NamespaceAction::RelayReplaced};
  for (size_t a = 0; a < 4; ++a) for (size_t b = 0; b < 4; ++b)
    assert(native_reauth::combineNamespaceActions(priority[a], priority[b]) == priority[a > b ? a : b]);
}

void manualDenialPreservesExplicitRecovery() {
  RetryPolicy policy;
  assert(policy.request(kTarget));
  assert(policy.begin(0, kReady));
  assert(policy.complete(Outcome::TransientFailure, 1));
  policy.requireManual(kTarget); // A clear permission refusal also stops pending retries.
  assert(policy.matches(kTarget));
  assert(policy.rejected());
  assert(!policy.inFlight());
  assert(!policy.begin(1000000, kReady));
  assert(policy.request(kTarget));
  assert(!policy.begin(2000000, kReady));
  policy.cancel(); // Existing valid manual login/profile/logout hooks own cancellation.
  assert(!policy.blocksSocket());
  assert(policy.request(kTarget));
  assert(policy.begin(2000001, kReady));
  policy.cancel();
  const Target incomplete{nullptr, "http://desktop:8748", nullptr, nullptr, false};
  policy.requireManual(incomplete);
  assert(policy.matches(incomplete));
  assert(policy.blocksSocket());
  assert(!policy.begin(2000002, kReady));
  assert(!policy.matches(kTarget)); // A newly selected target is distinguishable, even from incomplete state.
  policy.cancel();
  assert(!policy.blocksSocket());
}

void socketPreflightAndReauthIdentity() {
  assert(native_reauth::mayConnectSavedSocket(kTarget.deviceKey, true, true, true));
  assert(native_reauth::mayConnectSavedSocket(kTarget.deviceKey, true, false, false));
  assert(!native_reauth::mayConnectSavedSocket(kTarget.deviceKey, false, true, true)); // Cannot send ready/default first.
  assert(!native_reauth::mayConnectSavedSocket("", true, true, true));
  assert(!native_reauth::mayConnectSavedSocket(kTarget.deviceKey, true, false, true));
  assert(!native_reauth::mayConnectSavedSocket(kTarget.deviceKey, true, true, false));
  assert(native_reauth::sameDeviceBinding(kTarget.deviceKey, "machine|desktop|8748"));
  for (const char* other : {"another-machine|desktop|8748", "machine|web|8748", "machine|desktop|8648",
      "machine|desktop|8748suffix", "", static_cast<const char*>(nullptr)})
    assert(!native_reauth::sameDeviceBinding(kTarget.deviceKey, other));
  assert(!native_reauth::sameDeviceBinding("", ""));
  // Matching the old LAN URL does not permit a different identity to receive
  // saved credentials; the caller reports NoAttempt and retains the binding.
  RetryPolicy policy;
  assert(policy.request(kTarget));
  assert(policy.begin(1, kReady));
  assert(policy.complete(Outcome::NoAttempt, 2));
  assert(policy.matches(kTarget));
  assert(!policy.rejected());
  assert(!policy.begin(30001, kReady));
}

void manualLoginWaitsForExplicitProfile() {
  Target current = kTarget;
  current.profile = "";
  assert(!native_reauth::restoreManualProfileOnFailure(true, kTarget, current)); // Success waits for saveProfile.
  assert(native_reauth::restoreManualProfileOnFailure(false, kTarget, current)); // Failed attempt retains the original selection.
  const Target changed[] = {
    {"another|desktop|8748", current.deviceUrl, current.relayUrl, "", true},
    {current.deviceKey, "http://another:8748", current.relayUrl, "", true},
    {current.deviceKey, current.deviceUrl, "https://another-relay.test", "", true},
    {current.deviceKey, current.deviceUrl, current.relayUrl, "", false},
    {current.deviceKey, current.deviceUrl, current.relayUrl, "new-profile", true},
  };
  for (const auto& target : changed)
    assert(!native_reauth::restoreManualProfileOnFailure(false, kTarget, target));
  assert(!native_reauth::mayConnectSavedSocket(current.deviceKey, false, true, true));
  assert(native_reauth::mayConnectSavedSocket(current.deviceKey, true, true, true)); // Explicit saveProfile may connect.
}

void unavailableLanMayUseVerifiedRelay() {
  const Target lan{kTarget.deviceKey, "http://desktop:8748", "", "default", false};
  RetryPolicy retry;
  native_remote::FallbackPolicy fallback;
  const native_remote::Conditions ready{true, true, false, false, false};
  assert(retry.request(lan)); // Missing/expired LAN JWT at startup outside home.
  assert(!retry.mayYieldToRemoteFallback(true));
  assert(retry.begin(1000, kReady));
  assert(!retry.mayYieldToRemoteFallback(false)); // Never compete with an HTTP login.
  assert(retry.complete(Outcome::NoAttempt, 2000)); // LAN discovery cannot find the bound computer.
  fallback.recordLanFailure(2000);
  assert(retry.mayYieldToRemoteFallback(false));
  assert(!fallback.begin(12000, ready)); // Still requires a continuous failed series.
  assert(retry.begin(32000, kReady));
  assert(retry.complete(Outcome::TransientFailure, 33000));
  fallback.recordLanFailure(33000);
  assert(retry.mayYieldToRemoteFallback(false));
  assert(fallback.begin(33000, ready));
  retry.cancel(); // The caller stages only the fingerprint-verified same machine.
  assert(!retry.blocksSocket());
  assert(fallback.failed(34000));
  assert(retry.request(lan)); // Failed staging restores the original retry target.
  assert(retry.matches(lan));
  assert(retry.mayYieldToRemoteFallback(false));
  retry.requireManual(lan);
  assert(!retry.mayYieldToRemoteFallback(false));
  retry.cancel();
  assert(retry.request(lan));
  assert(retry.begin(35000, kReady));
  assert(retry.complete(Outcome::Rejected, 36000));
  assert(!retry.mayYieldToRemoteFallback(false)); // A real refusal cannot be bypassed remotely.
  retry.cancel();
  assert(retry.request(kTarget));
  assert(!retry.mayYieldToRemoteFallback(false)); // A remote target is never a LAN fallback source.
}

void staleAudioCannotHoldReauthentication() {
  RetryPolicy retry;
  AudioStreamQueue buffers;
  AudioStreamClaim first, second;
  assert(buffers.tryClaim(first) && buffers.submit(first, 1024));
  assert(buffers.tryClaim(second) && buffers.submit(second, 1024));
  const auto before = buffers.stats();
  assert(before.in_flight == 2 && before.released == 0);
  assert(retry.request(kTarget));
  assert(retry.request(kTarget)); // Repeated auth.invalid does not create another login.
  assert(retry.blocksSocket()); // All audio entry points remain blocked in this same packet batch.
  const Conditions queued{true, true, false};
  assert(!retry.begin(10, queued));
  // Firmware joins the SDK consumer before dropping stale queues. The queue
  // contract must discard, rather than report successful buffer releases.
  buffers.resetAfterJoin();
  const auto after = buffers.stats();
  assert(after.in_flight == 0 && after.free_slots == kAudioStreamSlotCount);
  assert(after.discarded_after_join == 2 && after.released == 0);
  assert(!buffers.releaseExactPointer(first.data));
  assert(!buffers.releaseExactPointer(second.data));
  assert(retry.begin(11, kReady));
  assert(retry.blocksSocket());
  assert(!retry.begin(12, kReady));
  assert(retry.complete(Outcome::Success, 13));
  assert(!retry.blocksSocket());
  // A fresh Agent ACK, not an unconfirmed transport, owns subsequent audio.
  NativeAgentSelection selection;
  assert(selection.request(0));
  assert(!selection.ready());
  assert(selection.acknowledge(0, 1));
  assert(selection.ready());
  AudioStreamClaim fresh;
  assert(buffers.tryClaim(fresh) && buffers.submit(fresh, 1024));
  assert(buffers.releaseExactPointer(fresh.data));
  assert(buffers.stats().released == 1 && buffers.stats().in_flight == 0);
}

void stagedRejectionRestoresManualTarget() {
  const Target lan{kTarget.deviceKey, "http://desktop:8748", "", "default", false};
  const native_remote::Conditions ready{true, true, false, false, false};
  for (int code : {401, 403, -1, -11, 0, 500, 502, 503}) {
    native_remote::FallbackPolicy fallback;
    RetryPolicy retry;
    fallback.recordLanFailure(0);
    fallback.recordLanFailure(5000);
    assert(fallback.begin(10000, ready));
    const Outcome result = loginOutcome(code != 0, false, code);
    assert(fallback.failed(11000)); // Roll back before attaching a refusal to the original target.
    if (result == Outcome::Rejected) retry.requireManual(lan);
    if (code == 401 || code == 403) {
      assert(retry.matches(lan) && retry.rejected() && retry.blocksSocket());
      assert(!retry.mayYieldToRemoteFallback(false));
    } else {
      assert(!retry.blocksSocket());
      assert(!fallback.begin(40999, ready));
      assert(fallback.begin(41000, ready));
    }
  }
  // Stale HTTP refusals cannot classify a discovery miss as a new rejection.
  assert(loginOutcome(false, false, 401) == Outcome::NoAttempt);
  assert(loginOutcome(false, false, 403) == Outcome::NoAttempt);
}

void stagedReplacementCannotRestartAutomatically() {
  const Target restored{kTarget.deviceKey, "http://desktop:8748", "", "default", false};
  RetryPolicy retry;
  retry.requireManual(restored);
  assert(retry.matches(restored) && retry.rejected() && retry.blocksSocket());
  assert(!retry.mayYieldToRemoteFallback(false));
  // Firmware persists relay_replaced only after restoring this original target.
  const BootstrapState restarted{true, true, true, true, true, true, true};
  assert(native_reauth::bootstrapAction(restarted) == BootstrapAction::RequireManual);
  const Conditions replaced{true, false, true};
  assert(!retry.begin(1000000, replaced));
  assert(!retry.request(kTarget) || !retry.begin(1000001, replaced));
}
} // namespace

int main() {
  classifyCurrentAttempt();
  pauseAndBackoff();
  noAttemptIsRetryable();
  deniedRequiresExplicitAction();
  conditionsNeverConsumeAttempts();
  targetBinding();
  cancellationAndSuccessReset();
  timerWraparound();
  newInvalidEventAfterLoginSuccess();
  bootstrapDoesNotRotateCachedToken();
  completeBindingSyntax();
  namespaceErrorClassification();
  manualDenialPreservesExplicitRecovery();
  socketPreflightAndReauthIdentity();
  manualLoginWaitsForExplicitProfile();
  unavailableLanMayUseVerifiedRelay();
  staleAudioCannotHoldReauthentication();
  stagedRejectionRestoresManualTarget();
  stagedReplacementCannotRestartAutomatically();
  std::puts("MCU reauth: 19 scenarios passed (retry, identity, cached startup, namespace errors, staged rejection/replacement, stale audio cleanup)");
}
