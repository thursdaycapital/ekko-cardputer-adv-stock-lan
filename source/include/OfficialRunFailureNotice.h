#pragma once
#include <cstddef>
#include <cstdint>

// Display hints for untrusted run.failed text, not auth/retry policy or an
// assertion of the provider's exact cause. No raw text is returned or logged.
namespace official_run_failure {
constexpr size_t kMaxErrorBytes = 512;
enum class Reason : uint8_t { Unknown, RuntimeOrFileMissing, Authorization, NetworkOrTimeout, QuotaOrRateLimit };
struct Notice { Reason reason; const char* code; const char* text; };

namespace detail {
struct View { const char* data; size_t bytes; };
inline View bounded(const char* raw) {
  if(!raw)return {"",0};
  size_t bytes=0;while(bytes<kMaxErrorBytes && raw[bytes])++bytes;
  return {raw,bytes};
}
inline unsigned char lower(unsigned char c) {return c>='A'&&c<='Z'?c+('a'-'A'):c;}
inline bool space(unsigned char c) {return c==' '||c=='\t'||c=='\n'||c=='\r';}
inline bool delimiter(unsigned char c) {
  return space(c)||c>=0x80||c==':'||c=='='||c==','||c==';'||c=='('||c==')'||
      c=='['||c==']'||c=='{'||c=='}'||c=='\''||c=='"'||c=='!';
}
inline bool left(View input,size_t at) {return !at||delimiter(static_cast<unsigned char>(input.data[at-1]));}
inline bool right(View input,size_t at) {
  // At the byte cap the next byte is unknown, not an established token end.
  // Do not turn a clipped ENOENTsuffix / 42901 into an exact error marker.
  if(at==input.bytes)return input.bytes<kMaxErrorBytes;
  const auto c=static_cast<unsigned char>(input.data[at]);
  // A sentence-ending period is allowed, while timeout.json / ENOENT.log and
  // slash-delimited paths/URLs must not manufacture standalone error tokens.
  return delimiter(c)||(c=='.'&&((at+1==input.bytes&&input.bytes<kMaxErrorBytes)||
      (at+1<input.bytes&&delimiter(static_cast<unsigned char>(input.data[at+1])))));
}
inline bool match(View input,size_t at,const char* needle,size_t& end) {
  end=at;
  for(size_t n=0;needle[n];++n,++end)
    if(end==input.bytes||lower(static_cast<unsigned char>(input.data[end]))!=lower(static_cast<unsigned char>(needle[n])))return false;
  return true;
}
inline bool has(View input,const char* needle) {
  for(size_t at=0;at<input.bytes;++at) {
    size_t end=0;
    if(left(input,at)&&match(input,at,needle,end)&&right(input,end))return true;
  }
  return false;
}
template<size_t N> inline bool any(View input,const char* const (&needles)[N]) {
  for(const char* needle:needles)if(has(input,needle))return true;
  return false;
}
inline bool statusField(View input,const char* field,const char* code) {
  for(size_t at=0;at<input.bytes;++at) {
    size_t end=0;
    if(!left(input,at)||!match(input,at,field,end)||!right(input,end))continue;
    size_t cursor=end;
    if(cursor<input.bytes&&(input.data[cursor]=='\''||input.data[cursor]=='"'))++cursor;
    const size_t beforeSpace=cursor;
    while(cursor<input.bytes&&space(static_cast<unsigned char>(input.data[cursor])))++cursor;
    const bool hadSpace=cursor>beforeSpace;
    if(cursor<input.bytes&&(input.data[cursor]==':'||input.data[cursor]=='=')) {
      ++cursor;
      while(cursor<input.bytes&&space(static_cast<unsigned char>(input.data[cursor])))++cursor;
      if(cursor<input.bytes&&(input.data[cursor]=='\''||input.data[cursor]=='"'))++cursor;
    } else if(!hadSpace)continue;
    if(match(input,cursor,code,end)&&right(input,end))return true;
  }
  return false;
}
inline bool httpStatus(View input,const char* code) {
  static constexpr const char* fields[]={"http","http/1.0","http/1.1","http/2",
      "http status","http status code","status","status code","status_code","statuscode"};
  for(const char* field:fields)if(statusField(input,field,code))return true;
  return false;
}
} // namespace detail

inline Reason classify(const char* raw) {
  const auto input=detail::bounded(raw);
  static constexpr const char* runtime[]={"enoent","no such file or directory","file not found",
      "command not found","executable not found","cannot find module","module not found",
      "runtime not found","runtime missing","runtime is missing","runtime not installed",
      "cli not installed","agent not installed","找不到可执行文件","运行环境缺失","运行环境不存在","运行时不存在","运行时未安装"};
  static constexpr const char* authorization[]={"unauthorized","forbidden","invalid_api_key",
      "incorrect_api_key","authentication_error","invalid_token","token_expired",
      "authentication failed","authentication required","authorization failed",
      "authorization required","invalid api key","incorrect api key","missing api key",
      "api key is required","api key is invalid","invalid token","token has expired",
      "login required","not logged in","please log in","认证失败","授权失败","未授权",
      "未登录","登录已过期","密钥无效","密钥错误"};
  static constexpr const char* network[]={"etimedout","econnreset","econnrefused","ehostunreach",
      "enetunreach","enotfound","eai_again","timeout","deadline_exceeded","timed out",
      "network error","network unreachable","network is unreachable","fetch failed",
      "failed to fetch","socket hang up","connection refused","connection reset",
      "could not resolve host","dns lookup failed","tls handshake failed",
      "请求超时","连接超时","执行超时","网络不可达","网络连接失败","连接被拒绝","连接被重置"};
  static constexpr const char* quota[]={"insufficient_quota","quota_exceeded","rate_limit_exceeded",
      "rate_limit_error","resource_exhausted","resource-exhausted","too many requests",
      "rate limit","rate limited","quota exceeded","quota exhausted","insufficient quota",
      "insufficient credits","insufficient balance","out of credits","额度不足","余额不足",
      "配额不足","触发限流","请求过于频繁"};
  const uint8_t hints=(detail::any(input,runtime)?1:0)|
      ((detail::any(input,authorization)||detail::httpStatus(input,"401")||detail::httpStatus(input,"403"))?2:0)|
      (detail::any(input,network)?4:0)|
      ((detail::any(input,quota)||detail::httpStatus(input,"429"))?8:0);
  // Multiple mutually different categories cannot establish a precise cause.
  // An API-key mention alone isn't an auth error, so normal quota diagnostics
  // which merely mention a key still classify as quota.
  if(!hints||(hints&(hints-1)))return Reason::Unknown;
  if(hints==1)return Reason::RuntimeOrFileMissing;
  if(hints==2)return Reason::Authorization;
  if(hints==4)return Reason::NetworkOrTimeout;
  return Reason::QuotaOrRateLimit;
}
inline Notice describe(Reason reason) {
  switch(reason) {
    case Reason::RuntimeOrFileMissing:return {reason,"runtime_or_file_missing","运行环境或文件缺失，请检查电脑"};
    case Reason::Authorization:return {reason,"authorization","模型或 Agent 授权失败，请检查电脑"};
    case Reason::NetworkOrTimeout:return {reason,"network_or_timeout","请求超时或网络异常，请稍后重试"};
    case Reason::QuotaOrRateLimit:return {reason,"quota_or_rate_limit","模型额度或限流，请查看电脑端"};
    case Reason::Unknown:break;
  }
  return {Reason::Unknown,"unknown","请求失败，请在电脑端查看详情"};
}
inline Notice notice(const char* raw) {return describe(classify(raw));}
} // namespace official_run_failure
