#pragma once
#include "OfficialStudioVoice.h"
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ram_recording {

constexpr size_t kSampleRate = 16000;
constexpr size_t kMaxSeconds = 5;
constexpr size_t kMaxPcmBytes = kSampleRate * kMaxSeconds * 2;
constexpr size_t kEncodedCapacityBytes = kMaxPcmBytes / 4;
constexpr size_t kAppendBytes = 1024;

enum class Error : uint8_t { Ok, Busy, InvalidState, InvalidInput, OutOfMemory, InsufficientSpace, SizeMismatch };
using Result = Error;
enum class State : uint8_t { Idle, Prepared, Recording, Finished, Failed };

// An instance-local, non-virtual allocation seam. Both functions must be
// non-throwing, return storage aligned for max_align_t, and remain callable
// until the final Reader is destroyed. No global allocator/test state is used.
struct Allocation {
  void* (*allocate)(size_t);
  void (*deallocate)(void*);
};

// Store calls/destruction belong to one owner task. A returned Reader alone
// may read/destruct on a worker; its reference/lease counters are atomic.
class Store {
 public:
  Store();
  explicit Store(Allocation allocation);
  ~Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  Result prepare();
  Result begin();
  // PCM16LE mono. Each even, non-empty <=1 KiB append is accepted completely
  // or rejected without changing any prior samples/encoder state.
  Result append(const uint8_t* pcm, size_t bytes);
  Result finish();
  std::unique_ptr<official_voice::Reader> openReader(Result& result);
  Result discard();
  bool readerActive() const;
  size_t capacityBytes() const { return block_ ? kMaxPcmBytes : 0; }
  size_t recordedBytes() const { return recorded_; }
  size_t encodedCapacityBytes() const { return block_ ? kEncodedCapacityBytes : 0; }
  size_t encodedBytes() const { return (recorded_ / 2 + 1) / 2; }
  State state() const { return state_; }
  Result result() const { return result_; }

 private:
  struct Block;
  class OwnedReader;
  Allocation allocation_;
  Block* block_ = nullptr;
  size_t recorded_ = 0;
  int predictor_ = 0, index_ = 0;
  State state_ = State::Idle;
  Result result_ = Error::Ok;
  Result set(Result error) { result_ = error; return error; }
};

const char* errorName(Error error);

}  // namespace ram_recording
