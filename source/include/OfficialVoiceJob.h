#pragma once

#include "OfficialStudioVoice.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace official_voice_job {
constexpr size_t kBaseUrlBytes=512,kJwtBytes=4096,kProfileBytes=224,kSessionBytes=224;
constexpr size_t kTextBytes=1024,kTranscriptBytes=512,kPcmSamples=1024,kPcmSlots=2;
constexpr uint32_t kTaskStackBytes=32*1024;
enum class Type:uint8_t { Stt, Tts };
enum class StartResult:uint8_t { Started, Busy, Invalid, NoMemory, NotSupported };
enum class Poll:uint8_t { Idle, Pending, Ready };

struct Context {
  std::array<char,kBaseUrlBytes+1> baseUrl{};
  std::array<char,kJwtBytes+1> userJwt{};
  std::array<char,kProfileBytes+1> profile{};
  std::array<char,kSessionBytes+1> session{};
  uint32_t epoch=0;
  ~Context(); // Wipes this owned JWT copy, without logging.
};
official_voice::Error makeContext(Context&,const char* baseUrl,const char* userJwt,
                                 const char* profile,const char* session,uint32_t epoch);
bool validContext(const Context&);

struct Output {
  official_voice::Result result;
  Type type=Type::Stt;
  uint32_t epoch=0;
  std::array<char,kProfileBytes+1> profile{};
  std::array<char,kSessionBytes+1> session{};
  // At most 512 UTF-8 bytes, plus NUL. Oversize is ResponseTooLarge, never a
  // truncated successful transcript. No JWT or TTS text is returned.
  std::array<char,kTranscriptBytes+1> transcript{};
  // Non-secret completion diagnostics; unavailable on host builds.
  // Heap/largest minima are sampled at worker start, PCM callbacks and end;
  // systemHeapLowWaterBytes is the SDK's since-boot internal-heap minimum.
  // ESP-IDF reports task stack high-water mark as free BYTES, not words.
  uint32_t workerStackFreeBytes=0;
  uint32_t internalHeapMinObservedBytes=0;
  uint32_t internalLargestBlockMinObservedBytes=0;
  uint32_t systemHeapLowWaterBytes=0;
  bool resourcesAvailable=false;
};

struct SharedState; // Opaque worker/queue storage, not a caller-owned pointer.
class Job;
class PcmBlock {
 public:
  PcmBlock()=default;
  ~PcmBlock();
  PcmBlock(const PcmBlock&)=delete;
  PcmBlock& operator=(const PcmBlock&)=delete;
  PcmBlock(PcmBlock&&)noexcept;
  PcmBlock& operator=(PcmBlock&&)noexcept;
  // A move-only lease on one of exactly two fixed slots. It holds the storage
  // alive even after Job destruction. reset() releases that slot to the worker.
  const int16_t* data()const;
  size_t samples()const;
  uint32_t sampleRate()const;
  uint32_t epoch()const;
  explicit operator bool()const{return state_!=nullptr;}
  void reset();
 private:
  SharedState* state_=nullptr;
  size_t slot_=0;
  friend class Job;
};

class Job {
 public:
  Job()=default;
  ~Job();
  Job(const Job&)=delete;
  Job& operator=(const Job&)=delete;
  StartResult startTts(const Context&,const char* text,
                       std::unique_ptr<official_voice::Transport> ={},official_voice::RawPcmFormat ={});
  // Transfers Reader ownership even when start is rejected. Reader read()
  // must return in bounded time; a blocking caller callback cannot be killed
  // safely. A nonce must be fresh/unpredictable (e.g. two esp_random() calls).
  StartResult startStt(const Context&,std::unique_ptr<official_voice::Reader>,size_t pcmBytes,
                       uint32_t rate,uint16_t channels,uint64_t nonce,
                       std::unique_ptr<official_voice::Transport> ={});
  // Operations are called serially from the main loop, never concurrently.
  // A block is MONO s16 (<=1024, even count), NOT a hardware stereo buffer.
  // Main must check epoch, shape/convert/copy samples, then promptly reset it.
  // Holding both slots intentionally applies cancellable worker backpressure.
  bool tryTakePcm(PcmBlock&);
  Poll pollDone(Output&); // Ready only when worker ended AND every lease drained.
  bool workerDone()const;
  bool queueDrained()const;
  bool busy()const;
  void cancel(); // Discards queued blocks; borrowed blocks live until reset().
  static size_t fixedJobHeapBytes(); // Includes 2 slots/context/output, not Reader/HTTP/TLS.
 private:
  SharedState* state_=nullptr;
  StartResult start(const Context&,Type,const char*,std::unique_ptr<official_voice::Reader>,
                    size_t,uint32_t,uint16_t,uint64_t,std::unique_ptr<official_voice::Transport>,
                    official_voice::RawPcmFormat);
  static void worker(void*);
};
// No automatic replay or global state, filesystem, UI or audio hardware access.
// Main coordinates this one job with selection/history. Cancellation wakes a
// full queue and silent HTTP waits. SDK DNS/connect/TLS and Reader callbacks are
// synchronous until they return; their late results are rejected. Destruction
// requests cancellation and detaches safely, without freeing a live worker.
} // namespace official_voice_job
