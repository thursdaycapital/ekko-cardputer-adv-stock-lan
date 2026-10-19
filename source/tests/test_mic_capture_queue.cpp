#include "MicCaptureQueue.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <thread>

using namespace mic_capture;

static int16_t pattern(uint32_t sequence, size_t index) {
  return static_cast<int16_t>((sequence * 31 + uint32_t(index) * 17) % 32767);
}
static void fill(const Claim& claim, uint32_t sequence) {
  assert(claim && claim.samples == kSamples);
  assert(reinterpret_cast<uintptr_t>(claim.data) % 4 == 0);
  for (size_t n = 0; n < kSamples; ++n) claim.data[n] = pattern(sequence, n);
}
static void check(const Lease& lease, uint32_t sequence) {
  assert(lease && lease.samples() == kSamples);
  for (size_t n = 0; n < kSamples; ++n) assert(lease.data()[n] == pattern(sequence, n));
}

static void ownershipAndFifo() {
  Queue queue;
  assert(queue.stats().freeSlots == kSlots && !queue.take());
  const Claim first = queue.claim(); fill(first, 0);
  assert(!queue.take()); // A claimed SDK buffer is not complete yet.
  assert(queue.completeExactPointer(first.data, first.samples));
  // Read that initial completed row; a lease pins its buffer until released.
  Lease lease = queue.take(); check(lease, 0);
  assert(!queue.take() && !queue.resetAfterJoin());
  Lease moved(std::move(lease)); assert(!lease); check(moved, 0);
  lease = std::move(moved); assert(!moved); check(lease, 0);
  Lease* alias = &lease; lease = std::move(*alias); check(lease, 0);
  assert(queue.release(lease) && !lease);
  lease.reset(); assert(queue.stats().consumedBlocks == 1);
  for (uint32_t n = 1; n <= kSlots; ++n) {
    const Claim claim = queue.claim(); fill(claim, n);
    assert(queue.completeExactPointer(claim.data, claim.samples));
  }
  assert(queue.stats().readyBlocks == kSlots && queue.stats().maxReadyBlocks == kSlots);
  for (uint32_t n = 1; n <= kSlots; ++n) {
    auto row = queue.take(); check(row, n);
    assert(!queue.take()); row.reset();
  }
  const auto stats = queue.stats();
  assert(stats.producedBlocks == kSlots + 1 && stats.consumedBlocks == stats.producedBlocks);
  assert(stats.freeSlots == kSlots && !stats.readyBlocks && !stats.borrowedBlocks && !stats.queuedBlocks);
  assert(queue.resetAfterJoin() && queue.stats().resets == 1 && !queue.failed());
}

static void failuresAreSticky() {
  { Queue queue; auto claim = queue.claim();
    assert(!queue.claim() && queue.error() == Error::InvalidClaim);
    assert(!queue.completeExactPointer(claim.data, claim.samples));
    assert(!queue.take() && queue.stats().invalidClaims == 1);
    assert(queue.resetAfterJoin() && queue.stats().discardedAfterJoin == 1);
  }
  for (unsigned kind = 0; kind < 7; ++kind) {
    Queue queue; auto claim = queue.claim(); fill(claim, 1);
    int16_t foreign = 0;
    const int16_t* pointer = kind == 0 ? nullptr : kind == 1 ? &foreign :
        kind == 2 ? claim.data + 1 : claim.data;
    const size_t samples = kind == 3 ? 0 : kind == 4 ? kSamples - 1 :
        kind == 5 ? kSamples + 1 : kSamples;
    if (kind == 6) assert(queue.completeExactPointer(claim.data, kSamples));
    assert(!queue.completeExactPointer(pointer, samples));
    assert(queue.error() == Error::InvalidCallback && queue.stats().invalidCallbacks == 1);
    assert(!queue.take() && !queue.claim());
    queue.failSdkSubmission(); assert(queue.error() == Error::InvalidCallback);
    assert(queue.resetAfterJoin() && !queue.failed());
    auto next = queue.claim(); fill(next, 2);
    assert(queue.completeExactPointer(next.data, kSamples));
    auto row = queue.take(); check(row, 2); row.reset();
  }
  { Queue queue;
    for (uint32_t n = 0; n < kSlots; ++n) {
      const auto claim = queue.claim(); fill(claim, n);
      assert(queue.completeExactPointer(claim.data, kSamples));
    }
    assert(!queue.claim() && queue.error() == Error::Overrun && queue.stats().overruns == 1);
    assert(!queue.take()); // Failure cannot expose a successful audio prefix.
    assert(queue.resetAfterJoin() && queue.stats().discardedAfterJoin == kSlots);
  }
  { Queue queue; const auto claim = queue.claim(); fill(claim, 0);
    assert(queue.completeExactPointer(claim.data, kSamples));
    auto row = queue.take();
    for (uint32_t n = 1; n < kSlots; ++n) {
      const auto next = queue.claim(); fill(next, n);
      assert(queue.completeExactPointer(next.data, kSamples));
    }
    assert(!queue.claim() && queue.error() == Error::Overrun);
    check(row, 0); row.reset(); // Never overwrite a pinned consumer buffer.
    assert(queue.stats().consumedBlocks == 1 && queue.resetAfterJoin());
  }
  { Queue queue; queue.claim(); queue.failSdkSubmission();
    assert(queue.error() == Error::SdkRejected && !queue.take());
    assert(queue.resetAfterJoin() && queue.stats().discardedAfterJoin == 1);
  }
  { Queue queue; Lease empty;
    assert(!queue.release(empty) && queue.error() == Error::InvalidRelease);
    assert(queue.stats().invalidReleases == 1 && queue.resetAfterJoin());
  }
  { Queue first, second; const auto claim = second.claim(); fill(claim, 9);
    assert(second.completeExactPointer(claim.data, kSamples)); auto row = second.take();
    assert(!first.release(row) && first.error() == Error::InvalidRelease);
    assert(row && !second.failed()); check(row, 9); row.reset();
    assert(second.stats().freeSlots == kSlots);
  }
}

static void stopPreservesOnlyCompletedBlocks() {
  Queue queue;
  for (uint32_t n = 0; n < 3; ++n) {
    const auto claim = queue.claim(); fill(claim, n);
    assert(queue.completeExactPointer(claim.data, kSamples));
  }
  const auto unfinished = queue.claim(); fill(unfinished, 99);
  queue.requestStop(); assert(queue.stopped() && !queue.failed());
  assert(!queue.claim() && !queue.completeExactPointer(unfinished.data, kSamples));
  assert(!queue.completeExactPointer(unfinished.data, kSamples / 2));
  for (uint32_t n = 0; n < 3; ++n) {
    auto row = queue.take(); check(row, n); assert(queue.hasLease());
    assert(!queue.resetAfterJoin()); row.reset(); assert(!queue.hasLease());
  }
  assert(!queue.take() && queue.stats().producedBlocks == 3 && queue.stats().consumedBlocks == 3);
  assert(queue.stats().queuedBlocks == 1 && !queue.stats().invalidCallbacks);
  // Synthetic SDK join has returned. The incomplete request is not consumed.
  assert(queue.resetAfterJoin() && !queue.stopped() && !queue.failed());
  assert(queue.stats().discardedAfterJoin == 1 && queue.stats().freeSlots == kSlots);
}

static void realSpscLongSequence() {
  auto queue = std::unique_ptr<Queue>(new Queue);
  constexpr uint32_t iterations = 30000;
  std::atomic<uint32_t> consumed{0};
  // Initial main submission hands producer ownership to the SDK thread.
  Claim first = queue->claim(); fill(first, 0);
  std::thread producer([&]() {
    Claim current = first;
    for (uint32_t n = 0; n < iterations; ++n) {
      assert(queue->completeExactPointer(current.data, current.samples));
      if (n + 1 == iterations) break;
      // A deterministic fake SDK pace, not a production retry on overrun.
      // Keep <=6 outstanding slots, leaving scheduling freedom to both tasks.
      while (n + 1 - consumed.load(std::memory_order_acquire) >= 6) std::this_thread::yield();
      current = queue->claim(); fill(current, n + 1);
      if (n % 11 == 0) std::this_thread::yield();
    }
  });
  std::thread consumer([&]() {
    for (uint32_t n = 0; n < iterations;) {
      auto row = queue->take();
      if (!row) { assert(!queue->failed()); std::this_thread::yield(); continue; }
      check(row, n); assert(!queue->take());
      if (n % 17 == 0) std::this_thread::yield();
      row.reset(); consumed.store(++n, std::memory_order_release);
    }
  });
  producer.join(); consumer.join(); // Synthetic SDK end/join before reset/destruct.
  const auto stats = queue->stats();
  assert(!queue->failed() && stats.producedBlocks == iterations && stats.consumedBlocks == iterations);
  assert(stats.maxReadyBlocks >= 1 && stats.maxReadyBlocks <= 6 && stats.freeSlots == kSlots);
  assert(!stats.overruns && !stats.invalidCallbacks && !stats.invalidReleases && !stats.invalidClaims);
  assert(queue->resetAfterJoin() && !queue->stats().discardedAfterJoin);
}

int main() {
  ownershipAndFifo(); failuresAreSticky(); stopPreservesOnlyCompletedBlocks(); realSpscLongSequence();
  std::printf("Mic capture queue passed: exact callbacks, FIFO leases, sticky failures, join/reset, 30000 two-thread chunks; PCM=%zu queue=%zu bytes\n",
              kPcmBytes, sizeof(Queue));
}
