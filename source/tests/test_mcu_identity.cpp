#include "McuIdentity.h"
#include <cassert>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace native_identity;
using Read = ReadResult<std::string>;

int main() {
  const std::uint8_t entropy[kDeviceCodeEntropyBytes] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  const auto code = newDeviceCode(0x123456abcdefULL, entropy);
  assert(std::string(code.data()) == "hstudio_mcu_123456abcdef00010203040506070809");
  assert(code.back() == 0);
  assert(newDeviceCode(0, entropy)[0] == 0);
  assert(newDeviceCode(0x1000000000000ULL, entropy)[0] == 0);
  assert(newDeviceCode(0x123456abcdefULL, nullptr)[0] == 0);
  std::set<std::string> identities;
  for (std::uint64_t mac = 1; mac < 4097; ++mac)
    assert(identities.insert(newDeviceCode(mac, entropy).data()).second);

  int reads = 0, generations = 0, writes = 0;
  const auto generate = [&]() { ++generations; return std::string(code.data()); };
  BootIdentity<std::string> existing;
  auto stored = [&]() { ++reads; return Read{ReadState::Present, "legacy-paired-identity"}; };
  auto forbiddenWrite = [&](const std::string&) { ++writes; return WriteState::Saved; };
  for (int i = 0; i < 8; ++i)
    assert(existing.resolve(stored, generate, forbiddenWrite) == "legacy-paired-identity");
  assert(reads == 1 && generations == 0 && writes == 0 && existing.state() == State::Ready);

  BootIdentity<std::string> fresh;
  std::string saved;
  auto missing = [&]() { ++reads; return Read{ReadState::Missing, {}}; };
  auto save = [&](const std::string& candidate) { ++writes; saved = candidate; return WriteState::Saved; };
  assert(fresh.resolve(missing, generate, save) == code.data());
  assert(fresh.resolve(missing, generate, save) == saved);
  assert(generations == 1 && writes == 1 && fresh.state() == State::Ready);

  BootIdentity<std::string> retry;
  std::vector<std::string> attempted;
  auto unavailable = [&](const std::string& candidate) { attempted.push_back(candidate); return WriteState::Failed; };
  for (int i = 0; i < 6; ++i) {
    assert(retry.resolve(missing, generate, unavailable).empty());
    assert(retry.state() == State::SaveFailed);
  }
  assert(generations == 2 && attempted.size() == 6);
  for (const auto& candidate : attempted) assert(candidate == code.data());
  assert(retry.resolve(missing, generate, save) == code.data());
  assert(generations == 2 && retry.state() == State::Ready);

  BootIdentity<std::string> reboot;
  auto afterReboot = [&]() { return Read{ReadState::Present, saved}; };
  assert(reboot.resolve(afterReboot, generate, forbiddenWrite) == saved);
  assert(generations == 2);

  BootIdentity<std::string> readFailure;
  auto failedRead = [&]() { ++reads; return Read{ReadState::Failed, {}}; };
  const int writesBefore = writes, generationsBefore = generations;
  for (int i = 0; i < 4; ++i) assert(readFailure.resolve(failedRead, generate, save).empty());
  assert(readFailure.state() == State::ReadFailed && writes == writesBefore && generations == generationsBefore);

  BootIdentity<std::string> conflicting;
  int conflicts = 0;
  auto conflict = [&](const std::string&) { ++conflicts; return WriteState::Conflict; };
  assert(conflicting.resolve(missing, generate, conflict).empty());
  assert(conflicting.resolve(missing, generate, save).empty());
  assert(conflicts == 1 && conflicting.state() == State::Conflict);

  assert(mayAddMigrationField(ReadState::Missing, true));
  assert(!mayAddMigrationField(ReadState::Present, true));
  assert(!mayAddMigrationField(ReadState::Failed, true));
  assert(!mayAddMigrationField(ReadState::Missing, false));
  const Read absent{ReadState::Missing, {}}, bad{ReadState::Failed, {}}, empty{ReadState::Present, {}};
  const Read alice{ReadState::Present, "alice"}, password{ReadState::Present, "fixture-password"};
  const Read other{ReadState::Present, "other"};
  assert(mayAddLegacyCredentialPair(absent, absent, alice, password));
  assert(mayAddLegacyCredentialPair(alice, absent, alice, password));
  assert(mayAddLegacyCredentialPair(absent, password, alice, password));
  assert(!mayAddLegacyCredentialPair(other, absent, alice, password));
  assert(!mayAddLegacyCredentialPair(absent, other, alice, password));
  assert(!mayAddLegacyCredentialPair(alice, password, alice, password));
  assert(!mayAddLegacyCredentialPair(empty, absent, alice, password));
  assert(!mayAddLegacyCredentialPair(absent, absent, alice, empty));
  assert(!mayAddLegacyCredentialPair(bad, absent, alice, password));
  assert(!mayAddLegacyCredentialPair(absent, absent, bad, password));
  std::puts("MCU identity persistence, boot-stable retry and non-destructive migration policy passed");
}
