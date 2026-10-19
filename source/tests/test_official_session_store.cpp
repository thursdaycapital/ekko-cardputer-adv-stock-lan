#include "OfficialSessionStore.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>

using namespace official_sessions;
using Binding = std::array<char, kBindingBytes + 1>;
using Nonce = std::array<char, kNonceBytes + 1>;
using Record = std::array<char, kRecordBytes + 1>;
static_assert(!std::is_copy_constructible<Store>::value, "A pending transaction has one owner");

static std::size_t allocations = 0;
void* operator new(std::size_t size) {
  ++allocations;
  if (void* pointer = std::malloc(size ? size : 1)) return pointer;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }

static Binding bindingFor(std::uint64_t number) {
  Binding binding{};
  binding.fill('0'); binding.back() = 0;
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 16; ++i) {
    binding[63 - i] = hex[number & 15]; number >>= 4;
  }
  return binding;
}
static Nonce nonceFor(unsigned char first) {
  Nonce nonce{};
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 16; ++i) {
    const unsigned char value = static_cast<unsigned char>(first + i);
    nonce[2 * i] = hex[value >> 4]; nonce[2 * i + 1] = hex[value & 15];
  }
  return nonce;
}
static Record recordFor(const Binding& binding, const Nonce& nonce) {
  Record record{};
  std::memcpy(record.data(), "v1|", 3);
  std::memcpy(record.data() + 3, binding.data(), kBindingBytes);
  record[67] = '|';
  std::memcpy(record.data() + 68, nonce.data(), kNonceBytes);
  return record;
}

class FakeStorage final : public Storage {
 public:
  struct Slot { bool present = false; std::size_t bytes = 0; Record value{}; };
  std::array<Slot, kSlotCount> slots{};
  std::size_t reads = 0, writes = 0, randoms = 0;
  std::size_t failReadAt = 0;
  bool failReads = false, failWrite = false, persistOnFailure = false;
  bool suppressPersist = false, failRandom = false, repeatEntropy = false;
  int verification = 0;  // One independent read after write: error/missing/mismatch/oversize.
  bool afterWrite = false;
  char lastKey[4]{};
  Record lastAttempt{};

  static std::size_t index(const char* key) {
    assert(key && std::strlen(key) == 3 && key[0] == 's');
    auto hex = [](char c) -> unsigned {
      assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
      return c <= '9' ? static_cast<unsigned>(c - '0') : static_cast<unsigned>(c - 'a' + 10);
    };
    const std::size_t value = 16 * hex(key[1]) + hex(key[2]);
    assert(value < kSlotCount); return value;
  }
  void set(std::size_t slot, const Binding& binding, const Nonce& nonce) {
    slots[slot] = Slot{true, kRecordBytes, recordFor(binding, nonce)};
  }
  ReadStatus read(const char* key, char* out, std::size_t capacity, std::size_t& bytes) override {
    ++reads;
    const std::size_t slot = index(key);
    assert(out && capacity == kRecordBytes + 1);
    if (failReads || (failReadAt && reads == failReadAt)) return ReadStatus::Error;
    const bool verify = afterWrite;
    afterWrite = false;
    if (verify && verification == 1) return ReadStatus::Error;
    if (verify && verification == 2) return ReadStatus::Missing;
    if (!slots[slot].present) return ReadStatus::Missing;
    bytes = slots[slot].bytes;
    std::memcpy(out, slots[slot].value.data(), capacity);
    if (verify && verification == 3) out[99] = out[99] == '0' ? '1' : '0';
    if (verify && verification == 4) bytes = capacity + 4;
    return ReadStatus::Found;
  }
  bool writeCommit(const char* key, const char* value, std::size_t bytes) override {
    ++writes;
    const std::size_t slot = index(key);
    assert(bytes == kRecordBytes && value[bytes] == 0);
    std::memcpy(lastKey, key, sizeof(lastKey));
    std::memcpy(lastAttempt.data(), value, lastAttempt.size());
    if (!suppressPersist && (!failWrite || persistOnFailure)) {
      slots[slot].present = true; slots[slot].bytes = bytes;
      std::memcpy(slots[slot].value.data(), value, lastAttempt.size());
    }
    afterWrite = !failWrite;
    return !failWrite;
  }
  bool random(std::uint8_t* out, std::size_t bytes) override {
    ++randoms;
    assert(bytes == 16);
    if (failRandom) return false;
    for (std::size_t i = 0; i < bytes; ++i)
      out[i] = static_cast<std::uint8_t>((repeatEntropy ? 1 : randoms) + i);
    return true;
  }
};

static void createAndReboot() {
  FakeStorage storage; Store store(storage);
  const auto binding = bindingFor(1);
  assert(!store.ready() && !store.pending() && !store.nonce()[0]);
  assert(store.ensure(binding.data()) == Result::Ok);
  assert(store.ready() && std::strcmp(store.nonce(), nonceFor(1).data()) == 0);
  assert(storage.randoms == 1 && storage.writes == 1 && storage.reads == kSlotCount + 2);
  const auto reads = storage.reads;
  for (int i = 0; i < 5; ++i) assert(store.ensure(store.binding()) == Result::Ok);
  assert(storage.reads == reads && storage.randoms == 1 && storage.writes == 1);
  Store reboot(storage);
  assert(reboot.ensure(binding.data()) == Result::Ok);
  assert(std::strcmp(reboot.nonce(), store.nonce()) == 0 && storage.randoms == 1 && storage.writes == 1);
  assert(sizeof(Store) <= 1024);
}

static void validationAndIsolation() {
  FakeStorage storage; Store store(storage);
  auto binding = bindingFor(0xabcdef);
  assert(store.ensure(binding.data()) == Result::Ok);
  Binding upper = binding;
  for (char& c : upper) if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
  assert(store.ensure(upper.data()) == Result::Ok && std::strcmp(store.binding(), binding.data()) == 0);
  assert(storage.randoms == 1);
  assert(store.ensure(nullptr) == Result::InvalidBinding && !store.ready() && !store.confirmedNonce()[0]);
  assert(store.ensure(binding.data(), 63) == Result::InvalidBinding);
  assert(store.ensure(binding.data(), 65) == Result::InvalidBinding);
  auto invalid = binding;
  invalid[3] = 0;
  assert(store.ensure(invalid.data(), 64) == Result::InvalidBinding);
  invalid[3] = static_cast<char>(0xff);
  assert(store.ensure(invalid.data(), 64) == Result::InvalidBinding);
  invalid[3] = 'g';
  assert(store.ensure(invalid.data()) == Result::InvalidBinding);
  std::array<char, 66> tooLong{}; tooLong.fill('a'); tooLong.back() = 0;
  assert(store.ensure(tooLong.data()) == Result::InvalidBinding);
  assert(storage.randoms == 1 && storage.writes == 1);
  assert(store.ensure(binding.data(), 64) == Result::Ok);
  const auto second = bindingFor(2);
  assert(store.ensure(second.data()) == Result::Ok);
  assert(std::strcmp(store.nonce(), nonceFor(2).data()) == 0);
  storage.failReads = true;
  assert(store.ensure(binding.data()) == Result::ReadError);
  assert(!store.ready() && !store.nonce()[0] && !store.confirmedNonce()[0]);
  storage.failReads = false;
  assert(store.ensure(binding.data()) == Result::Ok && std::strcmp(store.nonce(), nonceFor(1).data()) == 0);
}

static void failedWritesAndStableRetry() {
  FakeStorage storage; Store store(storage);
  const auto a = bindingFor(1), b = bindingFor(2);
  storage.failWrite = true;
  assert(store.ensure(a.data()) == Result::WriteError && store.pending() && !store.ready());
  const Record candidate = storage.lastAttempt;
  for (int i = 0; i < 4; ++i) {
    assert(store.ensure(a.data()) == Result::WriteError && storage.lastAttempt == candidate);
    assert(!store.nonce()[0] && !store.confirmedNonce()[0]);
  }
  assert(storage.randoms == 1);
  const auto writes = storage.writes;
  assert(store.ensure(b.data()) == Result::Busy && storage.writes == writes && storage.randoms == 1);
  assert(store.rotate(b.data()) == Result::Busy && std::strcmp(store.binding(), a.data()) == 0);
  assert(store.ensure(nullptr) == Result::InvalidBinding && store.pending());
  storage.failWrite = false;
  assert(store.rotate(a.data()) == Result::Ok && !store.pending() && storage.lastAttempt == candidate);
  assert(storage.randoms == 1);

  // A false return can be ambiguous. A found candidate still requires another
  // successful commit and a separate readback before it can become ready.
  FakeStorage ambiguous; Store waiting(ambiguous);
  ambiguous.failWrite = true; ambiguous.persistOnFailure = true;
  assert(waiting.ensure(a.data()) == Result::WriteError && !waiting.ready());
  assert(waiting.ensure(a.data()) == Result::WriteError && ambiguous.randoms == 1);
  ambiguous.failWrite = false;
  assert(waiting.ensure(a.data()) == Result::Ok && ambiguous.writes == 3 && ambiguous.randoms == 1);
}

static void independentVerificationAndRotation() {
  const auto binding = bindingFor(19);
  for (int mode = 1; mode <= 4; ++mode) {
    FakeStorage storage; Store store(storage);
    assert(store.ensure(binding.data()) == Result::Ok);
    const Nonce old = nonceFor(1);
    storage.verification = mode;
    const Result error = mode == 1 ? Result::VerifyReadError : Result::VerifyMismatch;
    assert(store.rotate(binding.data()) == error && store.pending() && !store.ready());
    assert(!store.nonce()[0] && std::strcmp(store.confirmedNonce(), old.data()) == 0);
    const Record candidate = storage.lastAttempt;
    storage.verification = 0;
    assert(store.ensure(binding.data()) == Result::Ok && !store.pending());
    assert(storage.lastAttempt == candidate && storage.randoms == 2 && storage.writes == 3);
    assert(std::strcmp(store.nonce(), nonceFor(2).data()) == 0);
    Store reboot(storage);
    assert(reboot.ensure(binding.data()) == Result::Ok && std::strcmp(reboot.nonce(), store.nonce()) == 0);
  }
  FakeStorage storage; Store store(storage);
  assert(store.ensure(binding.data()) == Result::Ok);
  storage.failWrite = true;
  assert(store.rotate(binding.data()) == Result::WriteError);
  const auto oldKey = FakeStorage::index(storage.lastKey);
  assert(storage.slots[oldKey].value == recordFor(binding, nonceFor(1)));
  assert(std::strcmp(store.confirmedNonce(), nonceFor(1).data()) == 0);
  storage.failWrite = false;
  assert(store.rotate(binding.data()) == Result::Ok && storage.randoms == 2);
  storage.failReads = true;
  assert(store.rotate(binding.data()) == Result::ReadError && !store.ready() && !store.pending());
  assert(std::strcmp(store.confirmedNonce(), nonceFor(2).data()) == 0);
}

static void readErrorsNeverBecomeMissing() {
  FakeStorage storage; Store store(storage);
  const auto binding = bindingFor(1);
  storage.failReads = true;
  for (int i = 0; i < 3; ++i) assert(store.ensure(binding.data()) == Result::ReadError);
  assert(!store.ready() && !store.pending() && storage.writes == 0 && storage.randoms == 0);
  storage.failReads = false;
  assert(store.ensure(binding.data()) == Result::Ok);
  Store reboot(storage);
  storage.failReadAt = storage.reads + kSlotCount; // Existing entry was found before a later failure.
  assert(reboot.ensure(binding.data()) == Result::ReadError);
  assert(!reboot.ready() && !reboot.confirmedNonce()[0] && storage.writes == 1 && storage.randoms == 1);
  storage.failReadAt = 0;
  assert(reboot.ensure(binding.data()) == Result::Ok && storage.writes == 1);
}

static void collisionsFullAndCorruptRecords() {
  const auto target = bindingFor(999);
  FakeStorage storage;
  for (std::size_t i = 0; i < kSlotCount; ++i)
    if (i != 17) storage.set(i, bindingFor(1000 + i), nonceFor(static_cast<unsigned char>(i)));
  const auto before = storage.slots;
  Store store(storage);
  assert(store.ensure(target.data()) == Result::Ok && FakeStorage::index(storage.lastKey) == 17);
  for (std::size_t i = 0; i < kSlotCount; ++i) if (i != 17)
    assert(storage.slots[i].value == before[i].value);
  Store full(storage);
  const auto fresh = bindingFor(888);
  assert(full.ensure(fresh.data()) == Result::StoreFull && storage.writes == 1 && storage.randoms == 1);
  assert(full.ensure(target.data()) == Result::Ok && storage.writes == 1);
  assert(full.rotate(target.data()) == Result::Ok); // No eviction or extra slot needed.
  for (std::size_t i = 0; i < kSlotCount; ++i) if (i != 17)
    assert(storage.slots[i].value == before[i].value);

  // Never stop at the first hole or use another binding from the same probe chain.
  FakeStorage holes; holes.set(62, target, nonceFor(41)); Store find(holes);
  assert(find.ensure(target.data()) == Result::Ok && std::strcmp(find.nonce(), nonceFor(41).data()) == 0);
  assert(holes.writes == 0 && holes.randoms == 0);
  holes.set(4, target, nonceFor(42)); Store duplicate(holes);
  assert(duplicate.ensure(target.data()) == Result::Conflict && !duplicate.confirmedNonce()[0]);
  assert(holes.writes == 0 && holes.randoms == 0);

  for (int kind = 0; kind < 6; ++kind) {
    FakeStorage corrupt; corrupt.set(9, target, nonceFor(1));
    if (kind == 0) corrupt.slots[9].value[1] = '2';
    if (kind == 1) corrupt.slots[9].value[3] = 0;
    if (kind == 2) corrupt.slots[9].value[68] = 'G';
    if (kind == 3) corrupt.slots[9].bytes = 0;
    if (kind == 4) corrupt.slots[9].bytes = kRecordBytes + 30;
    if (kind == 5) corrupt.slots[9].value[100] = '!';
    Store bad(corrupt);
    assert(bad.ensure(target.data()) == Result::CorruptRecord && !bad.confirmedNonce()[0]);
    assert(corrupt.writes == 0 && corrupt.randoms == 0);
  }
}

static void changedPendingSlotAndRandomFailures() {
  const auto a = bindingFor(1), b = bindingFor(2);
  FakeStorage storage; Store store(storage);
  storage.failWrite = true;
  assert(store.ensure(a.data()) == Result::WriteError);
  const auto slot = FakeStorage::index(storage.lastKey);
  storage.set(slot, b, nonceFor(22));
  const auto other = storage.slots[slot].value;
  storage.failWrite = false;
  assert(store.ensure(a.data()) == Result::Conflict && store.pending());
  assert(storage.slots[slot].value == other && storage.writes == 1 && storage.randoms == 1);

  FakeStorage missing; Store deleted(missing);
  assert(deleted.ensure(a.data()) == Result::Ok);
  missing.failWrite = true;
  assert(deleted.rotate(a.data()) == Result::WriteError);
  missing.slots[FakeStorage::index(missing.lastKey)].present = false;
  missing.failWrite = false;
  assert(deleted.ensure(a.data()) == Result::Conflict && missing.writes == 2);
  assert(std::strcmp(deleted.confirmedNonce(), nonceFor(1).data()) == 0);

  FakeStorage random; Store waiting(random);
  random.failRandom = true;
  assert(waiting.ensure(a.data()) == Result::RandomError && !waiting.pending() && random.writes == 0);
  random.failRandom = false; random.repeatEntropy = true;
  assert(waiting.ensure(a.data()) == Result::Ok);
  assert(waiting.rotate(a.data()) == Result::RandomError && !waiting.pending() && !waiting.ready());
  assert(random.writes == 1 && std::strcmp(waiting.confirmedNonce(), nonceFor(1).data()) == 0);
  assert(waiting.ensure(a.data()) == Result::Ok); // Failed explicit rotation preserves the old session.
}

int main() {
  const auto before = allocations;
  createAndReboot();
  validationAndIsolation();
  failedWritesAndStableRetry();
  independentVerificationAndRotation();
  readErrorsNeverBecomeMissing();
  collisionsFullAndCorruptRecords();
  changedPendingSlotAndRandomFailures();
  assert(allocations == before);
  std::printf("Official session store passed: 64-slot binding isolation, durable readback, stable retries, rotation; state=%zu bytes\n", sizeof(Store));
}
