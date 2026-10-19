#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace native_remote {

inline std::string canonicalPublicKey(const char* publicKey) {
  if (!publicKey || !*publicKey) return {};
  std::string canonical;
  canonical.reserve(std::strlen(publicKey) + 1);
  for (const char* current = publicKey; *current; ++current) {
    if (*current == '\r' && current[1] == '\n') continue;
    canonical.push_back(*current);
  }
  while (!canonical.empty() && canonical.back() == '\n') canonical.pop_back();
  if (!canonical.empty()) canonical.push_back('\n');
  return canonical;
}

struct Identity {
  const char* machineId;
  const char* endpointKind;
  uint16_t port;
};

inline bool sameNonemptyText(const char* left, const char* right) {
  return left && *left && right && *right && std::strcmp(left, right) == 0;
}

// The caller derives this ID from the candidate's original PEM bytes first,
// then canonicalPublicKey() when transport line-ending changes require it:
// "hwui_" + base64url(SHA256(publicKey)).substr(0, 32).
// Validate this binding before sending any saved credentials to a candidate.
inline bool matchesBinding(const Identity& bound, const Identity& candidate,
                           const char* derivedMachineId) {
  return bound.port != 0 && bound.port == candidate.port &&
      sameNonemptyText(bound.machineId, candidate.machineId) &&
      sameNonemptyText(bound.machineId, derivedMachineId) &&
      sameNonemptyText(bound.endpointKind, candidate.endpointKind);
}

struct Conditions {
  bool wifiReady;
  bool hasCredentials;
  bool busy;
  bool remoteSelected;
  bool relayReplaced;
};

inline bool requestExpired(uint32_t now, bool pending, uint32_t startedAt, uint32_t timeout) {
  return pending && static_cast<uint32_t>(now - startedAt) >= timeout;
}

// TCP staying open does not prove either the native namespace or desktop is
// alive. Bound both waits, using Engine.IO's advertised heartbeat budget.
class SocketLiveness {
 public:
  void connected(uint32_t now) {
    connectedAtMs_ = lastPacketAtMs_ = now;
    heartbeatBudgetMs_ = 50000U;
  }
  void received(uint32_t now) { lastPacketAtMs_ = now; }
  void heartbeat(uint32_t interval, uint32_t timeout) {
    if (!interval || !timeout) return;
    const uint64_t budget = static_cast<uint64_t>(interval) + timeout + 5000U;
    // Keep the unsigned elapsed comparison unambiguous across millis() wrap.
    heartbeatBudgetMs_ = static_cast<uint32_t>(budget > 0x7fffffffU ? 0x7fffffffU : budget);
  }
  bool namespaceExpired(uint32_t now, bool ready, uint32_t timeout) const {
    return requestExpired(now, !ready, connectedAtMs_, timeout);
  }
  bool heartbeatExpired(uint32_t now) const {
    return static_cast<uint32_t>(now - lastPacketAtMs_) >= heartbeatBudgetMs_;
  }
 private:
  uint32_t connectedAtMs_ = 0, lastPacketAtMs_ = 0, heartbeatBudgetMs_ = 50000U;
};

class FallbackPolicy {
 public:
  void recordLanFailure(uint32_t now) {
    if (lanFailures_ == 0) firstLanFailureMs_ = now;
    if (lanFailures_ < 2) ++lanFailures_;
  }

  void clearLanFailures() {
    lanFailures_ = 0;
    firstLanFailureMs_ = 0;
  }

  bool mayBegin(uint32_t now, const Conditions& conditions) const {
    if (inFlight_ || remoteConfirmed_ || !conditions.wifiReady ||
        !conditions.hasCredentials || conditions.busy ||
        conditions.remoteSelected || conditions.relayReplaced ||
        lanFailures_ < 2 ||
        static_cast<uint32_t>(now - firstLanFailureMs_) < 10000U ||
        (remoteFailures_ != 0 &&
         static_cast<uint32_t>(now - lastRemoteFailureMs_) < retryDelayMs())) {
      return false;
    }
    return true;
  }

  bool begin(uint32_t now, const Conditions& conditions) {
    if (!mayBegin(now, conditions)) return false;
    inFlight_ = true;
    return true;
  }

  bool failed(uint32_t now) {
    if (!inFlight_) return false;
    inFlight_ = false;
    if (remoteFailures_ < 3) ++remoteFailures_;
    lastRemoteFailureMs_ = now;
    return true;
  }

  // Confirm only after the selected remote namespace and Agent acknowledge.
  bool succeeded() {
    if (!inFlight_) return false;
    inFlight_ = false;
    remoteConfirmed_ = true;
    remoteFailures_ = 0;
    return true;
  }

  // Network changes may discard clocks, but cannot undo a successful remote
  // selection and start competing LAN/remote attempts.
  void reset() {
    clearLanFailures();
    inFlight_ = false;
    remoteFailures_ = 0;
    lastRemoteFailureMs_ = 0;
  }

  // Call only for an explicit target/mode selection, including restoration of
  // the saved selected mode. Ordinary reconnects should use reset().
  void selectMode(bool remoteSelected) {
    reset();
    remoteConfirmed_ = remoteSelected;
  }

  uint32_t retryDelayMs() const {
    return remoteFailures_ == 0 ? 0U : remoteFailures_ == 1 ? 30000U :
        remoteFailures_ == 2 ? 60000U : 120000U;
  }

  bool inFlight() const { return inFlight_; }
  bool remoteConfirmed() const { return remoteConfirmed_; }

 private:
  uint32_t firstLanFailureMs_ = 0;
  uint32_t lastRemoteFailureMs_ = 0;
  uint8_t lanFailures_ = 0;
  uint8_t remoteFailures_ = 0;
  bool inFlight_ = false;
  bool remoteConfirmed_ = false;
};

}  // namespace native_remote
