#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace studio_auth {
constexpr size_t kBaseUrlBytes=512,kMachineKeyBytes=384,kProfileBytes=224,kInstanceBytes=96;
constexpr size_t kAccountBytes=224,kPasswordBytes=512,kJwtBytes=4096,kResponseBytes=8192;
constexpr uint32_t kIdentityDeadlineMs=8000,kDeadlineMs=16000,kPlainStackBytes=16384,kTlsStackBytes=32768;
enum class Error:uint8_t {Ok,InvalidInput,Transient,Rejected,BindingChanged,InvalidResponse,ResponseTooLarge,NoMemory,Cancelled,NotSupported};
enum class Stage:uint8_t {Input,Identity,Login,Complete};
struct Input {
  std::array<char,kBaseUrlBytes+1> baseUrl{};
  std::array<char,kMachineKeyBytes+1> machineKey{};
  std::array<char,kProfileBytes+1> profile{};
  std::array<char,kInstanceBytes+1> instance{};
  std::array<char,kAccountBytes+1> account{};
  std::array<char,kPasswordBytes+1> password{};
  uint32_t epoch=0;
  ~Input();
};
bool makeInput(Input&,const char* baseUrl,const char* machineKey,const char* profile,
               const char* instance,const char* account,const char* password,uint32_t epoch);
bool validInput(const Input&);
uint32_t taskStackBytes(const Input&);
size_t requiredHeapBytes(const Input&);
struct Output {
  Output()=default;Output(const Output&)=delete;Output& operator=(const Output&)=delete;
  Output(Output&&)noexcept;Output& operator=(Output&&)noexcept;
  Error error=Error::InvalidInput;Stage stage=Stage::Input;
  uint32_t epoch=0,elapsedMs=0,workerStackFreeBytes=0;int httpStatus=0;
  std::array<char,kJwtBytes+1> jwt{};
  ~Output();
  void clearSecret();
};
class Cancellation {
 public:virtual ~Cancellation()=default;virtual bool cancelled()const=0;
};
enum class Method:uint8_t {Get,Post};
struct Request {
  Request()=default;
  Request(Method methodValue,const char* originValue,const char* pathValue,const char* bodyValue,
          size_t bodySize,const char* instanceValue,uint32_t timeoutValue)
    :method(methodValue),baseUrl(originValue),path(pathValue),body(bodyValue),bodyBytes(bodySize),
     deviceInstance(instanceValue),timeoutMs(timeoutValue){}
  Method method=Method::Get;
  const char* baseUrl=nullptr;const char* path=nullptr;
  const char* body=nullptr;size_t bodyBytes=0;
  const char* deviceInstance=nullptr; // POST only; no account headers or JWT.
  uint32_t timeoutMs=0;
};
struct Headers {
  Headers()=default;
  Headers(int code,int64_t length,const char* type,const char* encoding,const char* transfer)
    :status(code),contentLength(length),contentType(type),contentEncoding(encoding),transferEncoding(transfer){}
  int status=0;int64_t contentLength=-1;
  const char* contentType=nullptr;const char* contentEncoding=nullptr;const char* transferEncoding=nullptr;
};
class ResponseSink {
 public:virtual ~ResponseSink()=default;
  virtual bool begin(const Headers&)=0;
  virtual bool write(const uint8_t*,size_t)=0;
};
class Transport {
 public:virtual ~Transport()=default;
  // No redirect, no cookie/JWT/Authorization header, no response/request log.
  // begin once then EXACT Content-Length bytes; unknown/chunked/encoded
  // responses are rejected. Honor cancellation and the absolute deadline.
  virtual bool exchange(const Request&,ResponseSink&,const Cancellation*)=0;
};
// The same production pipeline is exercised by owned host fixture transports.
// It validates a fresh same-origin identity BEFORE making any credential POST.
Output resolve(const Input&,Transport&,const Cancellation* =nullptr);
enum class StartResult:uint8_t {Started,Busy,Invalid,NoMemory,NotSupported};
enum class Poll:uint8_t {Idle,Pending,Ready};
class Job {
 public:
  Job()=default;~Job();Job(const Job&)=delete;Job& operator=(const Job&)=delete;
  StartResult start(const Input&,std::unique_ptr<Transport> ={});
  Poll poll(Output&);void cancel();bool busy()const;
 private:
  struct Shared;Shared* state_=nullptr;static void worker(void*);
  friend size_t requiredHeapBytes(const Input&);
};
// Only the caller may commit a successful JWT after comparing the full bound
// context and epoch again. Cancellation never commits or modifies NVS/UI.
} // namespace studio_auth
