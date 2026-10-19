#include "BootVideoPlayer.h"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

struct Fixture {
  std::array<uint8_t, 60 * 6> data{};
  std::array<uint32_t, 61> offsets{};
  NativeBootVideoAsset asset;
  Fixture() {
    for (size_t i = 0; i < 60; ++i) {
      offsets[i] = i * 6;
      const uint8_t frame[]{0xFF, 0xD8, uint8_t(i), 0, 0xFF, 0xD9};
      std::memcpy(data.data() + i * 6, frame, sizeof(frame));
    }
    offsets[60] = data.size();
    asset.data = data.data(); asset.bytes = data.size();
    asset.offsets = offsets.data(); asset.offsetCount = offsets.size();
    asset.frames = 60; asset.width = 240; asset.height = 135; asset.durationMs = 4000;
  }
};

int main() {
  Fixture f;
  assert(nativeBootVideoAssetValid(f.asset));
  for (uint32_t initial : {uint32_t(0), uint32_t(0xFFFFFFFFu - 1000)}) {
    uint32_t now = initial, calls = 0, largestWait = 0;
    const auto s = nativePlayBootVideo(f.asset, [&]() { return now; },
        [&](uint32_t ms) { assert(ms > 0 && ms <= 5); now += ms; largestWait = std::max(largestWait, ms); },
        [&](const uint8_t* data, size_t bytes) {
          assert(data == f.data.data() + calls * 6 && bytes == 6);
          assert(uint32_t(now - initial) >= calls * 4000 / 60);
          ++calls; now += 6; return true;
        }, []() { return false; });
    assert(s.attempted && s.completed && !s.skipped && calls == 60);
    assert(s.frames_presented == 60 && s.frames_total == 60);
    assert(s.elapsed_ms == 4001 && s.max_frame_ms == 6 && s.overruns == 0);
    assert(s.decode_failures == 0 && largestWait == 5);
  }
  {
    uint32_t now = 0, calls = 0;
    const auto s = nativePlayBootVideo(f.asset, [&]() { return now; },
        [&](uint32_t ms) { now += ms; },
        [&](const uint8_t*, size_t) { ++calls; return true; }, []() { return true; });
    assert(s.skipped && !s.completed && calls == 0 && s.elapsed_ms == 0);
  }
  {
    uint32_t now = 0;
    const auto s = nativePlayBootVideo(f.asset, [&]() { return now; },
        [&](uint32_t ms) { now += ms; },
        [](const uint8_t*, size_t) { return true; }, [&]() { return now >= 30; });
    assert(s.skipped && !s.completed && s.frames_presented == 1 && s.elapsed_ms == 30);
  }
  {
    uint32_t now = 0, calls = 0;
    const auto s = nativePlayBootVideo(f.asset, [&]() { return now; },
        [&](uint32_t ms) { now += ms; },
        [&](const uint8_t*, size_t) { return calls++ != 16; }, []() { return false; });
    assert(!s.completed && !s.skipped && s.frames_presented == 16 && calls == 17);
    assert(s.decode_failures == 1 && std::strcmp(s.last_error, "jpeg_decode_failed") == 0);
  }
  {
    uint32_t now = 0;
    const auto s = nativePlayBootVideo(f.asset, [&]() { return now; },
        [&](uint32_t ms) { now += ms; },
        [&](const uint8_t*, size_t) { now += 72; return true; }, []() { return false; });
    assert(s.completed && s.overruns == 60 && s.max_frame_ms == 72 && s.elapsed_ms > 4000);
  }
  {
    uint32_t now = 0, presented = 0;
    const auto s = nativePlayBootVideo(f.asset, [&]() { return now; },
        [&](uint32_t ms) {
          if (ms > 1) assert(now < presented * 4000 / 60);
          now += ms;
        },
        [&](const uint8_t*, size_t) { ++presented; return true; },
        [&]() { now += 37; return false; }); // Input I2C scan can cross a deadline.
    assert(s.completed && presented == 60);
  }
  // Invalid sizes/offsets must fail before either decoder or wait is invoked.
  const auto rejected = [](const NativeBootVideoAsset& asset) {
    const auto s = nativePlayBootVideo(asset, []() { return uint32_t(0); },
        [](uint32_t) { assert(false); },
        [](const uint8_t*, size_t) { assert(false); return true; }, []() { return false; });
    assert(!s.completed && s.frames_presented == 0 && std::strcmp(s.last_error, "asset_invalid") == 0);
  };
  auto bad = f.asset; bad.offsetCount = 1; rejected(bad);
  bad = f.asset; bad.frames = 0; rejected(bad);
  bad = f.asset; bad.durationMs = 0; rejected(bad);
  bad = f.asset; bad.durationMs = 10001; rejected(bad);
  bad = f.asset; bad.width = 241; rejected(bad);
  bad = f.asset; bad.bytes = 300 * 1024 + 1; rejected(bad);
  bad = f.asset; bad.data = nullptr; rejected(bad);
  f.offsets[1] = f.data.size() + 1; rejected(f.asset); f.offsets[1] = 6;
  f.offsets[1] = 0; rejected(f.asset); f.offsets[1] = 6;
  f.data[0] = 0; rejected(f.asset); f.data[0] = 0xFF;
  f.data.back() = 0; rejected(f.asset);
  std::cout << "Boot scheduling: 60 frames/4s, millis rollover, skip, failure, slow decode and bounds passed (host callbacks, not actual LCD)\n";
}
