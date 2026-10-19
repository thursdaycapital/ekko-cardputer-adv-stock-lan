#pragma once

#include <WiFiClientSecure.h>

namespace native_tls {

// Arduino-ESP32's pinned ESP-IDF build includes the full root bundle in
// libmbedtls.a. Referencing it links the read-only trust anchors into flash;
// the Arduino adapter only allocates an index of root pointers in RAM.
extern const uint8_t kSdkRootBundle[] asm("_binary_x509_crt_bundle_start");

inline void configureTrust(WiFiClientSecure& client) {
  // Never call setInsecure() on these clients: in Arduino-ESP32 2.0.17 this
  // setter does not clear a previous insecure flag. A failed verification
  // must remain a connection failure, without an insecure retry.
  client.setCACertBundle(kSdkRootBundle);
}

// The pinned SDK disables MBEDTLS_HAVE_TIME_DATE. This adapter enables chain
// and hostname verification; it does not claim certificate-date validation.

}  // namespace native_tls
