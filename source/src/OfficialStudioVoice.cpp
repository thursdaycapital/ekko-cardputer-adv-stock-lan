#include "OfficialStudioVoice.h"
#include "ChatUtf8.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <new>

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "third_party/minimp3/minimp3.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "NativeTlsTrust.h"
#endif

namespace official_voice {
namespace {
constexpr size_t kMaxRecordingBytes = 16 * 1024 * 1024;
constexpr size_t kMaxResponseBytes = 16 * 1024 * 1024;
constexpr size_t kMaxTextBytes = 20000;
constexpr size_t kMp3InputBytes = 8192;
constexpr size_t kPcmChunkSamples = 1024;
constexpr char kTtsJsonSuffix[] = "\",\"options\":{\"format\":\"wav\"}}";
bool cancelled(const Cancellation* cancellation) { return cancellation && cancellation->cancelled(); }

size_t boundedLength(const char* value, size_t limit) {
  if (!value) return 0;
  size_t length = 0;
  while (length <= limit && value[length]) ++length;
  return length;
}

bool validBinding(const Binding& binding) {
  const size_t baseLength = boundedLength(binding.baseUrl, 512);
  const size_t tokenLength = boundedLength(binding.userJwt, 8192);
  const size_t profileLength = boundedLength(binding.profile, 256);
  if (!baseLength || baseLength > 512 || !tokenLength || tokenLength > 8192 ||
      !profileLength || profileLength > 256) return false;
  const char* authority = nullptr;
  if (std::strncmp(binding.baseUrl, "http://", 7) == 0) authority = binding.baseUrl + 7;
  if (std::strncmp(binding.baseUrl, "https://", 8) == 0) authority = binding.baseUrl + 8;
  if (!authority || !*authority || *authority == '/') return false;
  for (const char* c = binding.baseUrl; *c; ++c) {
    if (static_cast<unsigned char>(*c) <= 0x20 || *c == '\\' || *c == '@' || *c == '?' || *c == '#') return false;
  }
  for (const char* c = binding.userJwt; *c; ++c) {
    const unsigned char ch = static_cast<unsigned char>(*c);
    if (!(ch >= 'a' && ch <= 'z') && !(ch >= 'A' && ch <= 'Z') &&
        !(ch >= '0' && ch <= '9') && ch != '.' && ch != '_' && ch != '-') return false;
  }
  for (const char* c = binding.profile; *c; ++c) if (static_cast<unsigned char>(*c) < 0x20 || *c == 0x7f) return false;
  return true;
}

struct EncodedProfile {
  char value[256 * 3 + 1]{};
  explicit EncodedProfile(const char* profile) {
    const char* hex = "0123456789ABCDEF";
    size_t length = 0;
    for (const auto* c = reinterpret_cast<const unsigned char*>(profile); *c; ++c) {
      if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
          (*c >= '0' && *c <= '9') || *c == '_' || *c == '-') value[length++] = static_cast<char>(*c);
      else { value[length++] = '%'; value[length++] = hex[*c >> 4]; value[length++] = hex[*c & 15]; }
    }
  }
};

Error httpError(int status) {
  if (status == 401 || status == 403) return Error::AuthenticationRequired;
  return status >= 200 && status < 300 ? Error::Ok : Error::HttpFailed;
}

void put16(uint8_t* out, uint16_t value) { out[0] = uint8_t(value); out[1] = uint8_t(value >> 8); }
void put32(uint8_t* out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out[i] = uint8_t(value >> (8 * i));
}
uint16_t get16(const uint8_t* in) { return uint16_t(in[0]) | uint16_t(uint16_t(in[1]) << 8); }
uint32_t get32(const uint8_t* in) {
  return uint32_t(in[0]) | (uint32_t(in[1]) << 8) | (uint32_t(in[2]) << 16) | (uint32_t(in[3]) << 24);
}

bool validRate(uint32_t rate) { return rate >= 8000 && rate <= 96000; }

// Keep the large upstream scratch frame behind the caller's stack check even
// when the firmware uses LTO; it must not be inlined before that check.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
int decodeMp3Frame(mp3dec_t* decoder, const uint8_t* bytes, int length,
                   int16_t* pcm, mp3dec_frame_info_t* info) {
  return mp3dec_decode_frame(decoder, bytes, length, pcm, info);
}

bool mediaTypeEquals(const char* actual, const char* expected) {
  if (!actual) return false;
  while (*expected) {
    char c = *actual++;
    if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    if (c != *expected++) return false;
  }
  while (*actual == ' ' || *actual == '\t') ++actual;
  return !*actual || *actual == ';';
}

bool rawParametersMatch(const char* contentType, RawPcmFormat raw) {
  if (boundedLength(contentType, 256) > 256) return false;
  char parameters[257]{};
  std::memcpy(parameters, contentType, std::strlen(contentType));
  for (char* c = parameters; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c = char(*c + ('a' - 'A'));
  auto trim = [](char* value) -> char* {
    while (*value == ' ' || *value == '\t') ++value;
    size_t length = std::strlen(value);
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) value[--length] = 0;
    return value;
  };
  char* offset = std::strchr(parameters, ';');
  while (offset) {
    char* field = offset + 1;
    char* next = std::strchr(field, ';');
    if (next) *next = '\0';
    char* equal = std::strchr(field, '=');
    if (!equal) return false;
    *equal = '\0';
    char* key = trim(field); char* value = trim(equal + 1);
    const size_t length = std::strlen(value);
    if (length >= 2 && value[0] == '"' && value[length - 1] == '"') { value[length - 1] = '\0'; ++value; }
    if (!std::strcmp(key, "format") || !std::strcmp(key, "codec") || !std::strcmp(key, "codecs")) {
      if (std::strcmp(value, "s16le") && std::strcmp(value, "pcm_s16le")) return false;
    } else if (!std::strcmp(key, "endianness")) {
      if (std::strcmp(value, "little") && std::strcmp(value, "le")) return false;
    } else {
      uint32_t number = 0;
      if (!*value || std::strlen(value) > 6) return false;
      for (const char* c = value; *c; ++c) { if (*c < '0' || *c > '9') return false; number = number * 10 + uint32_t(*c - '0'); }
      if (!std::strcmp(key, "rate") || !std::strcmp(key, "samplerate") || !std::strcmp(key, "sample_rate")) { if (number != raw.sampleRate) return false; }
      else if (!std::strcmp(key, "channels")) { if (number != raw.channels) return false; }
      else if (!std::strcmp(key, "bits") || !std::strcmp(key, "bitdepth") || !std::strcmp(key, "bits_per_sample")) { if (number != 16) return false; }
      else return false;
    }
    offset = next;
  }
  return true;
}

class MultipartBody final : public Reader {
 public:
  MultipartBody(const Recording& recording, uint64_t nonce, const Cancellation* token) : recording_(recording), cancellation_(token) {
    char boundary[48];
    std::snprintf(boundary, sizeof(boundary), "----EkkoVoice%016llx", static_cast<unsigned long long>(nonce));
    std::snprintf(contentType_, sizeof(contentType_), "multipart/form-data; boundary=%s", boundary);
    std::snprintf(prefix_, sizeof(prefix_), "--%s\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\ntext\r\n--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\nContent-Type: audio/wav\r\n\r\n", boundary, boundary);
    std::snprintf(suffix_, sizeof(suffix_), "\r\n--%s--\r\n", boundary);
    prefixBytes_ = std::strlen(prefix_); suffixBytes_ = std::strlen(suffix_);
    std::memcpy(wave_, "RIFF", 4); put32(wave_ + 4, uint32_t(recording.pcmBytes + 36));
    std::memcpy(wave_ + 8, "WAVEfmt ", 8); put32(wave_ + 16, 16); put16(wave_ + 20, 1);
    put16(wave_ + 22, recording.channels); put32(wave_ + 24, recording.sampleRate);
    const uint16_t align = uint16_t(recording.channels * 2);
    put32(wave_ + 28, recording.sampleRate * align); put16(wave_ + 32, align); put16(wave_ + 34, 16);
    std::memcpy(wave_ + 36, "data", 4); put32(wave_ + 40, uint32_t(recording.pcmBytes));
  }
  size_t length() const { return prefixBytes_ + sizeof(wave_) + recording_.pcmBytes + suffixBytes_; }
  const char* contentType() const { return contentType_; }
  bool failed() const { return failed_; }
  size_t read(uint8_t* out, size_t capacity) override {
    if (!out || !capacity || failed_ || cancelled(cancellation_)) return 0;
    size_t used = 0;
    while (used < capacity && offset_ < length()) {
      if (cancelled(cancellation_)) return 0;
      const size_t waveStart = prefixBytes_, pcmStart = waveStart + sizeof(wave_);
      const size_t suffixStart = pcmStart + recording_.pcmBytes;
      size_t count = 0;
      if (offset_ < waveStart) count = copy(out + used, capacity - used, prefix_, waveStart, offset_);
      else if (offset_ < pcmStart) count = copy(out + used, capacity - used, wave_, sizeof(wave_), offset_ - waveStart);
      else if (offset_ < suffixStart) {
        const size_t requested = std::min(capacity - used, suffixStart - offset_);
        count = recording_.pcm->read(out + used, requested);
        if (cancelled(cancellation_)) return 0;
        if (!count || count > requested) { failed_ = true; break; }
      } else count = copy(out + used, capacity - used, suffix_, suffixBytes_, offset_ - suffixStart);
      used += count; offset_ += count;
    }
    return used;
  }
 private:
  static size_t copy(uint8_t* out, size_t capacity, const void* bytes, size_t length, size_t offset) {
    const size_t count = std::min(capacity, length - offset);
    std::memcpy(out, static_cast<const uint8_t*>(bytes) + offset, count); return count;
  }
  const Recording& recording_;
  const Cancellation* cancellation_;
  char prefix_[384]{}, suffix_[64]{}, contentType_[96]{};
  size_t prefixBytes_ = 0, suffixBytes_ = 0;
  uint8_t wave_[44] = {};
  size_t offset_ = 0;
  bool failed_ = false;
};

// Encodes JSON as it is read, rather than allocating a second text-sized body.
class JsonTextBody final : public Reader {
 public:
  JsonTextBody(const char* text, const Cancellation* token) : text_(text), cancellation_(token) {
    length_ = 9 + sizeof(kTtsJsonSuffix) - 1;  // {"text":" + requested container.
    for (const auto* c = reinterpret_cast<const unsigned char*>(text); *c; ++c)
      length_ += *c < 0x20 ? 6 : (*c == '"' || *c == '\\' ? 2 : 1);
  }
  size_t length() const { return length_; }
  size_t read(uint8_t* out, size_t capacity) override {
    if (cancelled(cancellation_)) return 0;
    size_t used = 0;
    while (used < capacity) {
      if (pendingAt_ < pendingSize_) { out[used++] = pending_[pendingAt_++]; continue; }
      pendingAt_ = pendingSize_ = 0;
      if (stage_ == 0) {
        const char* prefix = "{\"text\":\"";
        if (prefixAt_ < 9) { out[used++] = uint8_t(prefix[prefixAt_++]); continue; }
        stage_ = 1;
      }
      if (stage_ == 1 && text_[textAt_]) {
        const unsigned char c = static_cast<unsigned char>(text_[textAt_++]);
        if (c < 0x20) {
          const char* hex = "0123456789abcdef";
          pending_[0] = '\\'; pending_[1] = 'u'; pending_[2] = pending_[3] = '0';
          pending_[4] = uint8_t(hex[c >> 4]); pending_[5] = uint8_t(hex[c & 15]); pendingSize_ = 6;
        } else if (c == '"' || c == '\\') {
          pending_[0] = '\\'; pending_[1] = c; pendingSize_ = 2;
        } else { out[used++] = c; }
        continue;
      }
      if (stage_ == 1) stage_ = 2;
      if (stage_ == 2 && suffixAt_ < sizeof(kTtsJsonSuffix) - 1)
        out[used++] = uint8_t(kTtsJsonSuffix[suffixAt_++]);
      else break;
    }
    return used;
  }
 private:
  const char* text_;
  const Cancellation* cancellation_;
  size_t length_ = 0, textAt_ = 0, prefixAt_ = 0, pendingAt_ = 0, pendingSize_ = 0, suffixAt_ = 0;
  uint8_t pending_[6] = {};
  unsigned stage_ = 0;
};

class TranscriptSink final : public ResponseSink {
 public:
  TranscriptSink(char* text, size_t capacity, const Cancellation* token) : text_(text), capacity_(capacity), cancellation_(token) { text_[0] = '\0'; }
  bool begin(const ResponseHeaders& h) override {
    result.httpStatus = h.status; result.error = httpError(h.status);
    if (cancelled(cancellation_)) { result.error = Error::Cancelled; return false; }
    if (result.error != Error::Ok) return false;
    if (!mediaTypeEquals(h.contentType, "text/plain")) {
      result.error = Error::UnsupportedMedia; return false;
    }
    expected_ = h.contentLength;
    if (h.contentLength >= int64_t(capacity_)) { result.error = Error::ResponseTooLarge; return false; }
    return true;
  }
  bool write(const uint8_t* bytes, size_t count) override {
    if (cancelled(cancellation_)) { result.error = Error::Cancelled; return false; }
    if (count > capacity_ - result.textBytes - 1) { result.error = Error::ResponseTooLarge; return false; }
    if (std::memchr(bytes, 0, count)) { result.error = Error::InvalidText; return false; }
    std::memcpy(text_ + result.textBytes, bytes, count); result.textBytes += count; text_[result.textBytes] = '\0'; return true;
  }
  Result result;
  Result finish(bool sent) {
    if (cancelled(cancellation_)) result.error = Error::Cancelled;
    if (result.ok() && (!sent || (expected_ >= 0 && int64_t(result.textBytes) != expected_))) result.error = Error::TransportFailed;
    if (result.ok() && !result.textBytes) result.error = Error::InvalidText;
    if (result.ok() && !native_chat::validUtf8(text_, result.textBytes)) result.error = Error::InvalidText;
    return result;
  }
 private:
  char* text_;
  size_t capacity_;
  const Cancellation* cancellation_;
  int64_t expected_ = -1;
};
}  // namespace

struct AudioDecoder::State {
  enum class Format { None, Mp3, Wave, Raw } format = Format::None;
  struct Mp3 {
    mp3dec_t decoder{};
    uint8_t input[kMp3InputBytes]{};
    int16_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
    size_t buffered = 0;
    uint8_t probe[10]{};
    size_t probeBytes = 0;
    uint32_t skipTag = 0;
    bool probed = false;
  };
  Mp3* mp3 = nullptr;
  PcmCallback callback = nullptr;
  void* context = nullptr;
  const Cancellation* cancellation = nullptr;
  Result result;
  bool begun = false, finished = false;
  size_t received = 0, monoCount = 0;
  int16_t mono[kPcmChunkSamples]{};
  uint16_t channels = 0;
  uint8_t sampleBytes[4]{};
  size_t sampleUsed = 0;
  enum class WaveStage { Riff, Chunk, Format, Skip, Data, Padding } waveStage = WaveStage::Riff;
  uint8_t header[40]{};
  size_t headerUsed = 0, headerTarget = 12;
  uint32_t riffRemaining = 0, chunkRemaining = 0, chunkSize = 0;
  bool haveFormat = false, haveData = false;
  bool streamingRiff = false, streamingData = false;
#ifdef ARDUINO
  TaskHandle_t decodeTask = nullptr;
#endif
  ~State() { delete mp3; }

  Error fail(Error error) { if (result.error == Error::Ok) result.error = error; return result.error; }
  bool emit(bool final = false) {
    if (cancelled(cancellation)) { fail(Error::Cancelled); return false; }
    if (final && (monoCount & 1)) mono[monoCount++] = 0;
    const size_t count = final ? monoCount : (monoCount & ~size_t(1));
    if (!count) return true;
    if (!callback(context, mono, count, result.sampleRate)) { fail(Error::Cancelled); return false; }
    result.samples += count;
    if (monoCount != count) mono[0] = mono[count];
    monoCount -= count; return true;
  }
  bool sample(int16_t value) {
    mono[monoCount++] = value;
    return monoCount < kPcmChunkSamples || emit();
  }
  bool pcm(const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) {
      sampleBytes[sampleUsed++] = bytes[i];
      if (sampleUsed == size_t(channels * 2)) {
        int32_t value = static_cast<int16_t>(get16(sampleBytes));
        if (channels == 2) value = (value + static_cast<int16_t>(get16(sampleBytes + 2))) / 2;
        sampleUsed = 0;
        if (!sample(static_cast<int16_t>(value))) return false;
      }
    }
    return true;
  }
  bool formatPcm(const uint8_t* b, size_t size) {
    if (size < 16) return false;
    const uint16_t tag = get16(b), ch = get16(b + 2), bits = get16(b + 14);
    const uint32_t rate = get32(b + 4);
    if ((ch != 1 && ch != 2) || bits != 16 || !validRate(rate) || get16(b + 12) != ch * 2 || get32(b + 8) != rate * ch * 2) return false;
    if (tag == 0xfffe) {
      const uint8_t pcmGuid[16] = {1,0,0,0,0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
      if (size < 40 || get16(b + 16) < 22 || get16(b + 18) != 16 || std::memcmp(b + 24, pcmGuid, 16)) return false;
    } else if (tag != 1) return false;
    if (haveFormat && (channels != ch || result.sampleRate != rate)) return false;
    channels = ch; result.sampleRate = rate; haveFormat = true; return true;
  }
  bool nextWaveChunk() {
    waveStage = (chunkSize & 1) ? WaveStage::Padding : WaveStage::Chunk;
    headerUsed = 0; headerTarget = 8; return true;
  }
  bool wave(const uint8_t* bytes, size_t count) {
    size_t offset = 0;
    while (offset < count) {
      if (waveStage == WaveStage::Riff || waveStage == WaveStage::Chunk) {
        const size_t part = std::min(count - offset, headerTarget - headerUsed);
        if (waveStage == WaveStage::Chunk && !streamingRiff) {
          if (part > riffRemaining) { fail(Error::InvalidAudio); return false; }
          riffRemaining -= uint32_t(part);
        }
        std::memcpy(header + headerUsed, bytes + offset, part); headerUsed += part; offset += part;
        if (headerUsed < headerTarget) continue;
        if (waveStage == WaveStage::Riff) {
          if (std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4) || get32(header + 4) < 4) { fail(Error::InvalidAudio); return false; }
          // Non-seekable TTS WAV streams use UINT32_MAX for RIFF/data size.
          // Their end is the verified HTTP body end, still bounded globally.
          streamingRiff = get32(header + 4) == UINT32_MAX;
          riffRemaining = streamingRiff ? 0 : get32(header + 4) - 4;
          waveStage = WaveStage::Chunk; headerUsed = 0; headerTarget = 8; continue;
        }
        chunkSize = chunkRemaining = get32(header + 4);
        streamingData = chunkSize == UINT32_MAX;
        if (streamingData && (!streamingRiff || std::memcmp(header, "data", 4))) { fail(Error::InvalidAudio); return false; }
        if (!streamingRiff && uint64_t(chunkSize) + (chunkSize & 1) > riffRemaining) { fail(Error::InvalidAudio); return false; }
        if (std::memcmp(header, "fmt ", 4) == 0) { waveStage = WaveStage::Format; headerUsed = 0; }
        else if (std::memcmp(header, "data", 4) == 0) {
          if (!haveFormat || sampleUsed || (!streamingData && chunkSize % (channels * 2))) { fail(Error::InvalidAudio); return false; }
          haveData = true; waveStage = WaveStage::Data; headerUsed = 0;
        } else waveStage = WaveStage::Skip;
        if (!chunkRemaining) nextWaveChunk();
        continue;
      }
      if (waveStage == WaveStage::Padding) {
        if (!streamingRiff && !riffRemaining) { fail(Error::InvalidAudio); return false; }
        if (!streamingRiff) --riffRemaining;
        ++offset; waveStage = WaveStage::Chunk; headerUsed = 0; continue;
      }
      const size_t part = streamingData ? count - offset : std::min(count - offset, size_t(chunkRemaining));
      if (waveStage == WaveStage::Data && !pcm(bytes + offset, part)) return false;
      if (waveStage == WaveStage::Format && headerUsed < sizeof(header)) {
        const size_t keep = std::min(part, sizeof(header) - headerUsed);
        std::memcpy(header + headerUsed, bytes + offset, keep); headerUsed += keep;
      }
      offset += part;
      if (!streamingData) chunkRemaining -= uint32_t(part);
      if (!streamingRiff) riffRemaining -= uint32_t(part);
      if (streamingData) continue;
      if (!chunkRemaining) {
        if (waveStage == WaveStage::Format && !formatPcm(header, headerUsed)) { fail(Error::UnsupportedMedia); return false; }
        nextWaveChunk();
      }
    }
    return true;
  }
  bool decodeMp3(bool final) {
    while (mp3->buffered) {
      if (cancelled(cancellation)) { fail(Error::Cancelled); return false; }
#ifdef ARDUINO
      // ESP-IDF reports stack high-water mark in bytes (StackType_t is uint8_t).
      // Refuse to corrupt a small Arduino loop stack; do not silently disable MP3.
      const TaskHandle_t current = xTaskGetCurrentTaskHandle();
      if (decodeTask && decodeTask != current) { fail(Error::InvalidAudio); return false; }
      if (!decodeTask) {
        if (uxTaskGetStackHighWaterMark(nullptr) < 24 * 1024) { fail(Error::DecodeStackTooSmall); return false; }
        // Check once: the first successful decode intentionally lowers the
        // historical high-water mark. Rechecking it would reject frame two.
        decodeTask = current;
      }
#endif
      if (final && mp3->buffered == 128 && std::memcmp(mp3->input, "TAG", 3) == 0) { mp3->buffered = 0; break; }
      mp3dec_frame_info_t info{};
      const int frames = decodeMp3Frame(&mp3->decoder, mp3->input, int(mp3->buffered), mp3->decoded, &info);
      if (info.frame_bytes <= 0) {
        if (final) { fail(Error::InvalidAudio); return false; }
        break;
      }
      if (size_t(info.frame_bytes) > mp3->buffered || frames < 0 || frames > 1152) { fail(Error::InvalidAudio); return false; }
      if (frames) {
        if (info.layer != 3 || (info.channels != 1 && info.channels != 2) || !validRate(uint32_t(info.hz)) ||
            (result.sampleRate && result.sampleRate != uint32_t(info.hz))) { fail(Error::UnsupportedMedia); return false; }
        result.sampleRate = uint32_t(info.hz);
        for (int i = 0; i < frames; ++i) {
          int32_t value = mp3->decoded[i * info.channels];
          if (info.channels == 2) value = (value + mp3->decoded[i * 2 + 1]) / 2;
          if (!sample(static_cast<int16_t>(value))) return false;
        }
      } else if (final && info.hz == 0) { fail(Error::InvalidAudio); return false; }
      mp3->buffered -= size_t(info.frame_bytes);
      std::memmove(mp3->input, mp3->input + info.frame_bytes, mp3->buffered);
      if (!final && mp3->buffered < kMp3InputBytes / 2) break;
    }
    return true;
  }
  bool compressed(const uint8_t* bytes, size_t count) {
    size_t offset = 0;
    while (offset < count) {
      if (!mp3->probed) {
        const size_t part = std::min(count - offset, sizeof(mp3->probe) - mp3->probeBytes);
        std::memcpy(mp3->probe + mp3->probeBytes, bytes + offset, part); mp3->probeBytes += part; offset += part;
        if (mp3->probeBytes < sizeof(mp3->probe)) continue;
        mp3->probed = true;
        if (std::memcmp(mp3->probe, "ID3", 3) == 0) {
          for (unsigned i = 6; i < 10; ++i) if (mp3->probe[i] & 0x80) { fail(Error::InvalidAudio); return false; }
          if (mp3->probe[3] < 2 || mp3->probe[3] > 4) { fail(Error::UnsupportedMedia); return false; }
          mp3->skipTag = (uint32_t(mp3->probe[6]) << 21) | (uint32_t(mp3->probe[7]) << 14) |
              (uint32_t(mp3->probe[8]) << 7) | mp3->probe[9];
          if (mp3->probe[3] == 4 && (mp3->probe[5] & 0x10)) mp3->skipTag += 10;
          if (mp3->skipTag > kMaxResponseBytes) { fail(Error::ResponseTooLarge); return false; }
        } else {
          std::memcpy(mp3->input, mp3->probe, sizeof(mp3->probe)); mp3->buffered = sizeof(mp3->probe);
        }
      }
      if (mp3->skipTag) {
        const size_t skip = std::min(count - offset, size_t(mp3->skipTag)); mp3->skipTag -= uint32_t(skip); offset += skip; continue;
      }
      const size_t part = std::min(count - offset, sizeof(mp3->input) - mp3->buffered);
      std::memcpy(mp3->input + mp3->buffered, bytes + offset, part); mp3->buffered += part; offset += part;
      if (mp3->buffered == sizeof(mp3->input) && !decodeMp3(false)) return false;
      if (mp3->buffered == sizeof(mp3->input)) { fail(Error::InvalidAudio); return false; }
    }
    return true;
  }
};

AudioDecoder::AudioDecoder(PcmCallback callback, void* context, const Cancellation* cancellation) {
  state_ = new (std::nothrow) State;
  if (state_) { state_->callback = callback; state_->context = context; state_->cancellation = cancellation; }
}
AudioDecoder::~AudioDecoder() { delete state_; }
size_t AudioDecoder::workingMemoryBytes() { return sizeof(State) + sizeof(State::Mp3); }

Error AudioDecoder::begin(const char* contentType, RawPcmFormat raw) {
  if (!state_) return Error::OutOfMemory;
  State& s = *state_;
  if (cancelled(s.cancellation)) return s.fail(Error::Cancelled);
  if (s.begun || !s.callback || !contentType) return s.fail(Error::UnsupportedMedia);
  s.begun = true;
  char mime[64]{}; size_t length = 0;
  for (const char* p = contentType; *p && *p != ';' && length + 1 < sizeof(mime); ++p) {
    char c = *p; if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A')); mime[length++] = c;
  }
  while (length && mime[length - 1] == ' ') mime[--length] = '\0';
  if (!std::strcmp(mime, "audio/mpeg") || !std::strcmp(mime, "audio/mp3")) {
    s.format = State::Format::Mp3; s.mp3 = new (std::nothrow) State::Mp3;
    if (!s.mp3) return s.fail(Error::OutOfMemory);
    mp3dec_init(&s.mp3->decoder);
  } else if (!std::strcmp(mime, "audio/wav") || !std::strcmp(mime, "audio/x-wav") || !std::strcmp(mime, "audio/vnd.wave")) {
    s.format = State::Format::Wave;
  } else if (!std::strcmp(mime, "audio/x-pcm") || !std::strcmp(mime, "audio/pcm")) {
    if (!raw.sampleRate || !raw.channels) return s.fail(Error::MissingAudioMetadata);
    if (!validRate(raw.sampleRate) || (raw.channels != 1 && raw.channels != 2) || !rawParametersMatch(contentType, raw)) return s.fail(Error::UnsupportedMedia);
    // The supplied hint is explicitly s16le; never interpret audio/L16 as LE.
    s.format = State::Format::Raw; s.channels = raw.channels; s.result.sampleRate = raw.sampleRate;
  } else return s.fail(Error::UnsupportedMedia);
  return s.result.error;
}

Error AudioDecoder::write(const uint8_t* bytes, size_t count) {
  if (!state_) return Error::OutOfMemory;
  State& s = *state_;
  if (cancelled(s.cancellation)) return s.fail(Error::Cancelled);
  if (s.result.error != Error::Ok) return s.result.error;
  if (!s.begun || s.finished || (!bytes && count)) return s.fail(Error::InvalidAudio);
  if (count > kMaxResponseBytes - s.received) return s.fail(Error::ResponseTooLarge);
  s.received += count;
  if (s.format == State::Format::Mp3) s.compressed(bytes, count);
  else if (s.format == State::Format::Wave) s.wave(bytes, count);
  else if (s.format == State::Format::Raw) s.pcm(bytes, count);
  else s.fail(Error::UnsupportedMedia);
  return s.result.error;
}

Result AudioDecoder::finish() {
  Result missing; missing.error = Error::OutOfMemory;
  if (!state_) return missing;
  State& s = *state_;
  if (cancelled(s.cancellation)) s.fail(Error::Cancelled);
  if (s.finished) return s.result;
  s.finished = true;
  if (!s.begun) s.fail(Error::InvalidAudio);
  if (s.result.error == Error::Ok && s.format == State::Format::Mp3) {
    if (!s.mp3->probed || s.mp3->skipTag) s.fail(Error::InvalidAudio);
    else s.decodeMp3(true);
  }
  if (s.result.error == Error::Ok && s.format == State::Format::Wave &&
      (!s.haveFormat || !s.haveData || s.riffRemaining || s.headerUsed ||
       (s.waveStage != State::WaveStage::Chunk && !(s.streamingData && s.waveStage == State::WaveStage::Data)))) s.fail(Error::InvalidAudio);
  if (s.result.error == Error::Ok && (s.sampleUsed || !s.received || (!s.result.samples && !s.monoCount))) s.fail(Error::InvalidAudio);
  if (s.result.error == Error::Ok) s.emit(true);
  return s.result;
}

Result transcribe(Transport& transport, const Binding& binding, const Recording& recording,
                  uint64_t boundaryNonce, char* transcript, size_t transcriptCapacity, const Cancellation* cancellation) {
  Result result;
  if (transcript && transcriptCapacity) transcript[0] = '\0';
  if (cancelled(cancellation)) { result.error = Error::Cancelled; return result; }
  if (!validBinding(binding)) { result.error = Error::InvalidBinding; return result; }
  if (!recording.pcm || !recording.pcmBytes || recording.pcmBytes > kMaxRecordingBytes ||
      (recording.channels != 1 && recording.channels != 2) || !validRate(recording.sampleRate) ||
      recording.pcmBytes % (recording.channels * 2) || !boundaryNonce || !transcript || transcriptCapacity < 2) {
    result.error = Error::InvalidRecording; return result;
  }
  MultipartBody body(recording, boundaryNonce, cancellation);
  const EncodedProfile profile(binding.profile); char path[1664]{};
  // Stock authentication resolves requested Profile from query/header/body,
  // not the proxy path parameter. Both must agree to enforce Profile access.
  std::snprintf(path, sizeof(path), "/api/studio/voice/proxy/%s/v1/audio/transcriptions?profile=%s", profile.value, profile.value);
  Request request; request.binding = &binding; request.path = path; request.contentType = body.contentType(); request.contentLength = body.length();
  request.cancellation = cancellation;
  TranscriptSink response(transcript, transcriptCapacity, cancellation);
  const bool sent = transport.post(request, body, response);
  result = response.finish(sent);
  if (cancelled(cancellation)) result.error = Error::Cancelled;
  else if (body.failed()) result.error = Error::SourceReadFailed;
  else if (!sent && result.error == Error::Ok) result.error = Error::TransportFailed;
  if (!result.ok()) { transcript[0] = '\0'; result.textBytes = 0; }
  return result;
}

namespace {
class AudioResponse final : public ResponseSink {
 public:
  AudioResponse(PcmCallback callback, void* context, RawPcmFormat raw, const Cancellation* token) : decoder(callback, context, token), raw_(raw), cancellation_(token) {}
  bool begin(const ResponseHeaders& h) override {
    result.httpStatus = h.status; result.error = httpError(h.status);
    if (cancelled(cancellation_)) { result.error = Error::Cancelled; return false; }
    if (!result.ok()) return false;
    if (h.contentLength > int64_t(kMaxResponseBytes)) { result.error = Error::ResponseTooLarge; return false; }
    expected_ = h.contentLength;
    result.error = decoder.begin(h.contentType, raw_); return result.ok();
  }
  bool write(const uint8_t* bytes, size_t count) override {
    if (cancelled(cancellation_)) { result.error = Error::Cancelled; return false; }
    if (expected_ >= 0 && uint64_t(received_) + count > uint64_t(expected_)) { result.error = Error::InvalidAudio; return false; }
    received_ += count; result.error = decoder.write(bytes, count); return result.ok();
  }
  Result finish(bool sent) {
    if (cancelled(cancellation_)) result.error = Error::Cancelled;
    if (result.ok() && (!sent || (expected_ >= 0 && int64_t(received_) != expected_))) result.error = Error::TransportFailed;
    if (result.ok()) { const int status = result.httpStatus; result = decoder.finish(); result.httpStatus = status; }
    return result;
  }
 private:
  AudioDecoder decoder;
  RawPcmFormat raw_;
  const Cancellation* cancellation_;
  Result result;
  int64_t expected_ = -1;
  size_t received_ = 0;
};
}  // namespace

Result synthesize(Transport& transport, const Binding& binding, const char* text,
                  PcmCallback callback, void* context, RawPcmFormat raw, const Cancellation* cancellation) {
  Result result;
  if (cancelled(cancellation)) { result.error = Error::Cancelled; return result; }
  if (!validBinding(binding)) { result.error = Error::InvalidBinding; return result; }
  const size_t length = boundedLength(text, kMaxTextBytes);
  bool nonblank = false;
  for (size_t i = 0; i < std::min(length, kMaxTextBytes); ++i) if (static_cast<unsigned char>(text[i]) > 0x20) nonblank = true;
  if (!length || length > kMaxTextBytes || !nonblank || !callback) { result.error = Error::InvalidText; return result; }
  JsonTextBody body(text, cancellation);
  const EncodedProfile profile(binding.profile); char path[896]{};
  std::snprintf(path, sizeof(path), "/api/studio/tts/synthesize?profile=%s", profile.value);
  Request request; request.binding = &binding; request.path = path; request.contentType = "application/json"; request.contentLength = body.length();
  request.cancellation = cancellation;
  AudioResponse response(callback, context, raw, cancellation);
  return response.finish(transport.post(request, body, response));
}

const char* errorMessage(Error error) {
  switch (error) {
    case Error::Ok: return "ok";
    case Error::InvalidBinding: return "invalid Studio binding";
    case Error::InvalidRecording: return "invalid PCM recording";
    case Error::InvalidText: return "invalid speech text";
    case Error::SourceReadFailed: return "recording read failed";
    case Error::TransportFailed: return "Studio voice transport failed";
    case Error::AuthenticationRequired: return "Studio login expired";
    case Error::HttpFailed: return "Studio voice request rejected";
    case Error::ResponseTooLarge: return "Studio voice response too large";
    case Error::UnsupportedMedia: return "unsupported Studio audio format";
    case Error::InvalidAudio: return "invalid or truncated Studio audio";
    case Error::OutOfMemory: return "insufficient voice memory";
    case Error::Cancelled: return "voice operation cancelled";
    case Error::DecodeStackTooSmall: return "MP3 requires a dedicated 32 KiB voice task";
    case Error::MissingAudioMetadata: return "Studio raw PCM response has no explicit sample rate/channel contract";
  }
  return "unknown voice error";
}

#ifdef ARDUINO
namespace {
using detail::NetworkBudget;

// HTTPClient's pinned body loop does not time out while available()==0.
// Gate the actual socket as well as each upload/download callback. The owner
// task alone stops its client; no watchdog task races an SDK read or write.
// SDK DNS/connect/TLS and caller callbacks remain synchronous: deny a late
// result, without claiming to preempt a blocking SDK/caller function mid-call.
template<typename Base> class DeadlineClient final : public Base {
 public:
  using Base::connect;
  using Base::read;
  DeadlineClient(NetworkBudget& budget, const Cancellation* token) : budget_(budget), cancellation_(token) {}
  int available() override { return alive() ? std::max(0, Base::available()) : 0; }
  uint8_t connected() override { return alive() ? Base::connected() : 0; }
  int read() override { return alive() ? Base::read() : -1; }
  int read(uint8_t* bytes, size_t count) override { return alive() ? Base::read(bytes, count) : -1; }
  int peek() override { return alive() ? Base::peek() : -1; }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t count) override {
    if (!alive()) return 0;
    const size_t written = Base::write(bytes, count);
    return alive() ? written : 0;
  }
  // Keep the two-argument overloads inherited: pinned WiFiClientSecure's
  // three-argument implementation dispatches back to them. Forwarding those
  // overloads to ours would recurse. HTTPClient calls the timed overload.
  int connect(const char* host, uint16_t port, int32_t timeout) override {
    if (!alive()) return 0;
    const uint32_t left = budget_.remaining(millis());
    if (!left) { alive(); return 0; }
    const int result = Base::connect(host, port, std::min<int32_t>(timeout > 0 ? timeout : int32_t(left), int32_t(left)));
    return alive() ? result : 0;
  }
  int connect(IPAddress ip, uint16_t port, int32_t timeout) override {
    if (!alive()) return 0;
    const uint32_t left = budget_.remaining(millis());
    if (!left) { alive(); return 0; }
    const int result = Base::connect(ip, port, std::min<int32_t>(timeout > 0 ? timeout : int32_t(left), int32_t(left)));
    return alive() ? result : 0;
  }
 private:
  NetworkBudget& budget_;
  const Cancellation* cancellation_;
  bool stopped_ = false;
  bool alive() {
    if (stopped_) return false;
    if (cancelled(cancellation_) || !budget_.remaining(millis())) {
      stopped_ = true;
      Base::stop();
      // WiFiClient::setTimeout takes seconds and hides Stream::_timeout.
      // Stream::timedRead must also stop its own retry loop immediately.
      static_cast<Stream&>(*this).setTimeout(0);
      return false;
    }
    return true;
  }
};

class UploadStream final : public Stream {
 public:
  UploadStream(Reader& reader, size_t length, NetworkBudget& budget, const Cancellation* token)
      : reader_(reader), remaining_(length), budget_(budget), cancellation_(token) { setTimeout(1000); }
  int available() override { return !alive() || !remaining_ ? -1 : int(std::min(remaining_, sizeof(buffer_))); }
  int read() override {
    if (!alive()) return -1;
    if (offset_ == filled_) {
      if (!remaining_ || failed_) return -1;
      const size_t requested = std::min(remaining_, sizeof(buffer_));
      budget_.pause(millis());filled_ = reader_.read(buffer_, requested);budget_.resume(millis());offset_ = 0;
      if (!alive() || !filled_ || filled_ > requested) { failed_ = true; return -1; }
    }
    --remaining_; return buffer_[offset_++];
  }
  int peek() override { const int byte = read(); if (byte >= 0) { --offset_; ++remaining_; } return byte; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  bool complete() const { return !remaining_ && !failed_; }
 private:
  Reader& reader_;
  size_t remaining_, offset_ = 0, filled_ = 0;
  uint8_t buffer_[512]{};
  bool failed_ = false;
  NetworkBudget& budget_;
  const Cancellation* cancellation_;
  bool alive() {
    if (cancelled(cancellation_) || !budget_.remaining(millis())) { failed_ = true; setTimeout(0); }
    return !failed_;
  }
};

class DownloadStream final : public Stream {
 public:
  DownloadStream(ResponseSink& sink, NetworkBudget& budget, const Cancellation* token)
      : sink_(sink), budget_(budget), cancellation_(token) {}
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* bytes, size_t count) override {
    if (failed_ || cancelled(cancellation_) || !budget_.remaining(millis()) || count > kMaxResponseBytes - received_) { failed_ = true; return 0; }
    budget_.pause(millis());const bool accepted=sink_.write(bytes,count);budget_.resume(millis());
    if (!accepted || cancelled(cancellation_) || !budget_.remaining(millis())) { failed_ = true; return 0; }
    received_ += count; return count;
  }
  bool failed() const { return failed_; }
 private:
  ResponseSink& sink_;
  size_t received_ = 0;
  bool failed_ = false;
  NetworkBudget& budget_;
  const Cancellation* cancellation_;
};

// HTTPClient borrows this selected socket until end()/destruction. The caller
// owns it in one branch, so plain LAN requests never construct TLS state.
bool postWithClient(WiFiClient& client, const String& url, uint16_t timeoutMs,
                    const Request& request, Reader& body, ResponseSink& response, NetworkBudget& budget) {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setReuse(false); http.setConnectTimeout(std::min<uint16_t>(5000, timeoutMs)); http.setTimeout(timeoutMs);
  if (!http.begin(client, url)) return false;
  http.addHeader("Authorization", String("Bearer ") + request.binding->userJwt);
  http.addHeader("Content-Type", request.contentType);
  http.addHeader("Accept-Encoding", "identity");
  const char* headers[] = {"Content-Type"}; http.collectHeaders(headers, 1);
  UploadStream upload(body, request.contentLength, budget, request.cancellation);
  const int status = http.sendRequest("POST", &upload, request.contentLength);
  if (status <= 0 || !upload.complete() || cancelled(request.cancellation) || !budget.remaining(millis())) { http.end(); return false; }
  const String contentType = http.header("Content-Type");
  ResponseHeaders info; info.status = status; info.contentType = contentType.c_str(); info.contentLength = http.getSize();
  if (!response.begin(info)) { http.end(); return false; }
  DownloadStream download(response, budget, request.cancellation);
  // writeToStream performs HTTP transfer dechunking; getStream() alone does not.
  const int copied = http.writeToStream(&download);
  http.end(); return copied >= 0 && !download.failed() && !cancelled(request.cancellation) && budget.remaining(millis());
}
}  // namespace

bool ArduinoTransport::post(const Request& request, Reader& body, ResponseSink& response) {
  if (cancelled(request.cancellation)) return false;
  if (!timeoutMs_ || !request.binding || !validBinding(*request.binding) || !request.path || request.path[0] != '/' ||
      request.path[1] == '/' || !request.contentType || !request.contentLength || request.contentLength > INT_MAX) return false;
  for (const char* c = request.path; *c; ++c) if (static_cast<unsigned char>(*c) <= 0x20 || *c == '\\' || *c == '#') return false;
  NetworkBudget budget(millis(),timeoutMs_);
  String base(request.binding->baseUrl);
  while (base.endsWith("/")) base.remove(base.length() - 1);
  const String url = base + request.path;
  if (url.startsWith("https://")) {
    DeadlineClient<WiFiClientSecure> secure(budget, request.cancellation);
    native_tls::configureTrust(secure);
    secure.setHandshakeTimeout((std::min<uint32_t>(15000, timeoutMs_) + 999) / 1000);
    return postWithClient(secure, url, timeoutMs_, request, body, response, budget);
  }
  DeadlineClient<WiFiClient> plain(budget, request.cancellation);
  return postWithClient(plain, url, timeoutMs_, request, body, response, budget);
}
#endif

}  // namespace official_voice
