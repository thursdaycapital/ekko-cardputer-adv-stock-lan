#include "SharedWifiConfig.h"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

namespace SW = SharedWifiConfig;
typedef std::array<uint8_t, SW::kBlobSize> Blob;

static void expectEmpty(const SW::Credentials& credentials) {
  for (size_t i = 0; i < sizeof(credentials.ssid); ++i) assert(credentials.ssid[i] == 0);
  for (size_t i = 0; i < sizeof(credentials.password); ++i) assert(credentials.password[i] == 0);
}

static void expectRejected(const uint8_t* blob, size_t length) {
  SW::Credentials credentials;
  std::strcpy(credentials.ssid, "old-network");
  std::strcpy(credentials.password, "synthetic-old-password");
  assert(!SW::decode(blob, length, credentials));
  expectEmpty(credentials);
}

static Blob record(const std::string& ssid, const std::string& password) {
  Blob blob;
  blob.fill(0xA5);
  assert(SW::encode(ssid.data(), ssid.size(), password.data(), password.size(),
                    blob.data(), blob.size()));
  return blob;
}

static void testRoundTripsAndByteLimits() {
  assert(std::strcmp(SW::kNvsNamespace, "cardputer") == 0);
  assert(std::strcmp(SW::kNvsKey, "ekko_wifi_v1") == 0);
  static_assert(SW::kBlobSize == 104, "Wire layout must remain fixed");
  static_assert(SW::kPasswordOffset + SW::kMaxPasswordBytes == SW::kBlobSize,
                "Password is the last wire field");
  const std::string names[] = {"A", "test-二楼电脑", std::string(32, 's')};
  const std::string passwords[] = {"", "abcde", "0123456789", std::string(63, 'p'),
                                   std::string(64, 'F')};
  for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); ++n) {
    for (size_t p = 0; p < sizeof(passwords) / sizeof(passwords[0]); ++p) {
      const Blob blob = record(names[n], passwords[p]);
      assert(blob[5] == names[n].size());
      assert(blob[6] == passwords[p].size());
      assert(blob[7] == 0);
      SW::Credentials decoded;
      assert(SW::decode(blob.data(), blob.size(), decoded));
      assert(std::string(decoded.ssid) == names[n]);
      assert(std::string(decoded.password) == passwords[p]);
      for (size_t i = names[n].size(); i < SW::kMaxSsidBytes; ++i)
        assert(blob[SW::kSsidOffset + i] == 0);
      for (size_t i = passwords[p].size(); i < SW::kMaxPasswordBytes; ++i)
        assert(blob[SW::kPasswordOffset + i] == 0);
    }
  }
  // Ten 3-byte UTF-8 characters plus two ASCII bytes exactly fills SSID[32].
  std::string utf8;
  for (int i = 0; i < 10; ++i) utf8 += "楼";
  utf8 += "01";
  assert(utf8.size() == 32);
  const Blob blob = record(utf8, "");
  SW::Credentials decoded;
  assert(SW::decode(blob.data(), blob.size(), decoded));
  assert(std::string(decoded.ssid) == utf8);
  Blob rejected;
  assert(!SW::encode((utf8 + "2").c_str(), "", rejected.data(), rejected.size()));
}

static void testEncodeFailuresAndAliasing() {
  Blob blob;
  const char nulSsid[] = {'a', '\0', 'b'};
  const char nulPassword[] = {'p', '\0', 'q'};
  assert(!SW::encode(nulSsid, sizeof(nulSsid), "", 0, blob.data(), blob.size()));
  assert(!SW::encode("ssid", 4, nulPassword, sizeof(nulPassword), blob.data(), blob.size()));
  assert(!SW::encode("", "", blob.data(), blob.size()));
  assert(!SW::encode(nullptr, 1, "", 0, blob.data(), blob.size()));
  assert(!SW::encode("ssid", 4, nullptr, 1, blob.data(), blob.size()));
  assert(!SW::encode(std::string(33, 's').c_str(), "", blob.data(), blob.size()));
  assert(!SW::encode("ssid", std::string(65, 'p').c_str(), blob.data(), blob.size()));
  assert(!SW::encode("ssid", static_cast<const char*>(nullptr), blob.data(), blob.size()));
  assert(!SW::encode("ssid", "", nullptr, SW::kBlobSize));
  blob.fill(0xA5);
  assert(!SW::encode("ssid", "", blob.data(), blob.size() - 1));
  for (size_t i = 0; i < blob.size(); ++i) assert(blob[i] == 0xA5);
  assert(!SW::encode("", "", blob.data(), blob.size()));
  for (size_t i = 0; i < blob.size(); ++i) assert(blob[i] == 0);
  assert(SW::encode("open", 4, nullptr, 0, blob.data(), blob.size()));
  std::memcpy(blob.data(), "ssid", 4);
  std::memcpy(blob.data() + 20, "synthetic", 9);
  assert(SW::encode(reinterpret_cast<const char*>(blob.data()), 4,
                    reinterpret_cast<const char*>(blob.data() + 20), 9,
                    blob.data(), blob.size()));
  SW::Credentials decoded;
  assert(SW::decode(blob.data(), blob.size(), decoded));
  assert(std::strcmp(decoded.ssid, "ssid") == 0);
  assert(std::strcmp(decoded.password, "synthetic") == 0);
}

static void testMalformedRecords() {
  const Blob valid = record("ssid", "synthetic");
  expectRejected(nullptr, SW::kBlobSize);
  for (size_t length = 0; length < SW::kBlobSize; ++length)
    expectRejected(valid.data(), length);
  expectRejected(valid.data(), SW::kBlobSize + 1);
  for (size_t i = 0; i < 4; ++i) {
    Blob bad = valid; bad[i] ^= 1; expectRejected(bad.data(), bad.size());
  }
  const uint8_t badVersions[] = {0, 2, 255};
  for (size_t i = 0; i < sizeof(badVersions); ++i) {
    Blob bad = valid; bad[4] = badVersions[i]; expectRejected(bad.data(), bad.size());
  }
  const uint8_t badSsidLengths[] = {0, 33, 255};
  for (size_t i = 0; i < sizeof(badSsidLengths); ++i) {
    Blob bad = valid; bad[5] = badSsidLengths[i]; expectRejected(bad.data(), bad.size());
  }
  const uint8_t badPasswordLengths[] = {65, 255};
  for (size_t i = 0; i < sizeof(badPasswordLengths); ++i) {
    Blob bad = valid; bad[6] = badPasswordLengths[i]; expectRejected(bad.data(), bad.size());
  }
  Blob bad = valid; bad[7] = 1; expectRejected(bad.data(), bad.size());
  bad = valid; bad[SW::kSsidOffset] = 0; expectRejected(bad.data(), bad.size());
  bad = valid; bad[SW::kPasswordOffset] = 0; expectRejected(bad.data(), bad.size());
  bad = valid; bad[SW::kSsidOffset + valid[5]] = 'x'; expectRejected(bad.data(), bad.size());
  bad = valid; bad[SW::kPasswordOffset + valid[6]] = 'x'; expectRejected(bad.data(), bad.size());
  // A claimed shorter field must not expose the unused old tail as valid padding.
  bad = valid; --bad[5]; expectRejected(bad.data(), bad.size());
  bad = valid; --bad[6]; expectRejected(bad.data(), bad.size());
}

// A host transaction double illustrates caller ordering, not ESP-IDF's flash
// atomicity. SDK/device power-loss behavior must be verified separately.
struct SingleBlobStore {
  Blob committed, pending;
  size_t sets = 0, commits = 0;
  bool hasPending = false;

  explicit SingleBlobStore(const Blob& initial) : committed(initial), pending() {}

  bool write(const Blob& next, bool setOk, bool commitOk) {
    ++sets;
    if (!setOk) return false;
    pending = next;
    hasPending = true;
    ++commits;
    if (!commitOk) { hasPending = false; return false; }
    committed = pending;
    hasPending = false;
    return true;
  }
};

static void testSingleRecordBoundary() {
  const Blob oldRecord = record("old-ssid", "old-synthetic-password");
  const Blob newRecord = record("new-ssid", "new-synthetic-password");
  SingleBlobStore storage(oldRecord);
  assert(!storage.write(newRecord, false, true));
  assert(storage.sets == 1 && storage.commits == 0);
  assert(storage.committed == oldRecord);
  assert(!storage.write(newRecord, true, false));
  assert(storage.sets == 2 && storage.commits == 1);
  assert(storage.committed == oldRecord);
  assert(storage.write(newRecord, true, true));
  assert(storage.sets == 3 && storage.commits == 2);
  assert(storage.committed == newRecord);
  SW::Credentials decoded;
  assert(SW::decode(storage.committed.data(), storage.committed.size(), decoded));
  assert(std::strcmp(decoded.ssid, "new-ssid") == 0);
  assert(std::strcmp(decoded.password, "new-synthetic-password") == 0);
  Blob changed = newRecord;
  changed[SW::kPasswordOffset] ^= 1;
  // A codec has no commit marker or checksum: a structurally valid modified
  // record can decode. Atomic persistence must come from one NVS blob update.
  assert(SW::decode(changed.data(), changed.size(), decoded));
}

int main() {
  testRoundTripsAndByteLimits();
  testEncodeFailuresAndAliasing();
  testMalformedRecords();
  testSingleRecordBoundary();
  std::cout << "shared Wi-Fi wire contract: passed (synthetic credentials only)\n";
}
