#pragma once

#include "OfficialStudioModels.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

// Official Studio 0.7.36 / 1242ac6, authenticated LAN APIs only:
// modules/studio/controllers/auth.ts: currentUser -> user.id (positive integer).
// modules/hermes/controllers/models.ts: getAvailable -> profiles[].groups.
// modules/coding-agents/controllers/{agents,models}.ts -> tools / agents.
// No MCU extension, provider secret, configuration file or UI global is used.
namespace official_selection {

constexpr size_t kBaseUrlBytes = 512, kJwtBytes = 4096, kProfileBytes = 224, kAgentBytes = 31;
constexpr size_t kJsonMemoryBytes = 32 * 1024;
constexpr size_t kJsonMaxDepth = 16, kJsonKeyBytes = 512;
constexpr uint32_t kTaskStackBytes = 8192;

enum class Result : uint8_t { Ok, AuthInvalid, Transient, Unavailable, Invalid, NoMemory, Cancelled };
enum class Stage : uint8_t { Input, Account, Models, Installed, NativeModels, Complete };

struct Input {
  std::array<char,kBaseUrlBytes+1> baseUrl{};
  std::array<char,kJwtBytes+1> userJwt{};
  std::array<char,kProfileBytes+1> profile{};
  std::array<char,kAgentBytes+1> agentId{};
  uint32_t epoch = 0;
  ~Input();  // Clears this owned JWT copy; never logs it.
};

// UTF-8 byte limits are explicit; no partial/truncated binding is accepted.
Result makeInput(Input& output, const char* baseUrl, const char* userJwt,
                 const char* profile, const char* canonicalAgentId, uint32_t epoch);
bool validInput(const Input& input);

struct Output {
  Result result = Result::Invalid;
  Stage stage = Stage::Input;
  int httpStatus = 0;
  uint32_t epoch = 0, retryAfterMs = 0;
  size_t jsonPeakBytes = 0;  // Includes SAX workspace + JSON allocator overhead.
  std::array<char,21> accountId{};
  std::array<char,kProfileBytes+1> profile{};
  std::array<char,kAgentBytes+1> agentId{};
  official_studio::Mode mode = official_studio::Mode::Scoped;
  official_studio_models::Selection selection;
  official_studio::Result nativeResult = official_studio::Result::NativeNotReady;

  // Rebind only after the main loop validates its current immutable context.
  // epoch must change whenever the machine/token/profile/Agent binding changes.
  // The output never owns/returns borrowed pointers into a JsonDocument.
  official_studio::NativeReadiness readiness(const official_studio::Context& context) const;
};

struct Request {
  const Input* input = nullptr;  // Immutable for this synchronous get().
  const char* path = nullptr;   // Module-generated same-origin path, URL encoded.
  Stage stage = Stage::Input;
  uint16_t timeoutMs = 0;
};
struct Headers {
  int status = 0;
  const char* contentType = nullptr;  // Borrowed during begin() only.
  int64_t contentLength = -1;         // Decoded body length, or unknown.
  uint32_t retryAfterMs = 0;
};
class ResponseSink {
 public:
  virtual ~ResponseSink() = default;
  virtual bool begin(const Headers&) = 0;
  virtual bool write(const uint8_t* bytes, size_t count) = 0;
};
class Transport {
 public:
  virtual ~Transport() = default;
  // begin exactly once, then deliver DECHUNKED bytes. Return true only at a
  // complete HTTP body/clean EOF; false on timeout, cancellation or short body.
  // Never follow redirects or log JWT/response bodies. Honor sink cancellation.
  virtual bool get(const Request&, ResponseSink&) = 0;
};
class Cancellation {
 public:
  virtual ~Cancellation() = default;
  virtual bool cancelled() const = 0;
};

// Pure synchronous pipeline for a dedicated worker or host fixture transport.
// Profile scoped defaults take priority. Normal absence of a scoped model may
// use a supported native/global mode; malformed/OOM/permission responses never
// silently switch mode. No retry occurs here; retryAfterMs is a bounded hint.
Output resolve(const Input&, Transport&, const Cancellation* = nullptr);

enum class StartResult : uint8_t { Started, Busy, Invalid, NoMemory, NotSupported };
enum class Poll : uint8_t { Idle, Pending, Ready };

// One outstanding job per instance. start/poll/cancel are main-loop operations
// and must not be called concurrently with each other. The caller coordinates
// its one instance with voice/network tasks; this module does not touch a UI,
// Wi-Fi settings, global Strings or a caller-owned Transport.
class Job {
 public:
  Job() = default;
  ~Job();
  Job(const Job&) = delete;
  Job& operator=(const Job&) = delete;
  // Optional transport is transferred to the worker, never borrowed. Host
  // tests may inject one; the default host transport is NotSupported.
  StartResult start(const Input&, std::unique_ptr<Transport> = {});
  Poll poll(Output&);
  void cancel();
  bool busy() const;
 private:
  struct State;
  State* state_ = nullptr;
  static void worker(void*);
};

} // namespace official_selection
