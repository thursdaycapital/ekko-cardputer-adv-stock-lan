#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Shared by the factory launcher and Ekko. This header only encodes/validates
// bytes; it does not access NVS, connect to a network, or log credentials.
namespace SharedWifiConfig {

static const char kNvsNamespace[] = "cardputer";
static const char kNvsKey[] = "ekko_wifi_v1";
static const size_t kBlobSize = 104;
static const size_t kMaxSsidBytes = 32;
static const size_t kMaxPasswordBytes = 64;
static const size_t kSsidOffset = 8;
static const size_t kPasswordOffset = 40;

struct Credentials {
  char ssid[kMaxSsidBytes + 1];
  char password[kMaxPasswordBytes + 1];

  Credentials() : ssid(), password() {}
};

// Storage contract:
// * Use the default NVS partition, namespace/cardputer, key/ekko_wifi_v1.
// * After the factory app has successfully connected, write the complete
//   104-byte record with one nvs_set_blob call, then nvs_commit. Do not split
//   SSID and password across separately committed keys. Never erase the old
//   record before replacement, and report either write/commit failure rather
//   than claiming the credentials were saved. Flash atomicity is NVS's job.
// * Ekko prefers a valid shared record over its historical configuration.
//   When absent, retain the factory's legacy cardputer keys, then Ekko's net
//   namespace as migration fallbacks. Storage and fallback policy belong to
//   callers; codec success does not prove an NVS write/commit succeeded.
//
// Layout: E K W 1 | version=1 | SSID byte length | password byte length |
// flags=0 | SSID[32] | password[64]. Unused bytes must be zero. These are
// byte lengths, not character counts. Empty passwords support open networks;
// no PSK/WEP policy is imposed here. Embedded NUL bytes are unsupported.

inline bool containsNul(const char* value, size_t length) {
  return length != 0 && std::memchr(value, 0, length) != nullptr;
}

inline bool boundedLength(const char* value, size_t maximum, size_t& length) {
  if (!value) return false;
  for (size_t i = 0; i <= maximum; ++i) {
    if (value[i] == '\0') {
      length = i;
      return true;
    }
  }
  return false;
}

inline bool encode(const char* ssid, size_t ssidLength,
                   const char* password, size_t passwordLength,
                   uint8_t* blob, size_t capacity) {
  if (!blob || capacity < kBlobSize) return false;
  if (!ssid || ssidLength == 0 || ssidLength > kMaxSsidBytes ||
      passwordLength > kMaxPasswordBytes ||
      (!password && passwordLength != 0) ||
      containsNul(ssid, ssidLength) ||
      containsNul(password, passwordLength)) {
    std::memset(blob, 0, kBlobSize);
    return false;
  }

  // Assemble locally so no partially updated record is exposed to the caller,
  // including when either input string aliases the destination buffer.
  uint8_t record[kBlobSize] = {};
  record[0] = 'E'; record[1] = 'K'; record[2] = 'W'; record[3] = '1';
  record[4] = 1;
  record[5] = static_cast<uint8_t>(ssidLength);
  record[6] = static_cast<uint8_t>(passwordLength);
  std::memcpy(record + kSsidOffset, ssid, ssidLength);
  if (passwordLength) {
    std::memcpy(record + kPasswordOffset, password, passwordLength);
  }
  std::memcpy(blob, record, kBlobSize);
  return true;
}

inline bool encode(const char* ssid, const char* password,
                   uint8_t* blob, size_t capacity) {
  size_t ssidLength = 0, passwordLength = 0;
  if (!boundedLength(ssid, kMaxSsidBytes, ssidLength) ||
      !boundedLength(password, kMaxPasswordBytes, passwordLength)) {
    if (blob && capacity >= kBlobSize) std::memset(blob, 0, kBlobSize);
    return false;
  }
  return encode(ssid, ssidLength, password, passwordLength, blob, capacity);
}

inline bool decode(const uint8_t* blob, size_t length, Credentials& out) {
  if (!blob || length != kBlobSize ||
      blob[0] != 'E' || blob[1] != 'K' || blob[2] != 'W' || blob[3] != '1' ||
      blob[4] != 1 || blob[5] == 0 || blob[5] > kMaxSsidBytes ||
      blob[6] > kMaxPasswordBytes || blob[7] != 0) {
    out = Credentials();
    return false;
  }
  const size_t ssidLength = blob[5], passwordLength = blob[6];
  if (std::memchr(blob + kSsidOffset, 0, ssidLength) ||
      (passwordLength && std::memchr(blob + kPasswordOffset, 0, passwordLength))) {
    out = Credentials();
    return false;
  }
  for (size_t i = ssidLength; i < kMaxSsidBytes; ++i) {
    if (blob[kSsidOffset + i] != 0) {
      out = Credentials();
      return false;
    }
  }
  for (size_t i = passwordLength; i < kMaxPasswordBytes; ++i) {
    if (blob[kPasswordOffset + i] != 0) {
      out = Credentials();
      return false;
    }
  }
  Credentials decoded;
  std::memcpy(decoded.ssid, blob + kSsidOffset, ssidLength);
  if (passwordLength) {
    std::memcpy(decoded.password, blob + kPasswordOffset, passwordLength);
  }
  out = decoded;
  return true;
}

}  // namespace SharedWifiConfig
