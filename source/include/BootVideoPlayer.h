#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

struct NativeBootVideoAsset {
  const uint8_t* data = nullptr;
  size_t bytes = 0;
  const uint32_t* offsets = nullptr;
  size_t offsetCount = 0;
  uint16_t frames = 0, width = 0, height = 0;
  uint32_t durationMs = 0;
};

struct NativeBootVideoStats {
  bool attempted = false, completed = false, skipped = false;
  uint16_t frames_total = 0, frames_presented = 0;
  uint16_t decode_failures = 0, overruns = 0;
  uint32_t asset_bytes = 0, target_ms = 0, elapsed_ms = 0;
  uint32_t max_frame_ms = 0, max_decode_us = 0;
  const char* last_error = "none";
};

// Validate every range before any frame is read or hardware is called. Full
// JPEG codec/geometry validation belongs to the asset generator and decoder.
inline bool nativeBootVideoAssetValid(const NativeBootVideoAsset& asset) {
  if (!asset.data || !asset.offsets || !asset.frames || asset.frames > 180 ||
      !asset.bytes || asset.bytes > 300000 ||
      asset.offsetCount != size_t(asset.frames) + 1 ||
      asset.bytes > 300000 - asset.offsetCount * sizeof(uint32_t) ||
      asset.width != 240 || asset.height != 135 ||
      !asset.durationMs || asset.durationMs > 10000 || asset.offsets[0] != 0 ||
      asset.offsets[asset.frames] != asset.bytes) return false;
  for (size_t i = 0; i < asset.frames; ++i) {
    const uint32_t begin = asset.offsets[i], end = asset.offsets[i + 1];
    if (end > asset.bytes || begin >= end || end - begin < 4 ||
        asset.data[begin] != 0xFF || asset.data[begin + 1] != 0xD8 ||
        asset.data[end - 2] != 0xFF || asset.data[end - 1] != 0xD9) return false;
  }
  return true;
}

// Bounded, allocation-free scheduling. A successful present callback means a
// full frame transfer has completed, not merely that JPEG decoding succeeded.
// PollSkip must not enqueue navigation; Wait yields to the hardware scheduler.
template <class Clock, class Wait, class Present, class PollSkip>
NativeBootVideoStats nativePlayBootVideo(const NativeBootVideoAsset& asset,
                                        Clock clock, Wait wait, Present present,
                                        PollSkip pollSkip) {
  NativeBootVideoStats stats;
  stats.attempted = true;
  if (!nativeBootVideoAssetValid(asset)) {
    stats.last_error = "asset_invalid";
    return stats;
  }
  stats.frames_total = asset.frames;
  stats.asset_bytes = static_cast<uint32_t>(asset.bytes);
  stats.target_ms = asset.durationMs;
  const uint32_t start = clock();
  for (size_t i = 0; i < asset.frames; ++i) {
    if (pollSkip()) { stats.skipped = true; break; }
    const uint32_t before = clock();
    if (!present(asset.data + asset.offsets[i], asset.offsets[i + 1] - asset.offsets[i])) {
      ++stats.decode_failures;
      stats.last_error = "jpeg_decode_failed";
      break;
    }
    ++stats.frames_presented;
    stats.max_frame_ms = std::max(stats.max_frame_ms, uint32_t(clock() - before));
    const uint32_t deadline = uint32_t((uint64_t(i + 1) * asset.durationMs) / asset.frames);
    if (uint32_t(clock() - start) > deadline) ++stats.overruns;
    while (uint32_t(clock() - start) < deadline) {
      if (pollSkip()) { stats.skipped = true; break; }
      const uint32_t elapsed = uint32_t(clock() - start);
      if (elapsed >= deadline) break; // Hardware input scanning itself takes time.
      const uint32_t left = deadline - elapsed;
      wait(std::min<uint32_t>(left, 5));
    }
    if (stats.skipped) break;
    // Also yield when decoding itself consumed the entire frame budget.
    wait(1);
  }
  stats.elapsed_ms = uint32_t(clock() - start);
  stats.completed = !stats.skipped && !stats.decode_failures &&
                    stats.frames_presented == asset.frames;
  return stats;
}
