#include "OfficialRunFailureNotice.h"
#include "ChatUtf8.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using official_run_failure::Reason;
static void expect(const char* raw,Reason expected) {
  const auto actual=official_run_failure::notice(raw);
  const auto fixed=official_run_failure::describe(expected);
  assert(actual.reason==expected && !std::strcmp(actual.code,fixed.code) && !std::strcmp(actual.text,fixed.text));
  assert(!std::strstr(actual.text,"PRIVATE")&&!std::strstr(actual.code,"PRIVATE"));
  assert(!std::strstr(actual.text,"sk-")&&!std::strstr(actual.text,"/Users/")&&!std::strstr(actual.text,"eyJ"));
  assert(native_chat::validUtf8(actual.text,std::strlen(actual.text)));
  unsigned width=0;
  for(size_t at=0;actual.text[at];) {
    const auto lead=static_cast<uint8_t>(actual.text[at]);
    width+=lead<128?6:12;at+=native_chat::codepointBytes(lead);
  }
  assert(width<=219); // Existing single chat-notice line; no layout change.
}
int main() {
  struct Case {const char* raw;Reason reason;};
  const Case cases[]{
    {nullptr,Reason::Unknown},{"",Reason::Unknown},
    {"spawn /Users/PRIVATE/runtime ENOENT",Reason::RuntimeOrFileMissing},
    {"ENOENT: open /Users/PRIVATE/429/quota.json",Reason::RuntimeOrFileMissing},
    {"No such file or directory: /Users/PRIVATE/runtime",Reason::RuntimeOrFileMissing},
    {"Executable not found.",Reason::RuntimeOrFileMissing},
    {"Cannot find module 'PRIVATE_MODULE'",Reason::RuntimeOrFileMissing},
    {"运行环境缺失：/Users/PRIVATE/runtime",Reason::RuntimeOrFileMissing},
    {"HTTP 401: invalid API key sk-PRIVATE",Reason::Authorization},
    {"{\"statusCode\":\"401\"}",Reason::Authorization},
    {"status_code=403",Reason::Authorization},
    {"Authentication failed: PRIVATE_TOKEN",Reason::Authorization},
    {"invalid_api_key: sk-PRIVATE",Reason::Authorization},
    {"模型授权失败：PRIVATE_CREDENTIAL",Reason::Authorization},
    {"ETIMEDOUT connecting to PRIVATE_HOST",Reason::NetworkOrTimeout},
    {"ENOTFOUND PRIVATE_HOST",Reason::NetworkOrTimeout},
    {"econnreset: PRIVATE_HOST",Reason::NetworkOrTimeout},
    {"request timed out",Reason::NetworkOrTimeout},
    {"DNS lookup failed",Reason::NetworkOrTimeout},
    {"请求超时：PRIVATE_HOST",Reason::NetworkOrTimeout},
    {"HTTP/1.1 429 Too Many Requests",Reason::QuotaOrRateLimit},
    {"HTTP 429.",Reason::QuotaOrRateLimit},
    {"{\"status\": 429}",Reason::QuotaOrRateLimit},
    {"resource-exhausted",Reason::QuotaOrRateLimit},
    {"insufficient_quota: API key sk-PRIVATE",Reason::QuotaOrRateLimit},
    {"Too many requests for API key sk-PRIVATE",Reason::QuotaOrRateLimit},
    {"模型余额不足：PRIVATE_CREDENTIAL",Reason::QuotaOrRateLimit},
    // A filename, path, key mention or a bare number is not failure evidence.
    {"model not found",Reason::Unknown},
    {"session not found",Reason::Unknown},
    {"/Users/PRIVATE/429/quota.json",Reason::Unknown},
    {"/Users/PRIVATE/ENOENT/forbidden.json",Reason::Unknown},
    {"/Users/PRIVATE/授权失败/运行环境缺失.json",Reason::Unknown},
    {"https://PRIVATE.invalid/?status=401&file=quota.json",Reason::Unknown},
    {"/tmp/status:401",Reason::Unknown},
    {"timeout.json not found",Reason::Unknown},
    {"debug_file_429",Reason::Unknown},
    {"HTTP 42901",Reason::Unknown},
    {"HTTP 429/file",Reason::Unknown},
    {"provider quota=100",Reason::Unknown},
    {"API key configured; unrelated failure",Reason::Unknown},
    {"HTTP 200 https://PRIVATE.invalid/",Reason::Unknown},
    {"empty provider response",Reason::Unknown},
    {"未知中文错误，路径 /Users/PRIVATE，密钥 sk-PRIVATE，令牌 eyJPRIVATE",Reason::Unknown},
    {"future_provider_error sk-PRIVATE eyJPRIVATE",Reason::Unknown},
    // Contradictory explicit hints do not establish one exact cause.
    {"ENOENT and HTTP 401 Unauthorized",Reason::Unknown},
    {"HTTP 429 invalid_api_key",Reason::Unknown},
    {"network timeout or ENOENT",Reason::Unknown},
  };
  for(const auto& item:cases)expect(item.raw,item.reason);
  std::string exact(505,'x');exact+=" ENOENT";
  assert(exact.size()==official_run_failure::kMaxErrorBytes);
  expect(exact.c_str(),Reason::Unknown); // Cap is not proof that this token ends.
  exact.back()=' ';
  expect(exact.c_str(),Reason::Unknown); // A clipped marker is not ENOENT.
  exact=std::string(504,'x')+" ENOENT ";
  expect(exact.c_str(),Reason::RuntimeOrFileMissing); // Delimiter is inside cap.
  std::string clipped(507,'x');clipped+=" ENOENT";
  expect(clipped.c_str(),Reason::Unknown);
  clipped=std::string(504,'x')+" ENOENT.log";
  expect(clipped.c_str(),Reason::Unknown); // The cap hides the file extension.
  std::string beyond(1024*1024,'x');beyond+=" HTTP 429 sk-PRIVATE";
  expect(beyond.c_str(),Reason::Unknown);
  // The bounded view must not read byte 513 looking for NUL.
  std::array<char,official_run_failure::kMaxErrorBytes> nonNul;nonNul.fill('x');
  expect(nonNul.data(),Reason::Unknown);
  std::memcpy(nonNul.data()+nonNul.size()-7," ENOENT",7);
  expect(nonNul.data(),Reason::Unknown);
  std::memcpy(nonNul.data()+nonNul.size()-8," ENOENT ",8);
  expect(nonNul.data(),Reason::RuntimeOrFileMissing);
  std::puts("Run failure notices: bounded 512 bytes, exact hints/conflict fallback, auth/runtime/network/quota, paths/numbers and private-text non-disclosure passed");
}
