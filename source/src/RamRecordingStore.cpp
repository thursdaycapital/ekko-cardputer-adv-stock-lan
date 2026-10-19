#include "RamRecordingStore.h"
#include <algorithm>
#include <atomic>
#include <new>

namespace ram_recording {
namespace {
// Standard IMA 4-bit ADPCM adaptation tables. The private stream begins at
// predictor=0/index=0, low nibble first, and carries no WAV/block header.
constexpr uint16_t steps[89] = {
  7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
  130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,
  1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,
  6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};
constexpr int8_t indexDelta[8] = {-1,-1,-1,-1,2,4,6,8};
int decode(uint8_t code, int& predictor, int& index) {
  const int step = steps[index];
  int difference = step >> 3;
  if (code & 4) difference += step;
  if (code & 2) difference += step >> 1;
  if (code & 1) difference += step >> 2;
  predictor += code & 8 ? -difference : difference;
  predictor = std::max(-32768, std::min(32767, predictor));
  index = std::max(0, std::min(88, index + indexDelta[code & 7]));
  return predictor;
}
uint8_t encode(int sample, int& predictor, int& index) {
  int difference = sample - predictor;
  uint8_t code = difference < 0 ? 8 : 0;
  if (difference < 0) difference = -difference;
  int step = steps[index];
  if (difference >= step) { code |= 4; difference -= step; }
  step >>= 1;
  if (difference >= step) { code |= 2; difference -= step; }
  step >>= 1;
  if (difference >= step) code |= 1;
  decode(code, predictor, index);
  return code;
}
void* allocateDefault(size_t bytes) { return ::operator new(bytes, std::nothrow); }
void deallocateDefault(void* memory) { ::operator delete(memory); }

// This header outlives the Reader object's destructor and supplies the exact
// allocator for unique_ptr<official_voice::Reader>'s ordinary virtual delete.
struct alignas(std::max_align_t) ReaderMemory { void (*deallocate)(void*); };
}  // namespace

struct Store::Block {
  explicit Block(Allocation owner) : allocation(owner) {}
  Allocation allocation;
  std::atomic<unsigned> references{1}, readers{0};
  uint8_t encoded[kEncodedCapacityBytes];
  void release() {
    if (references.fetch_sub(1, std::memory_order_acq_rel) != 1) return;
    const auto deallocate = allocation.deallocate;
    this->~Block(); deallocate(this);
  }
};

class Store::OwnedReader final : public official_voice::Reader {
 public:
  static OwnedReader* create(Block* block, size_t bytes) {
    static_assert(alignof(OwnedReader) <= alignof(ReaderMemory), "Reader prefix must preserve object alignment");
    void* memory = block->allocation.allocate(sizeof(ReaderMemory) + sizeof(OwnedReader));
    if (!memory) return nullptr;
    auto* header = ::new (memory) ReaderMemory{block->allocation.deallocate};
    auto* reader = ::new (header + 1) OwnedReader(block, bytes);
    block->references.fetch_add(1, std::memory_order_relaxed);
    block->readers.fetch_add(1, std::memory_order_release);
    return reader;
  }
  ~OwnedReader() override {
    block_->readers.fetch_sub(1, std::memory_order_release);
    block_->release();
  }
  static void operator delete(void* memory) noexcept {
    auto* header = static_cast<ReaderMemory*>(memory) - 1;
    const auto deallocate = header->deallocate;
    header->~ReaderMemory(); deallocate(header);
  }
  size_t read(uint8_t* output, size_t capacity) override {
    if (!output || !capacity || position_ == total_) return 0;
    const size_t count = std::min(capacity, total_ - position_);
    for (size_t n = 0; n < count; ++n, ++position_) {
      if (!(position_ & 1)) {
        const size_t sample = position_ / 2;
        const uint8_t code = (block_->encoded[sample / 2] >> ((sample & 1) * 4)) & 15;
        pending_ = uint16_t(decode(code, predictor_, index_));
      }
      output[n] = uint8_t(pending_ >> ((position_ & 1) * 8));
    }
    return count;
  }
 private:
  OwnedReader(Block* block, size_t bytes) : block_(block), total_(bytes) {}
  Block* block_;
  size_t total_, position_ = 0;
  int predictor_ = 0, index_ = 0;
  uint16_t pending_ = 0;
};

Store::Store() : Store(Allocation{allocateDefault, deallocateDefault}) {}
Store::Store(Allocation allocation) : allocation_(allocation) {}
Store::~Store() { if (block_) block_->release(); }
bool Store::readerActive() const { return block_ && block_->readers.load(std::memory_order_acquire) != 0; }

Result Store::prepare() {
  if (readerActive() || state_ == State::Recording) return set(Error::Busy);
  if (!allocation_.allocate || !allocation_.deallocate) return set(Error::InvalidInput);
  if (!block_) {
    void* memory = allocation_.allocate(sizeof(Block));
    if (!memory) { state_ = State::Failed; return set(Error::OutOfMemory); }
    block_ = ::new (memory) Block(allocation_);
  }
  recorded_ = 0; predictor_ = index_ = 0; state_ = State::Prepared;
  return set(Error::Ok);
}
Result Store::begin() {
  if (readerActive()) return set(Error::Busy);
  if (!block_ || state_ != State::Prepared) return set(Error::InvalidState);
  recorded_ = 0; predictor_ = index_ = 0; state_ = State::Recording;
  return set(Error::Ok);
}
Result Store::append(const uint8_t* pcm, size_t bytes) {
  if (readerActive()) return set(Error::Busy);
  if (!block_ || state_ != State::Recording) return set(Error::InvalidState);
  if (!pcm || !bytes || (bytes & 1) || bytes > kAppendBytes) return set(Error::InvalidInput);
  if (bytes > kMaxPcmBytes - recorded_) return set(Error::InsufficientSpace);
  const size_t first = recorded_ / 2;
  for (size_t n = 0; n < bytes / 2; ++n) {
    const uint16_t raw = uint16_t(pcm[n * 2]) | (uint16_t(pcm[n * 2 + 1]) << 8);
    const int sample = raw < 32768 ? int(raw) : int(raw) - 65536;
    const uint8_t code = encode(sample, predictor_, index_);
    const size_t at = first + n;
    if (!(at & 1)) block_->encoded[at / 2] = code;
    else block_->encoded[at / 2] |= uint8_t(code << 4);
  }
  recorded_ += bytes;
  return set(Error::Ok);
}
Result Store::finish() {
  if (readerActive()) return set(Error::Busy);
  if (!block_ || state_ != State::Recording) return set(Error::InvalidState);
  if (!recorded_ || (recorded_ & 1)) { state_ = State::Failed; return set(Error::SizeMismatch); }
  state_ = State::Finished; return set(Error::Ok);
}
std::unique_ptr<official_voice::Reader> Store::openReader(Result& result) {
  if (readerActive()) { result = set(Error::Busy); return {}; }
  if (!block_ || state_ != State::Finished || !recorded_) { result = set(Error::InvalidState); return {}; }
  auto* reader = OwnedReader::create(block_, recorded_);
  if (!reader) { result = set(Error::OutOfMemory); return {}; }
  result = set(Error::Ok);
  return std::unique_ptr<official_voice::Reader>(reader);
}
Result Store::discard() {
  if (readerActive()) return set(Error::Busy);
  if (block_) { block_->release(); block_ = nullptr; }
  recorded_ = 0; predictor_ = index_ = 0; state_ = State::Idle;
  return set(Error::Ok);
}
const char* errorName(Error error) {
  switch (error) {
    case Error::Ok: return "ok";
    case Error::Busy: return "busy";
    case Error::InvalidState: return "invalid_state";
    case Error::InvalidInput: return "invalid_input";
    case Error::OutOfMemory: return "out_of_memory";
    case Error::InsufficientSpace: return "insufficient_space";
    case Error::SizeMismatch: return "size_mismatch";
  }
  return "unknown";
}
}  // namespace ram_recording
