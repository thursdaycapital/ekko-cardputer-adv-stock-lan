#include "AudioStreamQueue.h"
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <new>
#include <thread>

static size_t allocations = 0;
void* operator new(std::size_t length) {
  ++allocations;
  if (void* result = std::malloc(length ? length : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](std::size_t length) { return ::operator new(length); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }

static void ownershipAndCancellation() {
  AudioStreamQueue queue;
  assert(queue.stats().free_slots == 3 && queue.stats().buffer_bytes == 6144);
  AudioStreamClaim claims[3];
  for (size_t i = 0; i < 3; ++i) {
    assert(queue.tryClaim(claims[i]));
    assert(claims[i].capacity_bytes == 2048 && reinterpret_cast<uintptr_t>(claims[i].data) % 4 == 0);
    assert(!queue.releaseExactPointer(claims[i].data)); // Writer still filling.
    for (size_t sample = 0; sample < 1024; ++sample) claims[i].data[sample] = static_cast<int16_t>(i + 1);
    assert(!queue.submit(claims[i], 0) && !queue.submit(claims[i], 2) && !queue.submit(claims[i], 2052));
    assert(queue.submit(claims[i], 2048));
    assert(!queue.submit(claims[i], 2048) && !queue.abandon(claims[i]));
  }
  AudioStreamClaim unavailable;
  assert(!queue.tryClaim(unavailable) && !unavailable.valid());
  assert(!queue.tryClaim(claims[0])); // Do not overwrite a live ownership handle.
  int16_t foreign = 0;
  assert(!queue.releaseExactPointer(nullptr) && !queue.releaseExactPointer(&foreign));
  assert(!queue.releaseExactPointer(claims[0].data + 1));
  assert(!queue.releaseExactPointer(reinterpret_cast<const char*>(claims[0].data) + 1));
  AudioStreamQueueStats stats = queue.stats();
  assert(stats.queued == 3 && stats.released == 0 && stats.in_flight == 3 && stats.pending_bytes == 6144);
  // A driver idle bit by itself reclaims nothing. Only its release callback
  // makes this exact buffer reusable, even if it is delayed beyond that bit.
  assert(!queue.tryClaim(unavailable));
  assert(queue.releaseExactPointer(claims[0].data));
  assert(!queue.releaseExactPointer(claims[0].data)); // Duplicate callback.
  assert(queue.tryClaim(unavailable) && unavailable.data == claims[0].data);
  assert(unavailable.generation != claims[0].generation);
  assert(!queue.abandon(claims[0]) && !queue.cancelUnqueued(claims[0]));
  assert(!queue.releaseExactPointer(unavailable.data)); // No late callback can release a writer.
  assert(queue.submit(unavailable, 1024));
  assert(queue.cancelUnqueued(unavailable)); // playRaw returned false: no callback will follow.
  assert(!queue.cancelUnqueued(unavailable));
  stats = queue.stats();
  assert(stats.queued == 3 && stats.released == 1 && stats.cancelled_unqueued == 1 && stats.in_flight == 2);
  assert(queue.tryClaim(unavailable) && queue.abandon(unavailable));
  assert(!queue.abandon(unavailable));
  // Teardown's synthetic join has ended all driver reads; callbacks for its
  // discarded requests are not promised by the SDK. Count drops, not release.
  queue.resetAfterJoin();
  stats = queue.stats();
  assert(stats.free_slots == 3 && !stats.in_flight && !stats.claimed);
  assert(stats.discarded_after_join == 2 && stats.released == 1 && stats.resets == 1);
  assert(stats.queued == stats.released + stats.discarded_after_join);
  assert(!queue.submit(claims[1], 1024)); // Old generation after join.
  assert(queue.tryClaim(unavailable) && queue.submit(unavailable, 4));
  assert(queue.releaseExactPointer(unavailable.data)); // Can happen before playRaw returns true.
  assert(queue.stats().queued == 4 && queue.stats().released == 2);
}

static void concurrentDriver() {
  AudioStreamQueue queue;
  struct Work { int16_t* data = nullptr; int16_t pattern = 0; };
  std::array<Work, 2> sdkSlots{};
  std::mutex mutex;
  std::condition_variable ready, room;
  size_t head = 0, count = 0;
  bool finished = false;
  std::thread consumer([&]() {
    for (;;) {
      Work request;
      {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&]() { return count || finished; });
        if (!count && finished) break;
        request = sdkSlots[head]; head = (head + 1) % sdkSlots.size(); --count;
      }
      room.notify_one();
      std::this_thread::yield();
      for (size_t sample = 0; sample < kAudioStreamSlotSamples; ++sample)
        assert(request.data[sample] == request.pattern); // No producer overwrite before release.
      assert(queue.releaseExactPointer(request.data));
    }
  });
  uint32_t accepted = 0, rejected = 0;
  for (uint32_t sequence = 1; sequence <= 6000; ++sequence) {
    AudioStreamClaim claim;
    while (!queue.tryClaim(claim)) std::this_thread::yield();
    const int16_t pattern = static_cast<int16_t>(sequence ^ 0x5533);
    for (size_t sample = 0; sample < kAudioStreamSlotSamples; ++sample) claim.data[sample] = pattern;
    assert(queue.submit(claim, kAudioStreamSlotBytes));
    if (sequence % 17 == 0) { // Rejected SDK request: neither retained nor callback.
      assert(queue.cancelUnqueued(claim)); ++rejected; continue;
    }
    {
      std::unique_lock<std::mutex> lock(mutex);
      room.wait(lock, [&]() { return count < sdkSlots.size(); });
      Work& slot = sdkSlots[(head + count) % sdkSlots.size()];
      slot.data = claim.data; slot.pattern = pattern; ++count;
    }
    ++accepted; ready.notify_one();
  }
  {
    std::lock_guard<std::mutex> lock(mutex); finished = true;
  }
  ready.notify_one(); consumer.join();
  AudioStreamQueueStats stats = queue.stats();
  assert(stats.queued == accepted && stats.released == accepted && stats.cancelled_unqueued == rejected);
  assert(stats.free_slots == 3 && !stats.in_flight && !stats.claimed);
  assert(stats.rejected_releases == 0);
  queue.resetAfterJoin();
  assert(queue.stats().discarded_after_join == 0);
}

static void allocationFree() {
  const size_t before = allocations;
  AudioStreamQueue queue;
  AudioStreamClaim claim;
  assert(queue.tryClaim(claim));
  claim.data[0] = claim.data[1] = 1;
  assert(queue.submit(claim, 4) && queue.releaseExactPointer(claim.data));
  queue.resetAfterJoin();
  assert(queue.stats().free_slots == 3 && allocations == before);
}

static void reusableOutputHandle() {
  AudioStreamQueue queue;
  AudioStreamClaim claim;
  int16_t* submitted[3]{};
  for (size_t i = 0; i < 3; ++i) {
    assert(queue.tryClaim(claim));
    assert(!queue.tryClaim(claim)); // Cannot lose a still-filling writer.
    submitted[i] = claim.data;
    assert(queue.submit(claim, 4));
    // The next iteration can replace this SDK-owned handle immediately,
    // without waiting for its release or for the whole channel to be idle.
  }
  assert(queue.stats().in_flight == 3);
  for (size_t i = 0; i < 3; ++i) assert(queue.releaseExactPointer(submitted[i]));
}

int main() {
  ownershipAndCancellation(); concurrentDriver(); allocationFree(); reusableOutputHandle();
  std::cout << "Audio queue: ownership/exact callback/SDK rejection/join/6000 concurrent chunks/zero-heap passed; sizeof="
            << sizeof(AudioStreamQueue) << '\n';
}
