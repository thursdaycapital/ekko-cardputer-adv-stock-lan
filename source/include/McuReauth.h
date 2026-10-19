#pragma once

#include <cstdint>
#include <string>

namespace native_reauth {

enum class Outcome { NoAttempt, TransientFailure, Rejected, Success };

enum class BootstrapAction { WaitForWifi, ResumeSocket, QueueReauth, RequireManual };
struct BootstrapState {
  bool wifiReady;
  bool bindingComplete;
  bool hasProfile;
  bool hasToken;
  bool sourceConsistent;
  bool hasSavedCredentials;
  bool relayReplaced;
};

inline bool completeDeviceBinding(const char* rawKey) {
  const std::string key = rawKey ? rawKey : "";
  const size_t kind = key.find('|'), port = kind == std::string::npos ? kind : key.find('|', kind + 1);
  if (kind == std::string::npos || !kind || port == std::string::npos || port == kind + 1 ||
      port + 1 == key.size() || key.find('|', port + 1) != std::string::npos) return false;
  uint32_t number = 0;
  for (size_t i = port + 1; i < key.size(); ++i) {
    if (key[i] < '0' || key[i] > '9') return false;
    number = number * 10U + static_cast<uint32_t>(key[i] - '0');
    if (number > 65535U) return false;
  }
  return number != 0;
}

inline bool sameDeviceBinding(const char* savedKey, const char* candidateKey) {
  return completeDeviceBinding(savedKey) && completeDeviceBinding(candidateKey) &&
      std::string(savedKey) == candidateKey;
}

inline bool mayConnectSavedSocket(const char* key, bool hasProfile, bool savedRemote, bool hasRelayUrl) {
  return completeDeviceBinding(key) && hasProfile && savedRemote == hasRelayUrl;
}

inline BootstrapAction bootstrapAction(const BootstrapState& state) {
  if (!state.wifiReady) return BootstrapAction::WaitForWifi;
  if (state.relayReplaced || !state.bindingComplete || !state.hasProfile || !state.sourceConsistent)
    return BootstrapAction::RequireManual;
  if (state.hasToken) return BootstrapAction::ResumeSocket;
  return state.hasSavedCredentials ? BootstrapAction::QueueReauth : BootstrapAction::RequireManual;
}

inline std::string normalizedErrorText(const char* value) {
  std::string result = value ? value : "";
  for (char& byte : result) if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
  const size_t first = result.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return result.substr(first, result.find_last_not_of(" \t\r\n") - first + 1);
}

enum class NamespaceAction { Reconnect, Reauthenticate, RequireManual, RelayReplaced };

// Explicit protocol errors only. The pinned official Studio /chat-run emits
// "Authentication failed" for rejected user JWTs; App-device authentication
// errors are a different flow and must not submit saved LAN credentials.
inline NamespaceAction namespaceAction(const char* rawCode, const char* rawMessage) {
  const std::string code = normalizedErrorText(rawCode), message = normalizedErrorText(rawMessage);
  if (code == "relay.replaced" || code == "relay_replaced" || message == "relay.replaced")
    return NamespaceAction::RelayReplaced;
  if (code == "device_code_not_allowed" || code == "unofficial_device_code" || code == "profile_access_denied" ||
      message == "device_code_not_allowed" || message == "profile access denied" ||
      message.find("非官方设备码") != std::string::npos)
    return NamespaceAction::RequireManual;
  if (code == "unauthorized" || code == "invalid_token" || code == "token_invalid" || code == "token_expired" ||
      code == "jwt_expired" || code == "auth.invalid" || message == "unauthorized" ||
      message == "authentication failed" || message == "invalid token" ||
      message == "token expired" || message == "jwt expired")
    return NamespaceAction::Reauthenticate;
  return NamespaceAction::Reconnect;
}

inline NamespaceAction combineNamespaceActions(NamespaceAction outer, NamespaceAction data) {
  if (outer == NamespaceAction::RelayReplaced || data == NamespaceAction::RelayReplaced) return NamespaceAction::RelayReplaced;
  if (outer == NamespaceAction::RequireManual || data == NamespaceAction::RequireManual) return NamespaceAction::RequireManual;
  if (outer == NamespaceAction::Reauthenticate || data == NamespaceAction::Reauthenticate) return NamespaceAction::Reauthenticate;
  return NamespaceAction::Reconnect;
}

// A previous HTTP result must never classify a discovery/credential failure.
inline Outcome loginOutcome(bool attempted, bool succeeded, int httpCode) {
  if (!attempted) return Outcome::NoAttempt;
  if (succeeded && httpCode >= 200 && httpCode < 300) return Outcome::Success;
  if (httpCode == 401 || httpCode == 403) return Outcome::Rejected;
  return Outcome::TransientFailure;
}

struct Target {
  const char* deviceKey;
  const char* deviceUrl;
  const char* relayUrl;
  const char* profile;
  bool remote;
};

inline bool restoreManualProfileOnFailure(bool loginSucceeded, const Target& previous, const Target& current) {
  if (loginSucceeded || (current.profile && *current.profile)) return false;
  return std::string(previous.deviceKey ? previous.deviceKey : "") == (current.deviceKey ? current.deviceKey : "") &&
      std::string(previous.deviceUrl ? previous.deviceUrl : "") == (current.deviceUrl ? current.deviceUrl : "") &&
      std::string(previous.relayUrl ? previous.relayUrl : "") == (current.relayUrl ? current.relayUrl : "") &&
      previous.remote == current.remote;
}

struct Conditions {
  bool wifiReady;
  bool busy;
  bool relayReplaced;
};

class RetryPolicy {
 public:
  bool request(const Target& target) {
    if (!nonempty(target.deviceKey) || !nonempty(target.deviceUrl) || !nonempty(target.profile)) return false;
    if (active_ && matches(target)) return true; // Repeated auth.invalid cannot reset the backoff/rejection.
    cancel();
    deviceKey_ = target.deviceKey;
    deviceUrl_ = target.deviceUrl;
    relayUrl_ = text(target.relayUrl);
    profile_ = target.profile;
    remote_ = target.remote;
    active_ = true;
    return true;
  }

  bool matches(const Target& target) const {
    return active_ && deviceKey_ == text(target.deviceKey) && deviceUrl_ == text(target.deviceUrl) &&
        relayUrl_ == text(target.relayUrl) && profile_ == text(target.profile) && remote_ == target.remote;
  }

  bool mayBegin(uint32_t now, const Conditions& conditions) const {
    return active_ && !rejected_ && !inFlight_ && conditions.wifiReady && !conditions.busy &&
        !conditions.relayReplaced &&
        (failures_ == 0 || static_cast<uint32_t>(now - failedAtMs_) >= retryDelayMs());
  }

  bool begin(uint32_t now, const Conditions& conditions) {
    if (!mayBegin(now, conditions)) return false;
    inFlight_ = true;
    return true;
  }

  bool complete(Outcome outcome, uint32_t now) {
    if (!inFlight_) return false;
    inFlight_ = false;
    if (outcome == Outcome::Success) {
      cancel();
    } else if (outcome == Outcome::Rejected) {
      rejected_ = true;
    } else {
      if (failures_ < 3) ++failures_;
      failedAtMs_ = now;
    }
    return true;
  }

  void requireManual(const Target& target) {
    cancel();
    deviceKey_ = text(target.deviceKey); deviceUrl_ = text(target.deviceUrl);
    relayUrl_ = text(target.relayUrl); profile_ = text(target.profile); remote_ = target.remote;
    active_ = rejected_ = true;
  }

  // An explicit login/logout/target selection cancels retries, never credentials
  // or a relay_replaced restriction. Those are owned by the caller.
  void cancel() {
    active_ = rejected_ = inFlight_ = false;
    failures_ = 0;
    failedAtMs_ = 0;
    deviceKey_.clear(); deviceUrl_.clear(); relayUrl_.clear(); profile_.clear();
    remote_ = false;
  }

  uint32_t retryDelayMs() const {
    return failures_ == 0 ? 0U : failures_ == 1 ? 30000U : failures_ == 2 ? 60000U : 120000U;
  }
  bool active() const { return active_; }
  bool rejected() const { return rejected_; }
  bool inFlight() const { return inFlight_; }
  bool blocksSocket() const { return active_; }
  // A temporarily unreachable saved LAN endpoint must not prevent finding the
  // same verified machine over the official relay. Permission refusals and an
  // ongoing HTTP login never yield to an automatic transport change.
  bool mayYieldToRemoteFallback(bool remoteSelected) const {
    return active_ && !remoteSelected && !remote_ && !rejected_ && !inFlight_;
  }

 private:
  static bool nonempty(const char* value) { return value && *value; }
  static const char* text(const char* value) { return value ? value : ""; }
  std::string deviceKey_, deviceUrl_, relayUrl_, profile_;
  uint32_t failedAtMs_ = 0;
  uint8_t failures_ = 0;
  bool active_ = false, rejected_ = false, inFlight_ = false, remote_ = false;
};

} // namespace native_reauth
