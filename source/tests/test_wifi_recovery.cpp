#include "WifiRecovery.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
  native_wifi::SavedNetworkRetry retry;
  const char* actions[] = {"manual_setup", "forgot_network", "", nullptr};
  for (const char* explicitAction : actions) {
    retry.setup(explicitAction, 10, true);
    assert(!retry.scheduled() && !retry.begin(40000, false));
  }
  retry.setup("startup_wifi_unavailable", 100, false);
  assert(!retry.scheduled());
  retry.setup("startup_wifi_unavailable", 100, true);
  assert(retry.scheduled() && retry.delayMs() == 30000);
  assert(!retry.begin(30099, false) && !retry.begin(30100, true));
  assert(retry.begin(30100, false) && !retry.begin(30100, false));
  retry.failed(30110);
  assert(retry.delayMs() == 60000 && !retry.begin(90109, false));
  assert(retry.begin(90110, false));
  retry.failed(90111);
  assert(retry.delayMs() == 120000 && retry.begin(210111, false));
  retry.failed(210112);
  assert(retry.delayMs() == 120000);
  retry.connected();
  assert(!retry.scheduled() && retry.delayMs() == 30000);
  // Repeated setup notifications must not keep postponing the due time.
  retry.setup("wifi_reconnect_failed", 500, true);
  retry.setup("wifi_reconnect_failed", 30000, true);
  assert(retry.begin(30500, false));
  retry.cancel();
  assert(!retry.scheduled());
  // millis() wraps; unsigned subtraction and signed deadline comparison agree.
  const uint32_t start = UINT32_MAX - 20000;
  retry.setup("wifi_reconnect_failed", start, true);
  assert(!retry.begin(start + 29999U, false));
  assert(retry.begin(start + 30000U, false));
  std::puts("Saved Wi-Fi retry: explicit setup, no credentials, busy/inflight, capped backoff and clock wrap passed");
}
