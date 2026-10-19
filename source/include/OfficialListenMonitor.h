#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// Fixed policy for signed 16-bit MONO at exactly 16000 Hz. All methods and
// borrowed spans belong to one main task. No SDK, I/O, task, lock or allocation.
namespace official_listen {
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kWindowFrames = 128;
constexpr uint32_t kRmsThreshold = 420, kPeakThreshold = 1200, kActiveThreshold = 520;
constexpr uint32_t kActiveSamples = 24, kSpeechWindows = 36, kQuietWindows = 63;
constexpr uint32_t kMinCandidateFrames = 7200; // 450 ms, rounded only by whole windows.
constexpr uint32_t kMaxCandidateFrames = 8192;
constexpr size_t kPreRollFrames = 9216; // 576 ms, including the current block's tail.
constexpr size_t kMaxPostTriggerFrames = 1024;
constexpr bool validPolicy() {
  return kSampleRate == 16000 && kWindowFrames > 0 && kActiveSamples <= kWindowFrames &&
      kMinCandidateFrames <= kMaxCandidateFrames && kMaxCandidateFrames % kWindowFrames == 0 &&
      kSpeechWindows * kWindowFrames <= kMaxCandidateFrames &&
      kPreRollFrames >= kMaxCandidateFrames + kMaxPostTriggerFrames &&
      kRmsThreshold <= 32768 && kPeakThreshold <= 32768 && kActiveThreshold <= 32768;
}
static_assert(validPolicy(), "Automatic listening policy cannot preserve its qualified onset");

enum class Error : uint8_t { None, PostTriggerLimit };
struct Span { const int16_t* data = nullptr; size_t count = 0; };
struct PreRoll { Span first, second; size_t count = 0; };
struct Stats {
  uint64_t samples = 0, windows = 0, speechWindows = 0, triggers = 0;
  uint64_t candidateResets = 0, deadlineResets = 0, quietResets = 0;
  uint64_t triggerAtSample = 0, lastSumSquares = 0;
  uint32_t candidateFrames = 0, candidateSpeechWindows = 0, candidateQuietWindows = 0;
  uint32_t postTriggerFrames = 0;
  uint16_t lastPeak = 0, lastActiveSamples = 0;
};

class Monitor {
 public:
  Monitor() = default;
  Monitor(const Monitor&) = delete;
  Monitor& operator=(const Monitor&) = delete;
  // Borrowed spans become invalid at the next append/reset or destruction.
  // The caller allocates this ~18 KiB object only while listening, never on a
  // small task stack. Reset discards all published audio and VAD state.
  void reset() {
    write_ = count_ = 0; windowFrames_ = windowActive_ = windowPeak_ = 0;
    windowSquares_ = 0; candidate_ = triggered_ = false;
    error_ = Error::None; stats_ = Stats{};
  }

  // Feed every actual mono sample in order, independent of polling/chunks.
  // After triggered(), finish ONLY the current <=1024-frame capture block.
  // The extra bound is a guard, not permission to read a whole new block.
  // An exceeded bound is sticky failure: triggered()/ordered() cannot present
  // a truncated successful pre-roll. Reset is required before another take.
  bool append(int16_t sample) {
    if (!valid()) return false;
    if (triggered_ && stats_.postTriggerFrames >= kMaxPostTriggerFrames) {
      error_ = Error::PostTriggerLimit; return false;
    }
    audio_[write_] = sample; write_ = (write_ + 1) % kPreRollFrames;
    if (count_ < kPreRollFrames) ++count_;
    increment(stats_.samples);
    if (triggered_) { ++stats_.postTriggerFrames; return true; }
    const int32_t signedSample = sample;
    const uint32_t magnitude = static_cast<uint32_t>(signedSample < 0 ? -signedSample : signedSample);
    windowSquares_ += uint64_t(magnitude) * magnitude;
    if (magnitude > windowPeak_) windowPeak_ = static_cast<uint16_t>(magnitude);
    if (magnitude >= kActiveThreshold) ++windowActive_;
    if (++windowFrames_ == kWindowFrames) finishWindow();
    return true;
  }

  bool triggered() const { return valid() && triggered_; }
  bool valid() const { return error_ == Error::None; }
  Error error() const { return error_; }
  const Stats& stats() const { return stats_; }
  PreRoll ordered() const {
    PreRoll result;
    if (!valid() || !count_) return result;
    const size_t first = (write_ + kPreRollFrames - count_) % kPreRollFrames;
    const size_t length = count_ < kPreRollFrames - first ? count_ : kPreRollFrames - first;
    result.first.data = audio_.data() + first; result.first.count = length;
    if (length < count_) { result.second.data = audio_.data(); result.second.count = count_ - length; }
    result.count = count_; return result;
  }

 private:
  std::array<int16_t, kPreRollFrames> audio_{};
  size_t write_ = 0, count_ = 0;
  uint64_t windowSquares_ = 0;
  uint16_t windowFrames_ = 0, windowActive_ = 0, windowPeak_ = 0;
  bool candidate_ = false, triggered_ = false;
  Error error_ = Error::None;
  Stats stats_;
  static void increment(uint64_t& value) { if (value != std::numeric_limits<uint64_t>::max()) ++value; }
  void clearCandidate(bool quiet) {
    candidate_ = false;
    stats_.candidateFrames = stats_.candidateSpeechWindows = stats_.candidateQuietWindows = 0;
    increment(stats_.candidateResets);
    if (quiet) increment(stats_.quietResets); else increment(stats_.deadlineResets);
  }
  void finishWindow() {
    increment(stats_.windows);
    stats_.lastSumSquares = windowSquares_;
    stats_.lastPeak = windowPeak_; stats_.lastActiveSamples = windowActive_;
    // sqrt(sum/128) >= 420 is compared exactly without float or 16-bit squares.
    const bool speech = windowSquares_ >= uint64_t(kWindowFrames) * kRmsThreshold * kRmsThreshold &&
        windowPeak_ >= kPeakThreshold && windowActive_ >= kActiveSamples;
    if (speech) increment(stats_.speechWindows);
    if (!candidate_ && speech) {
      candidate_ = true;
      stats_.candidateFrames = stats_.candidateSpeechWindows = stats_.candidateQuietWindows = 0;
    }
    if (candidate_) {
      stats_.candidateFrames += kWindowFrames;
      if (speech) { ++stats_.candidateSpeechWindows; stats_.candidateQuietWindows = 0; }
      else ++stats_.candidateQuietWindows;
      if (stats_.candidateSpeechWindows >= kSpeechWindows &&
          stats_.candidateFrames >= kMinCandidateFrames && stats_.candidateFrames <= kMaxCandidateFrames) {
        triggered_ = true; stats_.triggerAtSample = stats_.samples; increment(stats_.triggers);
      } else if (stats_.candidateQuietWindows >= kQuietWindows) clearCandidate(true);
      else if (stats_.candidateFrames >= kMaxCandidateFrames) clearCandidate(false);
    }
    windowFrames_ = windowActive_ = windowPeak_ = 0; windowSquares_ = 0;
  }
};
static_assert(sizeof(Monitor) <= kPreRollFrames * sizeof(int16_t) + 256,
              "Listening monitor must keep bounded pre-roll state");
} // namespace official_listen
