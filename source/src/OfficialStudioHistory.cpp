#include "OfficialStudioHistory.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#ifndef ARDUINO
#include <pthread.h>
#endif
#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "NativeTlsTrust.h"
#endif

namespace official_history {
namespace {
void wipe(void* memory,size_t count) {
  auto* bytes=static_cast<volatile uint8_t*>(memory);while(count--)*bytes++=0;
}
size_t length(const char* text,size_t maximum) {
  if(!text)return maximum+1;
  size_t count=0;while(count<=maximum&&text[count])++count;return count;
}
template<size_t N> bool copy(std::array<char,N>& output,const char* text) {
  const size_t count=length(text,N-1);if(count>=N)return false;
  std::memcpy(output.data(),text,count+1);return true;
}
void clear(Input& input) {
  input.baseUrl.fill(0);wipe(input.userJwt.data(),input.userJwt.size());
  input.profile.fill(0);input.session.fill(0);input.epoch=input.offset=input.byteOffset=0;
}
bool cancelled(const Cancellation* token){return token&&token->cancelled();}
bool equalAscii(const char* value,const char* expected) {
  if(!value)return false;
  while(*expected) {
    char next=*value++;if(next>='A'&&next<='Z')next=char(next+'a'-'A');
    if(next!=*expected++)return false;
  }
  return !*value;
}
bool jsonMediaType(const char* value) {
  if(!value)return false;
  const char* expected="application/json";
  while(*expected) {
    char next=*value++;if(next>='A'&&next<='Z')next=char(next+'a'-'A');
    if(next!=*expected++)return false;
  }
  while(*value==' '||*value=='\t')++value;
  return !*value||*value==';';
}
bool nonblank(const char* value,size_t count) {
  // ECMAScript whitespace, without importing an unbounded JSON DOM. The UTF-8
  // and control checks run first, so scalar lengths are safe to inspect here.
  for(size_t at=0;at<count;) {
    const auto* p=reinterpret_cast<const uint8_t*>(value+at);
    size_t n=0;
    if(p[0]==' ')n=1;
    else if(count-at>=2&&p[0]==0xc2&&p[1]==0xa0)n=2;
    else if(count-at>=3&&((p[0]==0xe1&&p[1]==0x9a&&p[2]==0x80)||
        (p[0]==0xe2&&p[1]==0x80&&((p[2]>=0x80&&p[2]<=0x8a)||p[2]==0xa8||p[2]==0xa9||p[2]==0xaf))||
        (p[0]==0xe2&&p[1]==0x81&&p[2]==0x9f)||(p[0]==0xe3&&p[1]==0x80&&p[2]==0x80)||
        (p[0]==0xef&&p[1]==0xbb&&p[2]==0xbf)))n=3;
    if(!n)return true;at+=n;
  }
  return false;
}
bool identifier(const char* text,size_t maximum) {
  const size_t count=length(text,maximum);
  if(!count||count>maximum||!native_chat::validUtf8(text,count))return false;
  for(size_t n=0;n<count;++n)if(uint8_t(text[n])<0x20||text[n]==0x7f)return false;
  return nonblank(text,count);
}
Result httpResult(int status) {
  if(status==200)return Result::Ok;
  if(status==404)return Result::Empty;
  if(status==401)return Result::AuthInvalid;
  if(status<=0||status==408||status==425||status==429||status>=500)return Result::Transient;
  return Result::Invalid; // A profile/session 403 is not an invalid account JWT.
}
using Path=bounded_json::PathView;
bool key(Path path,size_t index,const char* name) {
  return index<path.size&&path.segments[index].kind==bounded_json::SegmentKind::Key&&
      path.segments[index].keyBytes==std::strlen(name)&&
      !std::memcmp(path.segments[index].key,name,path.segments[index].keyBytes);
}
bool numeric(bounded_json::Type type) {
  using bounded_json::Type;
  return type==Type::NumberBegin||type==Type::NumberChunk||type==Type::NumberEnd;
}
bool string(bounded_json::Type type) {
  using bounded_json::Type;
  return type==Type::StringBegin||type==Type::StringChunk||type==Type::StringEnd;
}

// Snapshot owns display windows, while this wrapper requires the official
// response envelope. All callbacks remain tentative until finish() succeeds.
class Projection final:public bounded_json::Listener {
 public:
  explicit Projection(Snapshot& snapshot):snapshot_(snapshot){}
  bool onEvent(const bounded_json::Event& value)override {
    using bounded_json::Type;
    const auto path=value.path;const Type type=value.type;
    if(!path.size) {
      if(type!=Type::ObjectBegin&&type!=Type::ObjectEnd)return false;
      if(type==Type::ObjectBegin)root_=true;
    } else if(path.size==1) {
      if(key(path,0,"session")) {
        if(type==Type::ObjectBegin)session_=true;
        else if(type!=Type::Key&&type!=Type::ObjectEnd)return false;
      } else if(key(path,0,"messages")) {
        if(type==Type::ArrayBegin)messages_=true;
        else if(type!=Type::Key&&type!=Type::ArrayEnd)return false;
      } else if(key(path,0,"total")||key(path,0,"offset")) {
        if(type!=Type::Key&&!numeric(type))return false;
        if(type==Type::NumberEnd){if(key(path,0,"total"))total_=true;else offset_=true;}
      } else if(key(path,0,"hasMore")) {
        if(type==Type::Boolean)more_=true;else if(type!=Type::Key)return false;
      }
    } else if(path.size==2&&key(path,0,"session")) {
      if(key(path,1,"id")) {
        if(type!=Type::Key&&!string(type))return false;
        if(type==Type::StringEnd)id_=true;
      } else if(key(path,1,"profile")) {
        if(type==Type::Key){if(profileKey_)return false;profileKey_=true;}
        else if(!string(type))return false;
        if(type==Type::StringChunk) {
          if(value.size>profile_.size()-1-profileUsed_||std::memchr(value.data,0,value.size))return false;
          std::memcpy(profile_.data()+profileUsed_,value.data,value.size);profileUsed_+=value.size;
          profile_[profileUsed_]=0;
        }
        if(type==Type::StringEnd)profileDone_=true;
      }
    } else if(path.size==2&&key(path,0,"messages")&&
        path.segments[1].kind==bounded_json::SegmentKind::Index) {
      if(path.segments[1].index>=kPageSize)return false;
      if(type!=Type::ObjectBegin&&type!=Type::ObjectEnd)return false;
    }
    return snapshot_.onEvent(value);
  }
  bool valid(const Input& input)const {
    if(!root_||!session_||!messages_||!total_||!offset_||!id_||!more_||!profileDone_||
        !snapshot_.hasTotal()||snapshot_.overflow||snapshot_.offset!=input.offset||
        std::strcmp(snapshot_.session.data(),input.session.data())||
        std::strcmp(profile_.data(),input.profile.data())||snapshot_.rows>kPageSize)return false;
    const uint64_t end=uint64_t(snapshot_.offset)+snapshot_.rows;
    if(snapshot_.rows&&end>snapshot_.total)return false;
    return snapshot_.hasMore==(end<snapshot_.total);
  }
 private:
  Snapshot& snapshot_;
  std::array<char,kProfileBytes+1> profile_{};size_t profileUsed_=0;
  bool root_=false,session_=false,messages_=false,total_=false,offset_=false,id_=false,more_=false;
  bool profileKey_=false,profileDone_=false;
};

class Sink final:public ResponseSink {
 public:
  Sink(const Input& input,Snapshot& snapshot,const Cancellation* cancellation):
      projection_(snapshot),reader_(projection_),input_(input),cancellation_(cancellation){}
  int status=0;
  bool begin(const Headers& headers)override {
    if(begun_)return fail(Result::Invalid);
    begun_=true;status=headers.status;expected_=headers.contentLength;
    if(cancelled(cancellation_))return fail(Result::Cancelled);
    const auto code=httpResult(status);if(code!=Result::Ok)return fail(code);
    if(expected_<-1||!jsonMediaType(headers.contentType)||
        (headers.contentEncoding&&*headers.contentEncoding&&!equalAscii(headers.contentEncoding,"identity")))
      return fail(Result::Invalid);
    return true;
  }
  bool write(const uint8_t* bytes,size_t count)override {
    if(result_!=Result::Ok)return false;
    if(!begun_||(!bytes&&count)||count>UINT64_MAX-received_)return fail(Result::Invalid);
    if(cancelled(cancellation_))return fail(Result::Cancelled);
    received_+=count;
    if(expected_>=0&&received_>uint64_t(expected_))return fail(Result::Invalid);
    if(!reader_.feed(bytes,count))return fail(Result::Invalid);
    return true;
  }
  Result finish(bool complete) {
    if(cancelled(cancellation_))return Result::Cancelled;
    if(result_!=Result::Ok)return result_;
    if(!begun_||!complete||(expected_>=0&&received_!=uint64_t(expected_)))return Result::Transient;
    return reader_.finish()&&reader_.complete()&&projection_.valid(input_)?Result::Ok:Result::Invalid;
  }
 private:
  Projection projection_;bounded_json::Reader<32,256,64> reader_;
  const Input& input_;const Cancellation* cancellation_;
  Result result_=Result::Ok;bool begun_=false;int64_t expected_=-1;uint64_t received_=0;
  bool fail(Result result){result_=result;return false;}
};

constexpr size_t kPathBytes=(kProfileBytes+kSessionBytes)*3+160;
bool append(char* path,size_t& used,const char* text,bool encode) {
  static const char hex[]="0123456789ABCDEF";
  for(const auto* p=reinterpret_cast<const uint8_t*>(text);*p;++p) {
    const bool literal=!encode||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||
        (*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'||*p=='~';
    const size_t count=literal?1:3;if(count>=kPathBytes-used)return false;
    if(literal)path[used++]=char(*p);
    else {path[used++]='%';path[used++]=hex[*p>>4];path[used++]=hex[*p&15];}
  }
  path[used]=0;return true;
}
bool pathFor(char* path,const Input& input) {
  size_t used=0;char offset[11];std::snprintf(offset,sizeof(offset),"%lu",static_cast<unsigned long>(input.offset));
  return append(path,used,"/api/studio/sessions/conversations/",false)&&append(path,used,input.session.data(),true)&&
      append(path,used,"/messages/paginated?limit=12&offset=",false)&&append(path,used,offset,false)&&
      append(path,used,"&profile=",false)&&append(path,used,input.profile.data(),true);
}
} // namespace

Input::~Input(){wipe(userJwt.data(),userJwt.size());}
bool validInput(const Input& input) {
  const size_t base=length(input.baseUrl.data(),kBaseUrlBytes),jwt=length(input.userJwt.data(),kJwtBytes);
  if(!base||base>kBaseUrlBytes||!jwt||jwt>kJwtBytes||!input.epoch||
      !identifier(input.profile.data(),kProfileBytes)||!identifier(input.session.data(),kSessionBytes))return false;
  const char* authority=nullptr;
  if(!std::strncmp(input.baseUrl.data(),"http://",7))authority=input.baseUrl.data()+7;
  if(!std::strncmp(input.baseUrl.data(),"https://",8))authority=input.baseUrl.data()+8;
  if(!authority||!*authority||*authority=='/'||*authority==':')return false;
  for(const char* p=input.baseUrl.data();*p;++p)if(uint8_t(*p)<=0x20||*p=='\\'||*p=='@'||*p=='?'||*p=='#')return false;
  for(const char* p=input.userJwt.data();*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||
      (*p>='0'&&*p<='9')||*p=='.'||*p=='_'||*p=='-'))return false;
  return true;
}
Result makeInput(Input& output,const char* base,const char* jwt,const char* profile,const char* session,
                 uint32_t epoch,uint32_t offset,uint32_t byteOffset) {
  // Heap transaction supports even cross-field aliases into output, without a
  // second ~5KiB value on the Arduino loop stack or partially updating a bind.
  auto candidate=std::unique_ptr<Input>(new(std::nothrow)Input);
  if(!candidate){clear(output);return Result::NoMemory;}
  candidate->epoch=epoch;candidate->offset=offset;candidate->byteOffset=byteOffset;
  if(!copy(candidate->baseUrl,base)||!copy(candidate->userJwt,jwt)||!copy(candidate->profile,profile)||
      !copy(candidate->session,session)||!validInput(*candidate)){clear(output);return Result::Invalid;}
  output=*candidate;return Result::Ok;
}
Output resolve(const Input& input,Transport& transport,const Cancellation* cancellation) {
  Output output;output.epoch=input.epoch;output.offset=input.offset;output.byteOffset=input.byteOffset;
  output.profile=input.profile;output.session=input.session;
  if(!validInput(input))return output;
  if(cancelled(cancellation)){output.result=Result::Cancelled;return output;}
  auto snapshot=std::unique_ptr<Snapshot>(new(std::nothrow)Snapshot(official_incoming::Kind::History,input.byteOffset));
  auto path=std::unique_ptr<char[]>(new(std::nothrow)char[kPathBytes]);
  if(!snapshot||!path){output.result=Result::NoMemory;return output;}
  auto sink=std::unique_ptr<Sink>(new(std::nothrow)Sink(input,*snapshot,cancellation));
  if(!sink){output.result=Result::NoMemory;return output;}
  if(!pathFor(path.get(),input))return output;
  Request request;request.input=&input;request.path=path.get();
  const bool complete=!cancelled(cancellation)&&transport.get(request,*sink);
  output.httpCode=sink->status;output.result=sink->finish(complete);
  if(output.result==Result::Ok)output.snapshot=std::move(snapshot);
  return output;
}

#ifdef ARDUINO
namespace {
// Arduino-ESP32 2.0.17 writeToStreamDataBlock() has no idle timeout when the
// connection stays open with available()==0. Its chunk-header Stream reads can
// also wait independently. Check the request's absolute deadline/cancellation
// at the actual Client boundary, including while no body callback is invoked.
template<class Base> class DeadlineClient final:public Base {
 public:
  using Base::connect;
  using Base::read;
  DeadlineClient(uint32_t started,uint32_t timeout,const Cancellation* token):
      started_(started),timeout_(timeout),token_(token){}
  int available()override{return expired()?0:std::max(0,Base::available());}
  uint8_t connected()override{return expired()?0:Base::connected();}
  int read()override{return expired()?-1:Base::read();}
  int read(uint8_t* bytes,size_t count)override{return expired()?-1:Base::read(bytes,count);}
  int peek()override{return expired()?-1:Base::peek();}
  size_t write(uint8_t byte)override{return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count)override{return expired()?0:Base::write(bytes,count);}
  int connect(const char* host,uint16_t port,int32_t timeout)override {
    if(expired())return 0;
    const uint32_t elapsed=uint32_t(millis()-started_);
    if(elapsed>=timeout_){expired();return 0;}
    const int32_t left=int32_t(timeout_-elapsed);
    const int result=Base::connect(host,port,std::min(timeout>0?timeout:left,left));
    // SDK DNS/connect/TLS are synchronous and may outlive this budget. Deny
    // their late result and any subsequent request write; never commit it.
    return expired()?0:result;
  }
 private:
  uint32_t started_,timeout_;const Cancellation* token_;bool stopped_=false;
  bool expired() {
    if(stopped_)return true;
    if(!cancelled(token_)&&uint32_t(millis()-started_)<timeout_)return false;
    stopped_=true;Base::stop();this->Stream::setTimeout(0);return true;
  }
};
class SinkStream final:public Stream {
 public:
  SinkStream(ResponseSink& sink,uint32_t started,uint32_t timeout):sink_(sink),started_(started),timeout_(timeout){}
  size_t write(uint8_t byte)override{return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count)override {
    if(failed_||uint32_t(millis()-started_)>=timeout_||!sink_.write(bytes,count)){failed_=true;return 0;}
    return count;
  }
  int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
  bool failed()const{return failed_;}
 private:
  ResponseSink& sink_;uint32_t started_,timeout_;bool failed_=false;
};
// Borrow the branch-owned socket, never a second idle TLS client on plain
// LAN. HTTPClient and its body stream are destroyed before the socket owner.
bool getWithClient(WiFiClient& client,const String& url,uint32_t started,
                   const Request& request,ResponseSink& sink) {
    HTTPClient http;
    http.setReuse(false);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(request.timeoutMs);http.setTimeout(request.timeoutMs);
    if(!http.begin(client,url))return false;
    const char* names[]{"Content-Type","Content-Encoding"};http.collectHeaders(names,2);
    http.addHeader("Authorization",String("Bearer ")+request.input->userJwt.data());
    http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
    const int status=http.GET();const String type=http.header("Content-Type"),encoding=http.header("Content-Encoding");
    Headers headers;headers.status=status;headers.contentType=type.c_str();headers.contentEncoding=encoding.c_str();
    headers.contentLength=http.getSize();
    if(!sink.begin(headers)){http.end();return false;}
    SinkStream stream(sink,started,request.timeoutMs);
    const int copied=http.writeToStream(&stream);http.end();
    return copied>=0&&!stream.failed()&&uint32_t(millis()-started)<request.timeoutMs;
}
class ArduinoTransport final:public Transport {
 public:
  explicit ArduinoTransport(const Cancellation* token):token_(token){}
  bool get(const Request& request,ResponseSink& sink)override {
    const uint32_t started=millis();
    String base(request.input->baseUrl.data());while(base.endsWith("/"))base.remove(base.length()-1);
    const String url=base+request.path;
    if(url.length()!=base.length()+std::strlen(request.path))return false;
    if(base.startsWith("https://")) {
      DeadlineClient<WiFiClientSecure> secure(started,request.timeoutMs,token_);
      native_tls::configureTrust(secure);secure.setHandshakeTimeout((request.timeoutMs+999)/1000);
      return getWithClient(secure,url,started,request,sink);
    }
    DeadlineClient<WiFiClient> plain(started,request.timeoutMs,token_);
    return getWithClient(plain,url,started,request,sink);
  }
 private:
  const Cancellation* token_;
};
} // namespace
#endif

struct Job::State final:Cancellation {
  Input input;Output output;std::unique_ptr<Transport> transport;
  std::atomic<unsigned> references{2};std::atomic<bool> cancelledFlag{false},done{false};
  bool cancelled()const override{return cancelledFlag.load(std::memory_order_relaxed);}
  void release(){if(references.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;}
};
Job::~Job(){if(state_){state_->cancelledFlag.store(true,std::memory_order_relaxed);state_->release();}}
StartResult Job::start(const Input& input,std::unique_ptr<Transport> transport) {
  if(state_)return StartResult::Busy;
  if(!validInput(input))return StartResult::Invalid;
#ifndef ARDUINO
  if(!transport)return StartResult::NotSupported;
#endif
  auto* state=new(std::nothrow)State;if(!state)return StartResult::NoMemory;
  state->input=input;state->transport=std::move(transport);state_=state;
#ifdef ARDUINO
  if(xTaskCreate(worker,"studio-history",kTaskStackBytes,state,1,nullptr)!=pdPASS) {
    state_=nullptr;state->release();state->release();return StartResult::NoMemory;
  }
#else
  pthread_attr_t attributes;int error=pthread_attr_init(&attributes);
  if(!error) {
    error=pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED);
    if(!error) {
      pthread_t thread;
      error=pthread_create(&thread,&attributes,[](void* value)->void*{Job::worker(value);return nullptr;},state);
    }
    pthread_attr_destroy(&attributes);
  }
  if(error){state_=nullptr;state->release();state->release();return StartResult::NoMemory;}
#endif
  return StartResult::Started;
}
Poll Job::poll(Output& output) {
  if(!state_)return Poll::Idle;
  if(!state_->done.load(std::memory_order_acquire))return Poll::Pending;
  output=std::move(state_->output);
  if(state_->cancelled()){output.result=Result::Cancelled;output.snapshot.reset();}
  State* finished=state_;state_=nullptr;finished->release();return Poll::Ready;
}
void Job::cancel(){if(state_)state_->cancelledFlag.store(true,std::memory_order_relaxed);}
bool Job::busy()const{return state_!=nullptr;}
void Job::worker(void* parameter) {
  auto* state=static_cast<State*>(parameter);
  {
#ifdef ARDUINO
    ArduinoTransport transport(state);
    state->output=resolve(state->input,state->transport?*state->transport:transport,state);
#else
    state->output=resolve(state->input,*state->transport,state);
#endif
    state->transport.reset();
  } // All HTTP/SAX resources are gone before exposing this owning snapshot.
  state->done.store(true,std::memory_order_release);
  state->release();
#ifdef ARDUINO
  vTaskDelete(nullptr);
#endif
}
} // namespace official_history
