#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace native_identity {

enum class ReadState { Missing, Present, Failed };
enum class WriteState { Saved, Failed, Conflict };
enum class State { Unread, Ready, PendingSave, ReadFailed, SaveFailed, Conflict };

constexpr std::size_t kDeviceCodeBytes = 45;
constexpr std::size_t kDeviceCodeEntropyBytes = 10;

// Keep the released hstudio_mcu_ + 32 hex format. The chip's six unique efuse
// MAC bytes distinguish fresh devices even before the Wi-Fi entropy source is
// running; the remaining bytes make a factory-reset identity a fresh identity.
inline std::array<char, kDeviceCodeBytes> newDeviceCode(
    std::uint64_t efuseMac, const std::uint8_t* entropy) {
  std::array<char, kDeviceCodeBytes> result{};
  if (!efuseMac || efuseMac > 0xFFFFFFFFFFFFULL || !entropy) return result;
  constexpr char prefix[] = "hstudio_mcu_";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < sizeof(prefix) - 1; ++i) result[i] = prefix[i];
  for (std::size_t i = 0; i < 16; ++i) {
    const std::uint8_t value = i < 6
        ? static_cast<std::uint8_t>(efuseMac >> ((5 - i) * 8))
        : entropy[i - 6];
    result[sizeof(prefix) - 1 + 2 * i] = hex[value >> 4];
    result[sizeof(prefix) + 2 * i] = hex[value & 15];
  }
  return result;
}

inline const char* stateName(State state) {
  switch (state) {
    case State::Unread: return "unread";
    case State::Ready: return "ready";
    case State::PendingSave: return "pending_save";
    case State::ReadFailed: return "read_failed";
    case State::SaveFailed: return "save_failed";
    case State::Conflict: return "identity_conflict";
  }
  return "unknown";
}

template <typename Text> struct ReadResult {
  ReadState state;
  Text value;
};

// The generated candidate never leaves this boot's cache until storage has
// accepted and read back it. A retry writes the same identity, never a new one.
template <typename Text> class BootIdentity {
 public:
  template <typename Reader, typename Generator, typename Writer>
  const Text& resolve(Reader read, Generator generate, Writer write) {
    if (state_ == State::Unread) {
      const ReadResult<Text> loaded = read();
      if (loaded.state == ReadState::Failed) {
        state_ = State::ReadFailed;
      } else if (loaded.state == ReadState::Present && loaded.value.length()) {
        value_ = loaded.value;
        state_ = value_ == loaded.value ? State::Ready : State::ReadFailed;
      } else {
        value_ = generate();
        state_ = value_.length() ? State::PendingSave : State::SaveFailed;
      }
    }
    if (state_ == State::PendingSave || (state_ == State::SaveFailed && value_.length())) {
      const WriteState saved = write(value_);
      state_ = saved == WriteState::Saved ? State::Ready :
          saved == WriteState::Conflict ? State::Conflict : State::SaveFailed;
    }
    return state_ == State::Ready ? value_ : unavailable_;
  }

  State state() const { return state_; }

 private:
  Text value_;
  Text unavailable_;
  State state_ = State::Unread;
};

// Fill only a missing modern field. An already present empty field can express
// an explicit reset, and an existing value may not be replaced by legacy data.
inline bool mayAddMigrationField(ReadState target, bool hasLegacyValue) {
  return target == ReadState::Missing && hasLegacyValue;
}

template <typename Text> bool mayAddLegacyCredentialPair(
    const ReadResult<Text>& account, const ReadResult<Text>& password,
    const ReadResult<Text>& legacyAccount, const ReadResult<Text>& legacyPassword) {
  if (account.state == ReadState::Failed || password.state == ReadState::Failed ||
      legacyAccount.state != ReadState::Present || legacyPassword.state != ReadState::Present ||
      !legacyAccount.value.length() || !legacyPassword.value.length()) return false;
  if (account.state == ReadState::Present && account.value != legacyAccount.value) return false;
  if (password.state == ReadState::Present && password.value != legacyPassword.value) return false;
  return account.state == ReadState::Missing || password.state == ReadState::Missing;
}

}  // namespace native_identity
