#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// A bounded RFC 6455 server-upgrade response. It consumes headers only: the
// caller stops at Complete, leaving an immediately coalesced frame unread.
namespace studio_handshake {
constexpr size_t kHeaderBytes=8192,kLineBytes=1024,kAcceptBytes=28;
enum class Error:uint8_t {None,InvalidExpectedAccept,Deadline,TooLarge,LineTooLong,InvalidLine,
  InvalidStatus,InvalidHeader,DuplicateAccept,InvalidAccept,MissingUpgrade,MissingConnection,
  MissingAccept,UnexpectedProtocol,UnexpectedBody};
enum class State:uint8_t {Reading,Complete,Failed};
class Parser {
 public:
  explicit Parser(const char* expected,uint32_t deadlineMs=8000):deadline_(deadlineMs) {
    size_t count=0;if(expected)while(count<=kAcceptBytes&&expected[count])++count;
    if(!expected || count!=kAcceptBytes || !deadlineMs) {fail(Error::InvalidExpectedAccept);return;}
    std::memcpy(expected_.data(),expected,kAcceptBytes+1);
  }
  bool feed(uint8_t byte,uint32_t elapsedMs) {
    if(state_!=State::Reading) return false;
    if(elapsedMs>=deadline_) return fail(Error::Deadline);
    if(++total_>kHeaderBytes) return fail(Error::TooLarge);
    if(carriage_) {
      carriage_=false;
      if(byte!='\n') return fail(Error::InvalidLine);
      line_[used_]=0;const bool ok=finishLine();used_=0;return ok;
    }
    if(byte=='\r') {carriage_=true;return true;}
    if(byte=='\n' || !byte || byte==0x7f || (byte<0x20 && byte!='\t') || byte>0x7e)
      return fail(Error::InvalidLine);
    if(used_==kLineBytes) return fail(Error::LineTooLong);
    line_[used_++]=char(byte);return true;
  }
  bool expired(uint32_t elapsedMs) {
    if(state_==State::Reading && elapsedMs>=deadline_) return fail(Error::Deadline);
    return state_!=State::Failed;
  }
  State state()const{return state_;}
  Error error()const{return error_;}
  size_t bytes()const{return total_;}
  uint16_t status()const{return status_;}
 private:
  State state_=State::Reading;Error error_=Error::None;
  std::array<char,kLineBytes+1> line_{};
  std::array<char,kAcceptBytes+1> expected_{};
  size_t used_=0,total_=0;uint32_t deadline_=0;uint16_t status_=0;
  bool carriage_=false,statusRead_=false,upgrade_=false,connection_=false,accept_=false;
  bool fail(Error error){error_=error;state_=State::Failed;return false;}
  static char lower(char c){return c>='A'&&c<='Z'?char(c+'a'-'A'):c;}
  static bool equal(const char* value,size_t bytes,const char* expected) {
    if(std::strlen(expected)!=bytes)return false;
    for(size_t n=0;n<bytes;++n)if(lower(value[n])!=expected[n])return false;
    return true;
  }
  static bool tokenChar(char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||
      std::strchr("!#$%&'*+-.^_`|~",c)!=nullptr;
  }
  static bool containsToken(const char* value,size_t bytes,const char* expected) {
    size_t at=0;bool found=false;
    while(at<bytes) {
      while(at<bytes&&(value[at]==' '||value[at]=='\t'))++at;
      const size_t first=at;while(at<bytes&&tokenChar(value[at]))++at;
      if(first==at)return false;
      if(equal(value+first,at-first,expected))found=true;
      while(at<bytes&&(value[at]==' '||value[at]=='\t'))++at;
      if(at==bytes)return found;
      if(value[at++]!=',' || at==bytes)return false;
    }
    return found;
  }
  bool finishLine() {
    if(!statusRead_) {
      statusRead_=true;
      if(used_<12 || std::memcmp(line_.data(),"HTTP/1.1 ",9) ||
          line_[9]<'0'||line_[9]>'9'||line_[10]<'0'||line_[10]>'9'||line_[11]<'0'||line_[11]>'9'||
          (used_>12&&line_[12]!=' ')) return fail(Error::InvalidStatus);
      status_=uint16_t((line_[9]-'0')*100+(line_[10]-'0')*10+line_[11]-'0');
      return status_==101 || fail(Error::InvalidStatus);
    }
    if(!used_) {
      if(!upgrade_)return fail(Error::MissingUpgrade);
      if(!connection_)return fail(Error::MissingConnection);
      if(!accept_)return fail(Error::MissingAccept);
      state_=State::Complete;return true;
    }
    size_t colon=0;while(colon<used_&&line_[colon]!=':') {
      if(!tokenChar(line_[colon]))return fail(Error::InvalidHeader);
      ++colon;
    }
    if(!colon || colon==used_)return fail(Error::InvalidHeader);
    size_t first=colon+1,last=used_;
    while(first<last&&(line_[first]==' '||line_[first]=='\t'))++first;
    while(last>first&&(line_[last-1]==' '||line_[last-1]=='\t'))--last;
    const char* value=line_.data()+first;const size_t bytes=last-first;
    if(equal(line_.data(),colon,"upgrade")) {
      if(!containsToken(value,bytes,"websocket"))return fail(Error::MissingUpgrade);
      upgrade_=true;
    } else if(equal(line_.data(),colon,"connection")) {
      if(containsToken(value,bytes,"upgrade"))connection_=true;
    } else if(equal(line_.data(),colon,"sec-websocket-accept")) {
      if(accept_)return fail(Error::DuplicateAccept);
      if(bytes!=kAcceptBytes || std::memcmp(value,expected_.data(),kAcceptBytes))return fail(Error::InvalidAccept);
      accept_=true;
    } else if(equal(line_.data(),colon,"sec-websocket-protocol")||equal(line_.data(),colon,"sec-websocket-extensions"))
      return fail(Error::UnexpectedProtocol);
    else if(equal(line_.data(),colon,"transfer-encoding")||
        (equal(line_.data(),colon,"content-length") && !(bytes==1&&value[0]=='0')))
      return fail(Error::UnexpectedBody);
    return true;
  }
};
} // namespace studio_handshake
