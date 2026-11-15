#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Non-consuming preflight for the pinned Arduino WebServer. The socket owner
// supplies an MSG_PEEK prefix, not a stream read. A 4096-byte TOTAL cap fits the
// pinned SDK's 5760-byte TCP receive window; waiting for an 8KiB request without
// consuming it would deadlock behind TCP window backpressure.
namespace management_request {
constexpr size_t kHeaderBytes=2048,kRequestBytes=4096,kLineBytes=1024;
constexpr uint32_t kDeadlineMs=750;
enum class Result:uint8_t {Waiting,Ready,Rejected};
enum class Error:uint8_t {None,Deadline,HeaderTooLarge,LineTooLong,InvalidLine,InvalidRequest,
  InvalidHeader,DuplicateLength,InvalidLength,UnsupportedFraming,UnsupportedBody,
  RequestTooLarge,ExtraBytes,SnapshotChanged};
class Gate {
 public:
  Result inspect(const uint8_t* prefix,size_t count,size_t available,uint32_t elapsedMs) {
    if(result_!=Result::Waiting)return result_;
    if(elapsedMs>=kDeadlineMs)return reject(Error::Deadline);
    if((!prefix&&count)||count>kHeaderBytes||count>available||count<examined_)
      return reject(Error::SnapshotChanged);
    if(available>kRequestBytes)return reject(Error::RequestTooLarge);
    while(!headersDone_&&examined_<count) {
      const uint8_t byte=prefix[examined_++];
      if(carriage_) {
        carriage_=false;
        if(byte!='\n')return reject(Error::InvalidLine);
        line_[used_]=0;
        if(!finishLine())return result_;
        // Headers can contain cookies. Retain only parsed numeric framing,
        // never the previous line while waiting for an incomplete form body.
        line_.fill(0);used_=0;continue;
      }
      if(byte=='\r'){carriage_=true;continue;}
      if(!byte||byte=='\n'||byte==0x7f||(byte<0x20&&byte!='\t')||byte>0x7e)
        return reject(Error::InvalidLine);
      if(used_==kLineBytes)return reject(Error::LineTooLong);
      line_[used_++]=char(byte);
    }
    if(!headersDone_) {
      if(examined_==kHeaderBytes)return reject(Error::HeaderTooLarge);
      return result_;
    }
    const size_t total=headerBytes_+bodyBytes_;
    if(total>kRequestBytes)return reject(Error::RequestTooLarge);
    // The pinned plain-body reader reads all currently available bytes instead
    // of clipping at Content-Length. Do not pass a pipeline/trailing payload.
    if(available>total)return reject(Error::ExtraBytes);
    if(available==total)result_=Result::Ready;
    return result_;
  }
  Result result()const{return result_;}
  Error error()const{return error_;}
  size_t headerBytes()const{return headerBytes_;}
  size_t bodyBytes()const{return bodyBytes_;}
  size_t examinedBytes()const{return examined_;}
 private:
  Result result_=Result::Waiting;Error error_=Error::None;
  std::array<char,kLineBytes+1> line_{};
  size_t used_=0,examined_=0,headerBytes_=0,bodyBytes_=0;
  bool carriage_=false,requestLine_=false,headersDone_=false,lengthSeen_=false,bodyMethod_=false;
  Result reject(Error error){error_=error;result_=Result::Rejected;return result_;}
  bool fail(Error error){reject(error);return false;}
  static char lower(char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):c;}
  static bool equal(const char* text,size_t bytes,const char* expected) {
    if(std::strlen(expected)!=bytes)return false;
    for(size_t n=0;n<bytes;++n)if(lower(text[n])!=expected[n])return false;
    return true;
  }
  static bool tokenChar(char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
      (c&&std::strchr("!#$%&'*+-.^_`|~",c)!=nullptr);
  }
  bool finishLine() {
    if(!requestLine_) {
      requestLine_=true;const char* first=std::strchr(line_.data(),' ');
      if(!first||first==line_.data())return fail(Error::InvalidRequest);
      const char* target=first+1;const char* second=std::strchr(target,' ');
      if(!second||target==second||*target!='/')return fail(Error::InvalidRequest);
      if(std::strcmp(second+1,"HTTP/1.1")&&std::strcmp(second+1,"HTTP/1.0"))return fail(Error::InvalidRequest);
      for(const char* p=target;p<second;++p)if(uint8_t(*p)<=0x20||*p=='#')return fail(Error::InvalidRequest);
      const size_t methodBytes=size_t(first-line_.data());
      auto method=[&](const char* expected){return std::strlen(expected)==methodBytes&&!std::memcmp(line_.data(),expected,methodBytes);};
      bodyMethod_=method("POST")||method("PUT")||method("PATCH")||method("DELETE");
      if(!bodyMethod_&&!method("GET")&&!method("HEAD")&&!method("OPTIONS"))return fail(Error::InvalidRequest);
      return true;
    }
    if(!used_) {
      headersDone_=true;headerBytes_=examined_;
      if(!bodyMethod_&&bodyBytes_)return fail(Error::UnsupportedBody);
      return headerBytes_+bodyBytes_<=kRequestBytes||fail(Error::RequestTooLarge);
    }
    size_t colon=0;while(colon<used_&&line_[colon]!=':') {
      if(!tokenChar(line_[colon]))return fail(Error::InvalidHeader);
      ++colon;
    }
    if(!colon||colon==used_)return fail(Error::InvalidHeader);
    size_t first=colon+1,last=used_;
    while(first<last&&(line_[first]==' '||line_[first]=='\t'))++first;
    while(last>first&&(line_[last-1]==' '||line_[last-1]=='\t'))--last;
    const char* value=line_.data()+first;const size_t bytes=last-first;
    if(equal(line_.data(),colon,"content-length")) {
      if(lengthSeen_)return fail(Error::DuplicateLength);
      lengthSeen_=true;if(!bytes)return fail(Error::InvalidLength);
      size_t number=0;
      for(size_t n=0;n<bytes;++n) {
        if(value[n]<'0'||value[n]>'9')return fail(Error::InvalidLength);
        const size_t digit=size_t(value[n]-'0');
        if(number>(kRequestBytes-digit)/10)return fail(Error::RequestTooLarge);
        number=number*10+digit;
      }
      bodyBytes_=number;
    } else if(equal(line_.data(),colon,"transfer-encoding")||equal(line_.data(),colon,"expect")||
        equal(line_.data(),colon,"upgrade")||(equal(line_.data(),colon,"content-encoding")&&bytes))
      return fail(Error::UnsupportedFraming);
    else if(equal(line_.data(),colon,"content-type")&&bytes>=10&&equal(value,10,"multipart/"))
      return fail(Error::UnsupportedBody);
    return true;
  }
};
} // namespace management_request
