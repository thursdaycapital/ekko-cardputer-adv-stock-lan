#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace official_sessions {

constexpr std::size_t kBindingBytes = 64;
constexpr std::size_t kNonceBytes = 32;  // Hex-encoded 128-bit random value.
constexpr std::size_t kSlotCount = 64;
constexpr std::size_t kRecordBytes = 3 + kBindingBytes + 1 + kNonceBytes;

enum class ReadStatus { Found, Missing, Error };
enum class Result {
  Uninitialized, Ok, InvalidBinding, ReadError, CorruptRecord, StoreFull,
  RandomError, WriteError, VerifyReadError, VerifyMismatch, Conflict, Busy
};

// A single owner must serialize Store calls and other writers to this namespace.
// writeCommit atomically replaces one value and reports a completed durable
// commit. read must independently fetch persistent storage, not echo a pending
// write or cached value. Found returns the complete byte count excluding NUL;
// an oversized value must never be silently truncated and reported as complete.
// The adapter owns the namespace; this class only uses keys s00 through s3f.
class Storage {
 public:
  virtual ~Storage() = default;
  virtual ReadStatus read(const char* key, char* out, std::size_t capacity,
                          std::size_t& bytes) = 0;
  virtual bool writeCommit(const char* key, const char* value, std::size_t bytes) = 0;
  virtual bool random(std::uint8_t* out, std::size_t bytes) = 0;
};

inline const char* resultName(Result result) {
  switch (result) {
    case Result::Uninitialized: return "uninitialized";
    case Result::Ok: return "ok";
    case Result::InvalidBinding: return "invalid_binding";
    case Result::ReadError: return "read_error";
    case Result::CorruptRecord: return "corrupt_record";
    case Result::StoreFull: return "store_full";
    case Result::RandomError: return "random_error";
    case Result::WriteError: return "write_error";
    case Result::VerifyReadError: return "verify_read_error";
    case Result::VerifyMismatch: return "verify_mismatch";
    case Result::Conflict: return "conflict";
    case Result::Busy: return "busy";
  }
  return "unknown";
}

class Store {
 public:
  explicit Store(Storage& storage) : storage_(storage) {}
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  Result ensure(const char* binding) { return resolveCString(binding, false); }
  Result rotate(const char* binding) { return resolveCString(binding, true); }
  Result ensure(const char* binding, std::size_t bytes) { return resolve(binding, bytes, false); }
  Result rotate(const char* binding, std::size_t bytes) { return resolve(binding, bytes, true); }

  bool ready() const { return result_ == Result::Ok && confirmed_[0] && !pending_; }
  const char* nonce() const { return ready() ? confirmed_.data() : ""; }
  // Retains the old confirmed conversation during a failed rotation. This value
  // belongs to binding(), not an argument rejected with Busy/InvalidBinding.
  const char* confirmedNonce() const { return confirmed_.data(); }
  const char* binding() const { return binding_.data(); }
  bool pending() const { return pending_; }
  Result result() const { return result_; }

 private:
  using Binding = std::array<char, kBindingBytes + 1>;
  using Nonce = std::array<char, kNonceBytes + 1>;
  using Record = std::array<char, kRecordBytes + 1>;

  static int hexValue(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  static bool canonicalBinding(const char* input, std::size_t bytes, Binding& out) {
    if (!input || bytes != kBindingBytes) return false;
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < kBindingBytes; ++i) {
      const int value = hexValue(static_cast<unsigned char>(input[i]));
      if (value < 0) return false;
      out[i] = hex[value];
    }
    return true;
  }
  static bool validRecord(const Record& record, std::size_t bytes) {
    if (bytes != kRecordBytes || std::memcmp(record.data(), "v1|", 3) ||
        record[3 + kBindingBytes] != '|' || record[kRecordBytes] != '\0') return false;
    for (std::size_t i = 3; i < kRecordBytes; ++i) {
      if (i == 3 + kBindingBytes) continue;
      const unsigned char c = static_cast<unsigned char>(record[i]);
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
  }
  static void slotKey(std::size_t slot, char (&key)[4]) {
    constexpr char hex[] = "0123456789abcdef";
    key[0] = 's'; key[1] = hex[slot >> 4]; key[2] = hex[slot & 15]; key[3] = 0;
  }
  static std::size_t firstSlot(const Binding& binding) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < kBindingBytes; ++i) {
      hash ^= static_cast<unsigned char>(binding[i]);
      hash *= 16777619u;
    }
    return hash % kSlotCount;
  }
  Result finish(Result result) { result_ = result; return result; }
  void clearActive() {
    binding_.fill(0); confirmed_.fill(0); candidate_.fill(0);
    key_[0] = 0; pending_ = false; result_ = Result::Uninitialized;
  }
  Result resolveCString(const char* binding, bool rotate) {
    std::size_t bytes = 0;
    if (binding) while (bytes <= kBindingBytes && binding[bytes]) ++bytes;
    return resolve(binding, bytes, rotate);
  }
  Result resolve(const char* input, std::size_t bytes, bool rotate) {
    Binding wanted{};  // Snapshot permits ensure(store.binding()) safely.
    if (!canonicalBinding(input, bytes, wanted)) {
      if (!pending_) clearActive();
      return finish(Result::InvalidBinding);
    }
    if (pending_) {
      if (wanted != binding_) return finish(Result::Busy);
      return savePending();  // Both ensure and rotate retry the same candidate.
    }
    if (!rotate && wanted == binding_ && confirmed_[0]) return finish(Result::Ok);
    if (wanted != binding_) clearActive();
    binding_ = wanted;  // No previous account's value is visible on a read error.

    std::size_t freeSlot = kSlotCount, foundSlot = kSlotCount;
    Nonce foundNonce{};
    const std::size_t start = firstSlot(binding_);
    for (std::size_t step = 0; step < kSlotCount; ++step) {
      const std::size_t slot = (start + step) % kSlotCount;
      char key[4]{}; slotKey(slot, key);
      Record record{}; std::size_t count = 0;
      const ReadStatus read = storage_.read(key, record.data(), record.size(), count);
      if (read == ReadStatus::Error) return finish(Result::ReadError);
      if (read == ReadStatus::Missing) {
        if (freeSlot == kSlotCount) freeSlot = slot;
        continue;  // A later probe can contain this binding after a missing slot.
      }
      if (!validRecord(record, count)) return finish(Result::CorruptRecord);
      if (std::memcmp(record.data() + 3, binding_.data(), kBindingBytes)) continue;
      if (foundSlot != kSlotCount) return finish(Result::Conflict);
      foundSlot = slot;
      std::memcpy(foundNonce.data(), record.data() + 4 + kBindingBytes, kNonceBytes);
    }
    if (foundSlot != kSlotCount) {
      if (confirmed_[0] && foundNonce != confirmed_) return finish(Result::Conflict);
      confirmed_ = foundNonce;
      slotKey(foundSlot, key_);
      if (!rotate) return finish(Result::Ok);
    } else {
      if (confirmed_[0]) return finish(Result::Conflict);
      if (freeSlot == kSlotCount) return finish(Result::StoreFull);
      slotKey(freeSlot, key_);
    }

    std::array<std::uint8_t, kNonceBytes / 2> entropy{};
    if (!storage_.random(entropy.data(), entropy.size())) return finish(Result::RandomError);
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < entropy.size(); ++i) {
      candidate_[2 * i] = hex[entropy[i] >> 4];
      candidate_[2 * i + 1] = hex[entropy[i] & 15];
    }
    if (candidate_ == confirmed_) {
      candidate_.fill(0);
      return finish(Result::RandomError);  // A rotation must actually change ID.
    }
    pending_ = true;
    return savePending();
  }

  Result savePending() {
    // Detect a changed slot before replacing it. The adapter must serialize
    // other writers through the independent read, commit and readback sequence.
    Record current{}; std::size_t count = 0;
    const ReadStatus before = storage_.read(key_, current.data(), current.size(), count);
    if (before == ReadStatus::Error) return finish(Result::ReadError);
    if (before == ReadStatus::Missing) {
      if (confirmed_[0]) return finish(Result::Conflict);
    } else {
      if (!validRecord(current, count)) return finish(Result::CorruptRecord);
      if (std::memcmp(current.data() + 3, binding_.data(), kBindingBytes))
        return finish(Result::Conflict);
      const char* currentNonce = current.data() + 4 + kBindingBytes;
      if (std::memcmp(currentNonce, candidate_.data(), kNonceBytes) &&
          (!confirmed_[0] || std::memcmp(currentNonce, confirmed_.data(), kNonceBytes)))
        return finish(Result::Conflict);
    }
    Record expected{};
    std::memcpy(expected.data(), "v1|", 3);
    std::memcpy(expected.data() + 3, binding_.data(), kBindingBytes);
    expected[3 + kBindingBytes] = '|';
    std::memcpy(expected.data() + 4 + kBindingBytes, candidate_.data(), kNonceBytes);
    if (!storage_.writeCommit(key_, expected.data(), kRecordBytes)) return finish(Result::WriteError);

    Record verified{}; count = 0;
    const ReadStatus after = storage_.read(key_, verified.data(), verified.size(), count);
    if (after == ReadStatus::Error) return finish(Result::VerifyReadError);
    if (after != ReadStatus::Found || count != kRecordBytes || verified != expected)
      return finish(Result::VerifyMismatch);
    confirmed_ = candidate_;
    candidate_.fill(0); pending_ = false;
    return finish(Result::Ok);
  }

  Storage& storage_;
  Binding binding_{};
  Nonce confirmed_{};
  Nonce candidate_{};
  char key_[4]{};
  Result result_ = Result::Uninitialized;
  bool pending_ = false;
};

static_assert(sizeof(Store) <= 1024, "Session persistence state must fit in 1 KiB");

}  // namespace official_sessions
