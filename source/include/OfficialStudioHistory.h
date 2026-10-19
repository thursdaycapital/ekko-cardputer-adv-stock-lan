#pragma once

#include "OfficialStudioIncoming.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

// Stock Studio 0.7.36 / 1242ac6 authenticated conversation pagination.
// Uses the bound user's JWT, never a provider key or an MCU extension.
namespace official_history {

constexpr size_t kBaseUrlBytes=512, kJwtBytes=4096, kProfileBytes=224, kSessionBytes=224;
constexpr size_t kPageSize=12;
constexpr uint16_t kTimeoutMs=8000;
constexpr uint32_t kTaskStackBytes=8192;
using Snapshot=official_incoming::Snapshot<kPageSize>;

enum class Result:uint8_t { Ok, Empty, AuthInvalid, Transient, Invalid, NoMemory, Cancelled };
struct Input {
  std::array<char,kBaseUrlBytes+1> baseUrl{};
  std::array<char,kJwtBytes+1> userJwt{};
  std::array<char,kProfileBytes+1> profile{};
  std::array<char,kSessionBytes+1> session{};
  uint32_t epoch=0, offset=0, byteOffset=0;
  ~Input(); // Wipes this owned JWT copy; no logging.
};
Result makeInput(Input&,const char* baseUrl,const char* userJwt,const char* profile,
                 const char* session,uint32_t epoch,uint32_t offset=0,uint32_t byteOffset=0);
bool validInput(const Input&);

struct Output {
  Result result=Result::Invalid;
  int httpCode=0;
  uint32_t epoch=0, offset=0, byteOffset=0;
  std::array<char,kProfileBytes+1> profile{};
  std::array<char,kSessionBytes+1> session{};
  // Present ONLY after a complete, validated HTTP/JSON response. Empty is a
  // literal HTTP 404; only the caller decides whether a new session permits it.
  std::unique_ptr<Snapshot> snapshot;
};

struct Request {
  const Input* input=nullptr; // Immutable and alive through get().
  const char* path=nullptr;  // Generated same-origin, URL-encoded path/query.
  uint16_t timeoutMs=kTimeoutMs;
};
struct Headers {
  int status=0;
  const char* contentType=nullptr; // Borrowed only through begin().
  const char* contentEncoding=nullptr;
  int64_t contentLength=-1; // Dechunked bytes, or -1 if unknown.
};
class ResponseSink {
 public:
  virtual ~ResponseSink()=default;
  virtual bool begin(const Headers&)=0;
  virtual bool write(const uint8_t*,size_t)=0;
};
class Transport {
 public:
  virtual ~Transport()=default;
  // Calls begin exactly once, supplies DECHUNKED bytes, and returns true only
  // after a complete body/clean EOF. No redirect, JWT/body log, or UI access.
  virtual bool get(const Request&,ResponseSink&)=0;
};
class Cancellation {
 public:
  virtual ~Cancellation()=default;
  virtual bool cancelled()const=0;
};

// Synchronous parsing pipeline for a worker/host fixture, not the Arduino loop.
// Snapshot and SAX workspace are bounded heap allocations, independent of the
// total message length. UTF-8 windows can be reloaded using byteOffset.
Output resolve(const Input&,Transport&,const Cancellation* =nullptr);

enum class StartResult:uint8_t { Started, Busy, Invalid, NoMemory, NotSupported };
enum class Poll:uint8_t { Idle, Pending, Ready };
class Job {
 public:
  Job()=default;
  ~Job();
  Job(const Job&)=delete;
  Job& operator=(const Job&)=delete;
  // Main-loop-only operations. Optional transport ownership transfers to the
  // worker. Host tests inject one; the host default is NotSupported.
  StartResult start(const Input&,std::unique_ptr<Transport> ={});
  Poll poll(Output&);
  void cancel();
  bool busy()const;
 private:
  struct State;
  State* state_=nullptr;
  static void worker(void*);
};

// Cancellation is cooperative. Body/Stream waits obey an absolute deadline;
// synchronous SDK DNS/connect/TLS may outlive it before their late result is
// rejected. Destruction detaches safely; it never kills a live network task.
// Caller compares epoch and complete bound context,
// and coordinates this job with selection/voice/network jobs before committing.
} // namespace official_history
