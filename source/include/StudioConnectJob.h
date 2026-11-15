#pragma once
#include "WebSocketHandshake.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#ifdef ARDUINO
#include <WiFiClient.h>
#endif

// No account/JWT/password belongs in this operation. The main owner performs
// native namespace auth only AFTER accepting this fully validated connection.
namespace studio_connect {
constexpr size_t kHostBytes=253,kPathBytes=1024;
constexpr uint32_t kPlainStackBytes=8192,kTlsStackBytes=16384;
enum class Error:uint8_t {Ok,InvalidInput,NoMemory,ConnectFailed,Deadline,WriteFailed,HandshakeFailed,Closed,Cancelled,NotSupported};
enum class StartResult:uint8_t {Started,Busy,Invalid,NoMemory,NotSupported};
enum class Poll:uint8_t {Idle,Pending,Ready};
struct Input {
  std::array<char,kHostBytes+1> host{};
  std::array<char,kPathBytes+1> path{};
  uint16_t port=0;bool tls=false;uint32_t epoch=0;
};
bool makeInput(Input&,const char* host,uint16_t port,bool tls,const char* path,uint32_t epoch);
bool validInput(const Input&);
uint32_t taskStackBytes(const Input&);
size_t requiredHeapBytes(const Input&); // Admission headroom, not a success guarantee.
struct Output {
  Error error=Error::InvalidInput;
  studio_handshake::Error handshakeError=studio_handshake::Error::None;
  uint32_t epoch=0,elapsedMs=0,workerStackFreeBytes=0,heapMinimum=0,largestBlockMinimum=0;
  uint16_t httpStatus=0;bool resourcesAvailable=false;
#ifdef ARDUINO
  std::unique_ptr<WiFiClient> client;
#endif
};
class Job {
 public:
  Job()=default;
  ~Job();
  Job(const Job&)=delete;Job& operator=(const Job&)=delete;
  StartResult start(const Input&);
  Poll poll(Output&); // Ready transfers exclusive client ownership exactly once.
  void cancel(); // Cooperative; never calls stop() on a worker-owned client.
  bool busy()const;
 private:
  struct Shared;Shared* state_=nullptr;
  static void worker(void*);
  friend size_t requiredHeapBytes(const Input&);
};
// start/poll/cancel are serial calls from the application owner. Cancellation
// remains busy until worker completion is polled. DNS/TLS are SDK synchronous;
// their late results are rejected, not claimed to have been forcibly stopped.
} // namespace studio_connect
