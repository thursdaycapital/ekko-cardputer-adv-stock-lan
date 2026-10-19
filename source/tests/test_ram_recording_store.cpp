#include "RamRecordingStore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <new>
#include <thread>
#include <vector>

using namespace ram_recording;

template<int Tag> struct Allocator {
  static size_t calls, failOn, peak, liveBytes;
  static std::map<void*, size_t> live;
  static void* allocate(size_t bytes) {
    if (++calls == failOn) return nullptr;
    void* memory = ::operator new(bytes, std::nothrow);
    if (memory) { live[memory] = bytes; liveBytes += bytes; peak = std::max(peak, liveBytes); }
    return memory;
  }
  static void deallocate(void* memory) {
    const auto found = live.find(memory); assert(found != live.end());
    liveBytes -= found->second; live.erase(found); ::operator delete(memory);
  }
  static Allocation hooks() { return Allocation{allocate, deallocate}; }
  static void reset() { assert(live.empty()); calls = failOn = peak = liveBytes = 0; }
};
template<int T> size_t Allocator<T>::calls = 0;
template<int T> size_t Allocator<T>::failOn = 0;
template<int T> size_t Allocator<T>::peak = 0;
template<int T> size_t Allocator<T>::liveBytes = 0;
template<int T> std::map<void*, size_t> Allocator<T>::live;

static std::vector<uint8_t> pcm(const std::vector<int>& samples) {
  std::vector<uint8_t> bytes;
  for (const int sample : samples) {
    const uint16_t value = uint16_t(sample);
    bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8));
  }
  return bytes;
}
static std::vector<uint8_t> readAll(official_voice::Reader& reader, const std::vector<size_t>& chunks) {
  std::vector<uint8_t> result;
  std::array<uint8_t, 4097> buffer{};
  size_t at = 0;
  assert(reader.read(nullptr, 1) == 0 && reader.read(buffer.data(), 0) == 0);
  for (;;) {
    const size_t capacity = chunks[at++ % chunks.size()]; assert(capacity && capacity <= buffer.size());
    const size_t count = reader.read(buffer.data(), capacity);
    assert(count <= capacity);
    if (!count) break;
    result.insert(result.end(), buffer.begin(), buffer.begin() + count);
  }
  assert(reader.read(buffer.data(), buffer.size()) == 0);
  return result;
}
static void appendChunks(Store& store, const std::vector<uint8_t>& input, const std::vector<size_t>& chunks) {
  size_t at = 0, part = 0;
  while (at < input.size()) {
    const size_t bytes = std::min(chunks[part++ % chunks.size()], input.size() - at);
    assert(store.append(input.data() + at, bytes) == Error::Ok); at += bytes;
  }
}
static std::vector<uint8_t> roundTrip(const std::vector<uint8_t>& input, const std::vector<size_t>& appended,
                                      const std::vector<size_t>& read) {
  Store store; assert(store.prepare() == Error::Ok && store.begin() == Error::Ok);
  appendChunks(store, input, appended);
  assert(store.recordedBytes() == input.size() && store.encodedBytes() == (input.size() / 2 + 1) / 2);
  assert(store.finish() == Error::Ok); Result error;
  auto reader = store.openReader(error); assert(reader && error == Error::Ok);
  return readAll(*reader, read);
}
static void knownVectorAndOddNibble() {
  // Canonical IMA predictor/index=0 vector, low nibble first:
  // codes 0,7,f,7,f,2,2,d,1 -> packed 70 7f 2f d2 01.
  const auto input = pcm({0,1000,-1000,32767,-32768,0,100,-100,0});
  const auto decoded = pcm({0,11,-19,44,-92,5,93,-85,-15});
  assert(roundTrip(input, {1024}, {4097}) == decoded);
  assert(roundTrip(input, {2,6,4}, {1}) == decoded);
  assert(roundTrip(input, {6,2}, {3,1,17}) == decoded);
  assert(roundTrip(pcm({32767}), {2}, {1}) == pcm({11}));
  assert(roundTrip(pcm({-32768}), {2}, {1}) == pcm({-11}));
}
static void stateAndLease() {
  using A = Allocator<0>; A::reset();
  Store store(A::hooks()); Result error;
  const auto input = pcm({1000,2000,-1000});
  assert(store.state() == State::Idle && store.capacityBytes() == 0 && store.encodedCapacityBytes() == 0);
  assert(store.begin() == Error::InvalidState && store.finish() == Error::InvalidState);
  assert(!store.openReader(error) && error == Error::InvalidState);
  assert(store.discard() == Error::Ok && store.discard() == Error::Ok);
  assert(store.prepare() == Error::Ok && store.capacityBytes() == 160000 && store.encodedCapacityBytes() == 40000);
  assert(A::calls == 1 && A::live.size() == 1 && A::liveBytes >= 40000 && A::liveBytes < 40512);
  assert(store.prepare() == Error::Ok && A::calls == 1); // Reuse, no duplicate 40k allocation.
  assert(store.begin() == Error::Ok && store.prepare() == Error::Busy);
  assert(!store.openReader(error) && error == Error::InvalidState);
  assert(store.append(input.data(), input.size()) == Error::Ok);
  assert(!store.openReader(error) && error == Error::InvalidState); // Unfinished takes never escape.
  assert(store.finish() == Error::Ok);
  auto reader = store.openReader(error); assert(reader && error == Error::Ok && store.readerActive());
  const auto calls = A::calls;
  assert(store.prepare() == Error::Busy && store.discard() == Error::Busy && store.begin() == Error::Busy);
  assert(store.append(input.data(), 2) == Error::Busy && store.finish() == Error::Busy);
  assert(!store.openReader(error) && error == Error::Busy && A::calls == calls);
  const auto decoded = readAll(*reader, {1,3,101});
  assert(decoded.size() == input.size() && store.readerActive());
  reader.reset(); assert(!store.readerActive() && A::live.size() == 1);
  reader = store.openReader(error); assert(reader && readAll(*reader, {2}) == decoded);
  reader.reset(); assert(store.discard() == Error::Ok && A::live.empty());
  assert(store.capacityBytes() == 0 && store.encodedBytes() == 0 && store.recordedBytes() == 0);
}
static void invalidAppendAndBound() {
  std::vector<int> values;
  for (int n = 0; n < 1201; ++n) values.push_back((n * 3791) % 65536 - 32768);
  const auto input = pcm(values);
  const auto baseline = roundTrip(input, {1024}, {4097});
  assert(roundTrip(input, {2,1024,6,4,510}, {1,3,7,19,1024}) == baseline);
  Store store; assert(store.prepare() == Error::Ok && store.begin() == Error::Ok);
  const auto first = pcm({1000});
  assert(store.append(first.data(), 2) == Error::Ok);
  for (const size_t count : {size_t(0), size_t(1), size_t(3), size_t(1026)}) {
    assert(store.append(input.data(), count) == Error::InvalidInput);
    assert(store.recordedBytes() == 2 && store.encodedBytes() == 1 && store.state() == State::Recording);
  }
  assert(store.append(nullptr, 2) == Error::InvalidInput && store.recordedBytes() == 2);
  assert(store.append(first.data(), 2) == Error::Ok && store.finish() == Error::Ok);
  Result error; auto reader = store.openReader(error); assert(reader);
  assert(readAll(*reader, {1}) == roundTrip(pcm({1000,1000}), {4}, {2}));
  reader.reset(); assert(store.discard() == Error::Ok);
  assert(store.prepare() == Error::Ok && store.begin() == Error::Ok);
  std::array<uint8_t, 1024> silence{};
  while (store.recordedBytes() < store.capacityBytes()) {
    const auto bytes = std::min(silence.size(), store.capacityBytes() - store.recordedBytes());
    assert(store.append(silence.data(), bytes) == Error::Ok);
  }
  assert(store.recordedBytes() == 160000 && store.encodedBytes() == 40000);
  assert(store.append(silence.data(), 2) == Error::InsufficientSpace && store.recordedBytes() == 160000);
  assert(store.finish() == Error::Ok); reader = store.openReader(error); assert(reader);
  const auto result = readAll(*reader, {4097});
  assert(result.size() == 160000 && std::all_of(result.begin(), result.end(), [](uint8_t b) { return b == 0; }));
  reader.reset(); assert(store.discard() == Error::Ok);
  assert(store.prepare() == Error::Ok && store.begin() == Error::Ok && store.finish() == Error::SizeMismatch);
  assert(store.state() == State::Failed && !store.openReader(error));
}
static int signedSample(const std::vector<uint8_t>& bytes, size_t sample) {
  const unsigned raw = unsigned(bytes[sample * 2]) | (unsigned(bytes[sample * 2 + 1]) << 8);
  return raw < 32768 ? int(raw) : int(raw) - 65536;
}
static void extremesAndRecovery() {
  std::vector<int> samples(96, 32767); samples.insert(samples.end(), 96, -32768);
  samples.insert(samples.end(), 96, 32767);
  const auto result = roundTrip(pcm(samples), {2,6,1024}, {1,17,3});
  assert(signedSample(result, 0) == 11);
  assert(signedSample(result, 95) > 32000 && signedSample(result, 191) < -32000 && signedSample(result, 287) > 32000);
}
static void allocationAndLifetime() {
  using A = Allocator<1>; using B = Allocator<2>; A::reset(); B::reset();
  const auto input = pcm({0,1000,-1000,32767,-32768});
  std::unique_ptr<official_voice::Reader> retained; Result error;
  {
    Store store(A::hooks()); A::failOn = 1;
    assert(store.prepare() == Error::OutOfMemory && store.state() == State::Failed && A::live.empty());
    A::failOn = 0;
    assert(store.prepare() == Error::Ok && store.begin() == Error::Ok && store.append(input.data(), input.size()) == Error::Ok);
    assert(store.finish() == Error::Ok);
    A::failOn = A::calls + 1;
    assert(!store.openReader(error) && error == Error::OutOfMemory && !store.readerActive());
    assert(store.state() == State::Finished && store.recordedBytes() == input.size() && A::live.size() == 1);
    A::failOn = 0; retained = store.openReader(error); assert(retained && A::live.size() == 2);
  }
  assert(A::live.size() == 2); // Reader retains the block after Store destruction.
  {
    Store unrelated(B::hooks()); assert(unrelated.prepare() == Error::Ok);
    assert(readAll(*retained, {1}) == roundTrip(input, {2}, {3}));
    std::thread cancellation([&]() { retained.reset(); }); cancellation.join();
    assert(A::live.empty() && B::live.size() == 1);
  }
  assert(B::live.empty() && A::peak < 41000 && B::peak < 41000);
  for (int n = 0; n < 64; ++n) {
    Store store(A::hooks()); assert(store.prepare() == Error::Ok && store.begin() == Error::Ok);
    assert(store.append(input.data(), input.size()) == Error::Ok && store.finish() == Error::Ok);
    auto reader = store.openReader(error); assert(reader);
    uint8_t byte; assert(reader->read(&byte, 1) == 1); // Cancel in the middle of a decoded sample.
    reader.reset(); assert(!store.readerActive() && store.discard() == Error::Ok && A::live.empty());
  }
  {
    Store aborted(A::hooks()); assert(aborted.prepare() == Error::Ok && aborted.begin() == Error::Ok);
    assert(aborted.append(input.data(), input.size()) == Error::Ok);
  }
  assert(A::live.empty());
  Store invalid(Allocation{nullptr, A::deallocate}); assert(invalid.prepare() == Error::InvalidInput);
  std::printf("RAM recording allocation peak=%zu bytes; no PCM expansion\n", A::peak);
}
int main() {
  knownVectorAndOddNibble(); stateAndLease(); invalidAppendAndBound(); extremesAndRecovery(); allocationAndLifetime();
  std::puts("RAM recording passed: canonical IMA, 5s bound, odd streams/nibbles, all-or-nothing appends, allocator failures, leases/cancellation/destruction");
}
