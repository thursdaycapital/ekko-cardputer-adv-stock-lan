#pragma once

#include <stddef.h>
#include <stdint.h>

// Official Studio 0.7.36 authenticated HTTP voice APIs. This module never
// requests provider settings/secrets, performs login, follows redirects, or
// caches the borrowed JWT, transcript or synthesis text.
namespace official_voice {

enum class Error : uint8_t {
  Ok, InvalidBinding, InvalidRecording, InvalidText, SourceReadFailed,
  TransportFailed, AuthenticationRequired, HttpFailed, ResponseTooLarge,
  UnsupportedMedia, InvalidAudio, OutOfMemory, Cancelled, DecodeStackTooSmall,
  MissingAudioMetadata
};

struct Result {
  Error error = Error::Ok;
  int httpStatus = 0;
  size_t textBytes = 0;
  size_t samples = 0;  // Mono output samples, including at most one alignment zero.
  uint32_t sampleRate = 0;
  bool ok() const { return error == Error::Ok; }
};

// The caller supplies its CURRENT authenticated Studio binding, not a URL
// supplied by a chat event. Storage must remain immutable until post() returns.
// baseUrl is an http(s) origin, optionally with a deployment base path.
struct Binding {
  const char* baseUrl = nullptr;
  const char* userJwt = nullptr;
  const char* profile = nullptr;
};

class Reader {
 public:
  virtual ~Reader() = default;
  // Blocking stream/file read. 0 means EOF/error; never return more than capacity.
  virtual size_t read(uint8_t* output, size_t capacity) = 0;
};

struct Recording {
  Reader* pcm = nullptr;
  size_t pcmBytes = 0;
  uint32_t sampleRate = 16000;
  uint16_t channels = 1;
};

class Cancellation {
 public:
  virtual ~Cancellation() = default;
  virtual bool cancelled() const = 0;
};

namespace detail {
// Network time excludes synchronous source/consumer callbacks (notably PCM
// backpressure). The caller supplies a wrapping millisecond clock for testing.
class NetworkBudget {
 public:
  NetworkBudget(uint32_t started,uint32_t timeout):started_(started),timeout_(timeout){}
  uint32_t remaining(uint32_t now)const {
    const uint32_t elapsed=uint32_t((paused_?pausedAt_:now)-started_)-excluded_;
    return elapsed<timeout_?timeout_-elapsed:0;
  }
  void pause(uint32_t now){if(!paused_){paused_=true;pausedAt_=now;}}
  void resume(uint32_t now){if(paused_){excluded_+=uint32_t(now-pausedAt_);paused_=false;}}
 private:
  uint32_t started_,timeout_,excluded_=0,pausedAt_=0;bool paused_=false;
};
} // namespace detail

struct ResponseHeaders {
  int status = 0;
  const char* contentType = nullptr;  // Borrowed only during begin().
  int64_t contentLength = -1;
};

class ResponseSink {
 public:
  virtual ~ResponseSink() = default;
  virtual bool begin(const ResponseHeaders& headers) = 0;
  virtual bool write(const uint8_t* bytes, size_t count) = 0;
};

struct Request {
  const Binding* binding = nullptr;
  const char* path = nullptr;  // Module-generated same-origin path only.
  const char* contentType = nullptr;
  size_t contentLength = 0;
  const Cancellation* cancellation = nullptr; // Immutable lifetime through post().
};

class Transport {
 public:
  virtual ~Transport() = default;
  // Must post to binding->baseUrl + path only, send Authorization: Bearer
  // userJwt, disable redirects, and deliver a dechunked response. A false sink
  // return aborts the request. Honor cancellation even during silent HTTP waits.
  // Must not log bodies, JWTs or URLs with credentials.
  virtual bool post(const Request& request, Reader& body, ResponseSink& response) = 0;
};

// Called synchronously with MONO signed 16-bit PCM, at most 1024 samples per
// call, always an even count. This is MONO, not the hardware's stereo buffer;
// the main loop must apply its output shaping and channel conversion. The
// receiver must copy or finish consuming before returning. Returning false
// cancels immediately. The caller owns speaker start/drain/join.
using PcmCallback = bool (*)(void*, const int16_t*, size_t, uint32_t sampleRate);

struct RawPcmFormat {
  uint32_t sampleRate = 0;  // Required for unframed PCM; no rate is guessed.
  uint16_t channels = 0;
};

// Independent streaming decoder, useful for playback of an already-authorized
// audio response. MP3, WAV PCM16 (incl. extensible PCM), or explicitly described
// raw s16le. Unknown/compressed MIME types are NEVER played as raw PCM.
// One decoder belongs to one task; it is neither reentrant nor thread-safe.
class AudioDecoder {
 public:
  AudioDecoder(PcmCallback callback, void* context, const Cancellation* = nullptr);
  ~AudioDecoder();
  AudioDecoder(const AudioDecoder&) = delete;
  AudioDecoder& operator=(const AudioDecoder&) = delete;
  Error begin(const char* contentType, RawPcmFormat raw = {});
  Error write(const uint8_t* bytes, size_t count);
  Result finish();
  static size_t workingMemoryBytes();
  // minimp3 has a ~16 KiB local scratch object. ESP32 callers must use a
  // dedicated voice task with >=32 KiB stack, NOT the 12 KiB Arduino loop.
  static constexpr size_t recommendedTaskStackBytes() { return 32 * 1024; }
 private:
  struct State;
  State* state_ = nullptr;
};

// A fresh unpredictable nonce (e.g. two esp_random() calls) makes each boundary
// independent of recording bytes. Upload is a WAV header + caller PCM stream;
// no duration-sized WAV allocation is made. Transcript stays in caller storage.
Result transcribe(Transport& transport, const Binding& binding,
                  const Recording& recording, uint64_t boundaryNonce,
                  char* transcript, size_t transcriptCapacity,
                  const Cancellation* = nullptr);

// Uses Studio's selected TTS provider and stored voice/model/speed/secrets,
// overriding only this request's audio container to WAV. The stock route does
// not describe raw PCM's sample rate/channels in response headers. Providers
// such as Edge may return MP3 instead; both are self-describing and supported.
// raw is an explicit out-of-band PCM contract, never inferred from a generic
// Content-Type or the requested WAV format. Missing raw metadata is rejected.
Result synthesize(Transport& transport, const Binding& binding, const char* text,
                  PcmCallback callback, void* context, RawPcmFormat raw = {},
                  const Cancellation* = nullptr);

const char* errorMessage(Error error);  // Fixed diagnostics, no server body/text.

#ifdef ARDUINO
class ArduinoTransport final : public Transport {
 public:
  // One network-time budget covers silent/partial headers and bodies; zero
  // refuses a request. Source/PCM callback time is excluded so playback
  // backpressure cannot masquerade as a network timeout. Those callbacks and
  // SDK DNS/connect/TLS remain synchronous; cancellation is cooperative.
  explicit ArduinoTransport(uint16_t timeoutMs = 30000) : timeoutMs_(timeoutMs) {}
  bool post(const Request&, Reader&, ResponseSink&) override;
 private:
  uint16_t timeoutMs_;
};
#endif

}  // namespace official_voice
