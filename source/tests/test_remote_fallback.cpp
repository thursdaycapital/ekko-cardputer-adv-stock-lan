#include "RemoteFallback.h"
#include "WifiRecovery.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>

using native_remote::Conditions;
using native_remote::FallbackPolicy;
using native_remote::Identity;
using native_remote::matchesBinding;
using native_remote::canonicalPublicKey;

namespace {

const char kMachine[] = "hwui_0123456789abcdefghijklmnopqrstuv";
const char kOtherMachine[] = "hwui_1123456789abcdefghijklmnopqrstuv";
const Conditions kReady = {true, true, false, false, false};

void publicKeyCanonicalization() {
  const char pem[] = "-----BEGIN PUBLIC KEY-----\nexample\n-----END PUBLIC KEY-----\n";
  assert(canonicalPublicKey(pem) == pem);
  assert(canonicalPublicKey("-----BEGIN PUBLIC KEY-----\nexample\n-----END PUBLIC KEY-----") == pem);
  assert(canonicalPublicKey("-----BEGIN PUBLIC KEY-----\r\nexample\r\n-----END PUBLIC KEY-----\r\n") == pem);
  assert(canonicalPublicKey("-----BEGIN PUBLIC KEY-----\nexample\n-----END PUBLIC KEY-----\n\n\n") == pem);
  assert(canonicalPublicKey("-----BEGIN PUBLIC KEY-----\r\nexample\n-----END PUBLIC KEY-----\r\n\n\r\n") == pem);
  assert(canonicalPublicKey("").empty());
  assert(canonicalPublicKey(nullptr).empty());
  assert(canonicalPublicKey("\n\r\n\n").empty());
  assert(canonicalPublicKey("  first  \r\n\r\nsecond \rinside\n  last  ") ==
         "  first  \n\nsecond \rinside\n  last  \n");
  assert(canonicalPublicKey(canonicalPublicKey(pem).c_str()) == pem);
}

void identityBinding() {
  const Identity bound = {kMachine, "desktop", 8748};
  assert(matchesBinding(bound, bound, kMachine));
  assert(!matchesBinding(bound, {kOtherMachine, "desktop", 8748}, kOtherMachine));
  assert(!matchesBinding(bound, bound, kOtherMachine));
  assert(!matchesBinding(bound, {kOtherMachine, "desktop", 8748}, kMachine));
  assert(!matchesBinding(bound, {kMachine, "web", 8748}, kMachine));
  assert(!matchesBinding(bound, {kMachine, "desktop", 8648}, kMachine));
  assert(!matchesBinding({kMachine, "desktop", 0}, {kMachine, "desktop", 0}, kMachine));
  for (const char* empty : {static_cast<const char*>(nullptr), ""}) {
    assert(!matchesBinding({empty, "desktop", 8748}, bound, kMachine));
    assert(!matchesBinding(bound, {empty, "desktop", 8748}, kMachine));
    assert(!matchesBinding(bound, bound, empty));
    assert(!matchesBinding({kMachine, empty, 8748}, bound, kMachine));
    assert(!matchesBinding(bound, {kMachine, empty, 8748}, kMachine));
  }
  assert(!matchesBinding(bound, {"hwui_0123456789ABCDEFGHIJKLMNOPQRSTUV", "desktop", 8748}, kMachine));
  assert(!matchesBinding(bound, {kMachine, "Desktop", 8748}, kMachine));
}

void failureThreshold() {
  FallbackPolicy policy;
  assert(!policy.begin(10000, kReady));
  policy.recordLanFailure(0);
  assert(!policy.begin(10000, kReady)); // One old failure is not a continuous failure series.
  policy.recordLanFailure(5000);
  assert(!policy.begin(9999, kReady));
  assert(policy.begin(10000, kReady));
  assert(policy.inFlight());
  assert(!policy.begin(10001, kReady));
  assert(policy.failed(10001));
  assert(!policy.failed(10002)); // Duplicate completion cannot advance the backoff.
  assert(policy.retryDelayMs() == 30000);
}

void readOnlyEligibility() {
  FallbackPolicy policy;
  policy.recordLanFailure(0);
  policy.recordLanFailure(5000);
  const FallbackPolicy& view = policy;
  assert(!view.mayBegin(9999, kReady));
  assert(view.mayBegin(10000, kReady));
  assert(view.mayBegin(10001, kReady));
  assert(!view.inFlight());
  assert(view.retryDelayMs() == 0);
  Conditions actual = kReady;
  actual.hasCredentials = false;
  assert(!policy.begin(10001, actual));
  assert(!policy.inFlight());
  assert(view.mayBegin(10001, kReady));
  assert(policy.begin(10001, kReady));
  assert(!view.mayBegin(10002, kReady));
  assert(policy.failed(10003));
  assert(!view.mayBegin(40002, kReady));
  assert(view.mayBegin(40003, kReady));
  assert(!view.inFlight());
}

void gating() {
  const Conditions blocked[] = {
    {false, true, false, false, false},
    {true, false, false, false, false},
    {true, true, true, false, false},
    {true, true, false, true, false},
    {true, true, false, false, true},
  };
  for (const Conditions& conditions : blocked) {
    FallbackPolicy policy;
    policy.recordLanFailure(0);
    policy.recordLanFailure(5000);
    assert(!policy.begin(10000, conditions));
    assert(!policy.inFlight());
    assert(policy.retryDelayMs() == 0);
    assert(policy.begin(10000, kReady)); // Blocking does not consume the eligible attempt.
  }
}

void retryBackoff() {
  FallbackPolicy policy;
  policy.recordLanFailure(0);
  policy.recordLanFailure(5000);
  assert(policy.retryDelayMs() == 0);
  assert(policy.begin(10000, kReady));
  uint32_t failedAt = 11000;
  for (const uint32_t expected : {30000U, 60000U, 120000U, 120000U, 120000U}) {
    assert(policy.failed(failedAt));
    assert(policy.retryDelayMs() == expected);
    assert(!policy.begin(failedAt + expected - 1, kReady));
    assert(policy.begin(failedAt + expected, kReady));
    failedAt += expected + 1000;
  }
}

void wraparound() {
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  FallbackPolicy policy;
  policy.recordLanFailure(maximum - 4999U);
  policy.recordLanFailure(maximum);
  assert(!policy.begin(4999U, kReady));
  assert(policy.begin(5000U, kReady));
  assert(policy.failed(6000U));
  assert(!policy.begin(35999U, kReady));
  assert(policy.begin(36000U, kReady));

  policy.reset();
  policy.recordLanFailure(maximum - 20000U);
  policy.recordLanFailure(maximum - 15000U);
  assert(policy.begin(maximum - 10000U, kReady));
  assert(policy.failed(maximum - 9999U));
  assert(!policy.begin(19999U, kReady));
  assert(policy.begin(20000U, kReady));
}

void resetAndSuccess() {
  FallbackPolicy policy;
  assert(!policy.failed(0));
  assert(!policy.succeeded());
  assert(!policy.remoteConfirmed());
  policy.recordLanFailure(0);
  policy.recordLanFailure(5000);
  assert(policy.begin(10000, kReady));
  assert(policy.failed(10001));
  policy.reset(); // A network change starts a fresh continuous LAN failure clock.
  assert(policy.retryDelayMs() == 0);
  assert(!policy.begin(40001, kReady));
  policy.recordLanFailure(40001);
  policy.recordLanFailure(45001);
  assert(!policy.begin(50000, kReady));
  assert(policy.begin(50001, kReady));
  assert(policy.succeeded());
  assert(policy.remoteConfirmed());
  assert(!policy.inFlight());
  assert(!policy.failed(50002));
  assert(!policy.begin(1000000, kReady));
  policy.reset();
  policy.recordLanFailure(1000000);
  policy.recordLanFailure(1005000);
  assert(policy.remoteConfirmed());
  assert(!policy.begin(1010000, kReady)); // Reconnect/reset never reverses remote selection.
  policy.selectMode(false); // Only an explicit LAN choice may unlock fallback again.
  assert(!policy.remoteConfirmed());
  policy.recordLanFailure(2000000);
  policy.recordLanFailure(2005000);
  assert(policy.begin(2010000, kReady));
  policy.selectMode(true);
  assert(!policy.inFlight());
  assert(policy.remoteConfirmed());
  assert(!policy.succeeded());
  policy.recordLanFailure(3000000);
  policy.recordLanFailure(3005000);
  assert(!policy.begin(3010000, kReady));
}

void interruptedFailureSeries() {
  FallbackPolicy policy;
  policy.recordLanFailure(0);
  policy.recordLanFailure(5000);
  policy.clearLanFailures(); // An intervening LAN connection breaks continuity.
  assert(!policy.begin(10000, kReady));
  policy.recordLanFailure(15000);
  policy.recordLanFailure(16000);
  assert(!policy.begin(24999, kReady));
  assert(policy.begin(25000, kReady));
}

void nativeHandshakeHasDeadlines() {
  native_remote::SocketLiveness socket;
  socket.connected(1000);
  assert(!socket.namespaceExpired(8999, false, 8000));
  socket.received(8999); // An Engine.IO ping is not a native namespace ACK.
  assert(socket.namespaceExpired(9000, false, 8000));
  assert(!socket.namespaceExpired(100000, true, 8000));
  assert(!native_remote::requestExpired(29999, true, 10000, 20000));
  assert(native_remote::requestExpired(30000, true, 10000, 20000));
  assert(!native_remote::requestExpired(100000, false, 10000, 20000));

  // A replacement connection has its own handshake deadline.
  socket.connected(100000);
  assert(!socket.namespaceExpired(119999, false, 20000));
  assert(socket.namespaceExpired(120000, false, 20000));
}

void engineHeartbeatAndTimerWrap() {
  native_remote::SocketLiveness socket;
  socket.connected(1000);
  assert(!socket.heartbeatExpired(50999));
  assert(socket.heartbeatExpired(51000));
  socket.heartbeat(25000, 20000);
  socket.received(50000);
  assert(!socket.heartbeatExpired(99999));
  assert(socket.heartbeatExpired(100000));
  socket.received(100000); // Duplicate traffic cannot consume a retry attempt.
  socket.received(100000);
  assert(!socket.heartbeatExpired(149999));
  socket.heartbeat(0, 1); // Incomplete/invalid Engine.IO data keeps the old budget.
  assert(socket.heartbeatExpired(150000));
  socket.heartbeat(1, 1);
  assert(!socket.heartbeatExpired(105001));
  assert(socket.heartbeatExpired(105002));

  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  socket.connected(maximum - 9999);
  assert(!socket.namespaceExpired(9999, false, 20000));
  assert(socket.namespaceExpired(10000, false, 20000));
  assert(!socket.heartbeatExpired(39999));
  assert(socket.heartbeatExpired(40000));
  assert(!native_remote::requestExpired(9999, true, maximum - 9999, 20000));
  assert(native_remote::requestExpired(10000, true, maximum - 9999, 20000));
  socket.heartbeat(maximum, maximum); // No overflow into an immediate disconnect.
  assert(!socket.heartbeatExpired(40001));
}

void desktopOfflineDoesNotCountAsReady() {
  FallbackPolicy policy;
  policy.recordLanFailure(1000);
  // A second TCP + namespace connection without Agent ACK must not clear this
  // failure series. It times out and becomes eligible for verified fallback.
  native_remote::SocketLiveness socket;
  socket.connected(5000);
  assert(!socket.namespaceExpired(13000, true, 8000));
  assert(native_remote::requestExpired(13000, true, 5000, 8000));
  policy.recordLanFailure(13000);
  assert(policy.begin(13000, kReady));
  assert(!policy.begin(13001, kReady));
  assert(policy.failed(13002));
  assert(!policy.begin(43001, kReady));
  assert(policy.begin(43002, kReady));
  assert(policy.succeeded()); // Desktop returned and acknowledged the Agent.
  assert(!policy.begin(1000000, kReady));
}

void automaticWifiRecoveryPreservesUserSetup() {
  native_wifi::SavedNetworkRetry retry;
  retry.setup("startup_wifi_unavailable", 1000, true);
  assert(retry.scheduled());
  assert(!retry.begin(30999, false));
  assert(!retry.begin(31000, true)); // User/audio activity does not consume a retry.
  assert(retry.begin(31000, false));
  assert(!retry.begin(31001, false));
  retry.failed(32000);
  assert(retry.delayMs() == 60000);
  retry.failed(32001); // Repeated failure notifications cannot double the backoff.
  assert(retry.delayMs() == 60000);
  retry.setup("wifi_reconnect_failed", 32002, true); // Re-opening AP preserves its deadline.
  assert(!retry.begin(91999, false));
  assert(retry.begin(92000, false));
  retry.failed(93000);
  assert(retry.delayMs() == 120000);
  assert(retry.begin(213000, false));
  retry.failed(214000);
  assert(retry.delayMs() == 120000);
  retry.connected();
  assert(!retry.scheduled() && !retry.begin(1000000, false));

  for (const char* reason : {"user_reconfigure", "user_forget_all", "forced_setup", "unknown",
      static_cast<const char*>(nullptr)}) {
    retry.setup("wifi_reconnect_failed", 1, true);
    assert(retry.scheduled());
    retry.setup(reason, 2, true);
    assert(!retry.scheduled() && !retry.begin(1000000, false));
  }
  retry.setup("startup_wifi_unavailable", 1, false);
  assert(!retry.scheduled()); // Fresh/forgotten device does not scan forever with no credentials.

  retry.setup("wifi_reconnect_failed", 1000, true);
  retry.cancel(); // A valid manual Wi-Fi submission supersedes the automatic retry.
  retry.failed(2000); // Its failed connection cannot re-arm the previous saved network.
  assert(!retry.scheduled() && !retry.begin(1000000, false));
  retry.setup("wifi_reconnect_failed", 1000, true);
  assert(retry.begin(31000, false));
  retry.cancel();
  retry.failed(32000);
  assert(!retry.scheduled() && !retry.begin(1000000, false));

  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  retry.setup("wifi_reconnect_failed", maximum - 9999, true);
  assert(!retry.begin(19999, false));
  assert(retry.begin(20000, false));
  retry.failed(20001);
  assert(!retry.begin(80000, false));
  assert(retry.begin(80001, false));
}

}  // namespace

int main() {
  publicKeyCanonicalization();
  identityBinding();
  failureThreshold();
  readOnlyEligibility();
  gating();
  retryBackoff();
  wraparound();
  resetAndSuccess();
  interruptedFailureSeries();
  nativeHandshakeHasDeadlines();
  engineHeartbeatAndTimerWrap();
  desktopOfflineDoesNotCountAsReady();
  automaticWifiRecoveryPreservesUserSetup();
  std::puts("Remote fallback: 13 scenarios passed (identity, backoff, native handshakes, Engine.IO liveness, Wi-Fi/desktop recovery, rollover)");
}
