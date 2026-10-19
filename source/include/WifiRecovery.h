#pragma once

#include <cstdint>
#include <cstring>

namespace native_wifi {

// Only an automatic network-loss/setup state retries stored STA credentials.
// Explicit provisioning/forget actions stay under the user's control.
class SavedNetworkRetry {
 public:
  void setup(const char* reason, uint32_t now, bool hasSavedNetworks) {
    const bool automatic = reason &&
        (std::strcmp(reason, "startup_wifi_unavailable") == 0 ||
         std::strcmp(reason, "wifi_reconnect_failed") == 0);
    if (!automatic || !hasSavedNetworks) { connected(); return; }
    if (!scheduled_) {
      scheduled_ = true;
      dueAtMs_ = now + delayMs_;
    }
  }
  bool begin(uint32_t now, bool busy) {
    if (!scheduled_ || inFlight_ || busy || static_cast<int32_t>(now - dueAtMs_) < 0) return false;
    inFlight_ = true;
    return true;
  }
  void failed(uint32_t now) {
    if (!inFlight_) return;
    inFlight_ = false;
    delayMs_ = delayMs_ < 120000U ? delayMs_ * 2U : 120000U;
    dueAtMs_ = now + delayMs_;
  }
  void connected() {
    scheduled_ = inFlight_ = false;
    delayMs_ = 30000U;
    dueAtMs_ = 0;
  }
  void cancel() { connected(); }
  bool scheduled() const { return scheduled_; }
  uint32_t delayMs() const { return delayMs_; }
 private:
  uint32_t dueAtMs_ = 0, delayMs_ = 30000U;
  bool scheduled_ = false, inFlight_ = false;
};

} // namespace native_wifi
