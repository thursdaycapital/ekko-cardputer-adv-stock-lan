#include "OfficialStudioSelection.h"
#include "BoundedJsonReader.h"
#include "ChatUtf8.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
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

namespace official_selection {
namespace {
void wipe(void* memory,size_t bytes) {
  volatile uint8_t* next=static_cast<volatile uint8_t*>(memory);
  while(bytes--) *next++=0;
}
size_t boundedLength(const char* value,size_t limit) {
  if(!value) return limit+1;
  size_t bytes=0;while(bytes<=limit && value[bytes]) ++bytes;return bytes;
}
template<size_t N> bool copy(std::array<char,N>& out,const char* text) {
  const size_t bytes=boundedLength(text,N-1);
  if(bytes>=N) return false;
  std::memcpy(out.data(),text,bytes+1);return true;
}
bool cancelled(const Cancellation* token) {return token && token->cancelled();}
void clearInput(Input& input) {
  input.baseUrl.fill(0);wipe(input.userJwt.data(),input.userJwt.size());
  input.profile.fill(0);input.agentId.fill(0);input.epoch=0;
}
bool jsonMediaType(const char* text) {
  if(!text) return false;
  const char* expected="application/json";
  while(*expected) {
    char c=*text++;if(c>='A'&&c<='Z')c=char(c+'a'-'A');
    if(c!=*expected++)return false;
  }
  while(*text==' '||*text=='\t')++text;
  return !*text||*text==';';
}
Result httpResult(Stage stage,int status) {
  if(status==200)return Result::Ok;
  if(status==401 || (stage==Stage::Account && (status==403||status==404)))return Result::AuthInvalid;
  if(status<=0||status==408||status==425||status==429||status>=500)return Result::Transient;
  if(status==403||status==404)return Result::Unavailable; // Profile/Agent access, not an expired JWT.
  return Result::Invalid;
}
class Budget final : public ArduinoJson::Allocator {
 public:
  explicit Budget(size_t capacity):capacity_(capacity){}
  void* allocate(size_t bytes) override {
    if(bytes>capacity_ || sizeof(Header)>capacity_-bytes || bytes+sizeof(Header)>capacity_-used_)return nullptr;
    auto* header=static_cast<Header*>(std::malloc(sizeof(Header)+bytes));if(!header)return nullptr;
    header->bytes=bytes;used_+=bytes+sizeof(Header);peak_=std::max(peak_,used_);return header+1;
  }
  void deallocate(void* pointer) override {
    if(!pointer)return;auto* header=static_cast<Header*>(pointer)-1;
    used_-=header->bytes+sizeof(Header);std::free(header);
  }
  void* reallocate(void* pointer,size_t bytes) override {
    if(!pointer)return allocate(bytes);
    auto* header=static_cast<Header*>(pointer)-1;const size_t old=header->bytes;
    if(bytes>capacity_ || sizeof(Header)>capacity_-bytes || bytes+sizeof(Header)>capacity_-(used_-old-sizeof(Header)))return nullptr;
    auto* next=static_cast<Header*>(std::realloc(header,sizeof(Header)+bytes));if(!next)return nullptr;
    next->bytes=bytes;used_=used_-old+bytes;peak_=std::max(peak_,used_);return next+1;
  }
  size_t peak()const{return peak_;}
 private:
  union Header {size_t bytes;std::max_align_t alignment;};
  size_t capacity_,used_=0,peak_=0;
};

enum class Wanted:uint8_t {Skip,Object,Array,String,Boolean,Number};
using Path=bounded_json::PathView;
bool key(Path path,size_t at,const char* value) {
  return at<path.size && path.segments[at].kind==bounded_json::SegmentKind::Key &&
      path.segments[at].keyBytes==std::strlen(value) && !std::memcmp(path.segments[at].key,value,path.segments[at].keyBytes);
}
bool index(Path path,size_t at) {return at<path.size&&path.segments[at].kind==bounded_json::SegmentKind::Index;}

// Project only the official selection schema while SAX validates all other
// bytes. No provider api_key/base_url, avatar, CLI output or server error string
// is copied. Every retained object key is reserved at Key, before its type is
// known, so null/wrong-type duplicates cannot bypass ambiguity checks.
class Projection final : public bounded_json::Listener {
 public:
  Projection(Stage stage,JsonDocument& document):stage_(stage),document_(document){}
  Result result=Result::Ok;
  bool onEvent(const bounded_json::Event& event) override {
    using bounded_json::Type;
    const Wanted wanted=field(event.path);
    if(wanted==Wanted::Skip)return true;
    if(event.type==Type::Key) {
      if(std::memchr(event.data,0,event.size))return fail(Result::Invalid);
      if(!locate(event.path,false).isUnbound())return fail(Result::Invalid);
      JsonVariant reserved=locate(event.path,true);
      return !reserved.isUnbound() && memoryOk();
    }
    if(event.type==Type::ObjectBegin||event.type==Type::ArrayBegin) {
      if((event.type==Type::ObjectBegin&&wanted!=Wanted::Object)||
         (event.type==Type::ArrayBegin&&wanted!=Wanted::Array))return fail(Result::Invalid);
      JsonVariant target=locate(event.path,true);
      if(event.type==Type::ObjectBegin)target.to<JsonObject>();else target.to<JsonArray>();
      return memoryOk();
    }
    if(event.type==Type::ObjectEnd||event.type==Type::ArrayEnd)return true;
    if(event.type==Type::StringBegin||event.type==Type::NumberBegin) {
      if((event.type==Type::StringBegin&&wanted!=Wanted::String)||
         (event.type==Type::NumberBegin&&wanted!=Wanted::Number))return fail(Result::Invalid);
      used_=0;buffer_[0]=0;return true;
    }
    if(event.type==Type::StringChunk||event.type==Type::NumberChunk) {
      if(event.size>sizeof(buffer_)-1-used_||std::memchr(event.data,0,event.size))return fail(Result::Invalid);
      std::memcpy(buffer_+used_,event.data,event.size);used_+=event.size;buffer_[used_]=0;return true;
    }
    if(event.type==Type::StringEnd) {
      locate(event.path,true).set(buffer_);return memoryOk();
    }
    if(event.type==Type::NumberEnd) {
      uint64_t value=0;
      for(size_t n=0;n<used_;++n) {
        if(buffer_[n]<'0'||buffer_[n]>'9')return fail(Result::Invalid);
        const uint8_t digit=uint8_t(buffer_[n]-'0');
        if(value>(UINT64_C(9007199254740991)-digit)/10)return fail(Result::Invalid);
        value=value*10+digit;
      }
      if(!used_||!value)return fail(Result::Invalid);
      locate(event.path,true).set(value);return memoryOk();
    }
    if(event.type==Type::Boolean) {
      if(wanted!=Wanted::Boolean)return fail(Result::Invalid);
      locate(event.path,true).set(event.booleanValue);return memoryOk();
    }
    if(event.type==Type::Null && stage_==Stage::Models && key(event.path,event.path.size-1,"api_mode"))return true;
    return fail(Result::Invalid);
  }
 private:
  Stage stage_;JsonDocument& document_;char buffer_[513]{};size_t used_=0;
  bool fail(Result failure){result=failure;return false;}
  bool memoryOk(){return result==Result::Ok && (!document_.overflowed()||fail(Result::NoMemory));}
  Wanted field(Path path)const {
    if(!path.size)return Wanted::Object;
    if(stage_==Stage::Account) {
      if(key(path,0,"user")) {
        if(path.size==1)return Wanted::Object;
        if(path.size==2&&key(path,1,"id"))return Wanted::Number;
      }
      return Wanted::Skip;
    }
    if(stage_==Stage::Installed||stage_==Stage::NativeModels) {
      const bool native=stage_==Stage::NativeModels;
      if(!key(path,0,native?"agents":"tools"))return Wanted::Skip;
      if(path.size==1)return Wanted::Array;
      if(!index(path,1))return Wanted::Skip;
      if(path.size==2)return Wanted::Object;
      if(path.size==3) {
        if(key(path,2,native?"agentId":"id")||(native&&key(path,2,"status")))return Wanted::String;
        if(!native&&key(path,2,"installed"))return Wanted::Boolean;
        if(native&&key(path,2,"models"))return Wanted::Array;
      }
      if(native&&key(path,2,"models")&&index(path,3)) {
        if(path.size==4)return Wanted::Object;
        if(path.size==5&&key(path,4,"id"))return Wanted::String;
        if(path.size==5&&key(path,4,"hidden"))return Wanted::Boolean;
      }
      return Wanted::Skip;
    }
    if(stage_!=Stage::Models||!key(path,0,"profiles"))return Wanted::Skip;
    if(path.size==1)return Wanted::Array;
    if(!index(path,1))return Wanted::Skip;
    if(path.size==2)return Wanted::Object;
    if(path.size==3) {
      if(key(path,2,"profile")||key(path,2,"default")||key(path,2,"default_provider"))return Wanted::String;
      if(key(path,2,"groups"))return Wanted::Array;
    }
    if(!key(path,2,"groups")||!index(path,3))return Wanted::Skip;
    if(path.size==4)return Wanted::Object;
    if(path.size==5) {
      if(key(path,4,"provider")||key(path,4,"api_mode"))return Wanted::String;
      if(key(path,4,"models"))return Wanted::Array;
      if(key(path,4,"model_meta"))return Wanted::Object;
    }
    if(path.size==6&&key(path,4,"models")&&index(path,5))return Wanted::String;
    if(key(path,4,"model_meta")&&path.segments[5].kind==bounded_json::SegmentKind::Key) {
      if(path.size==6)return Wanted::Object;
      if(path.size==7&&(key(path,6,"disabled")||key(path,6,"hidden")))return Wanted::Boolean;
    }
    return Wanted::Skip;
  }
  JsonVariant locate(Path path,bool create) {
    if(!path.size) {
      if(create&&document_.isNull())document_.to<JsonObject>();
      return document_.as<JsonVariant>();
    }
    JsonVariant current=document_.as<JsonVariant>();
    for(size_t n=0;n<path.size;++n) {
      const auto& part=path.segments[n];
      if(part.kind==bounded_json::SegmentKind::Key) {
        if(!current.is<JsonObject>()) {fail(Result::Invalid);return {};}
        if(std::memchr(part.key,0,part.keyBytes)) {fail(Result::Invalid);return {};}
        auto object=current.as<JsonObject>();
        if(create&&object[part.key].isUnbound())object[part.key]=nullptr;
        current=object[part.key].as<JsonVariant>();
      } else {
        if(!current.is<JsonArray>()||part.index>SIZE_MAX) {fail(Result::Invalid);return {};}
        auto array=current.as<JsonArray>();
        if(create)while(array.size()<=size_t(part.index))if(!array.add(nullptr)) {fail(Result::NoMemory);return {};}
        current=array[size_t(part.index)].as<JsonVariant>();
      }
      if(current.isUnbound())return current;
    }
    return current;
  }
};

class Sink final : public ResponseSink {
 public:
  Sink(Stage stage,JsonDocument& document,const Cancellation* cancellation):
      projection(stage,document),reader(projection),stage_(stage),cancellation_(cancellation){}
  Projection projection;
  bounded_json::Reader<kJsonMaxDepth,kJsonKeyBytes,64> reader;
  Result result=Result::Ok;
  int status=0;uint32_t retry=0;
  bool begin(const Headers& headers)override {
    if(begun_)return failure(Result::Invalid);
    begun_=true;status=headers.status;length_=headers.contentLength;
    retry=std::min<uint32_t>(headers.retryAfterMs,30000);
    if(cancelled(cancellation_))return failure(Result::Cancelled);
    const Result code=httpResult(stage_,status);if(code!=Result::Ok)return failure(code);
    if(length_<-1||!jsonMediaType(headers.contentType))return failure(Result::Invalid);
    return true;
  }
  bool write(const uint8_t* bytes,size_t count)override {
    if(result!=Result::Ok)return false;
    if(!begun_||(!bytes&&count)||count>UINT64_MAX-received_)return failure(Result::Invalid);
    if(cancelled(cancellation_))return failure(Result::Cancelled);
    received_+=count;
    if(length_>=0&&received_>uint64_t(length_))return failure(Result::Invalid);
    if(!reader.feed(bytes,count))return failure(projection.result==Result::Ok?Result::Invalid:projection.result);
    return true;
  }
  Result finish(bool completed) {
    if(cancelled(cancellation_))return Result::Cancelled;
    if(result!=Result::Ok)return result;
    if(!begun_||!completed||(length_>=0&&received_!=uint64_t(length_)))return Result::Transient;
    if(!reader.finish())return projection.result==Result::Ok?Result::Invalid:projection.result;
    return Result::Ok;
  }
 private:
  Stage stage_;const Cancellation* cancellation_;bool begun_=false;int64_t length_=-1;uint64_t received_=0;
  bool failure(Result failure){result=failure;return false;}
};
constexpr size_t kFixedJsonBytes=sizeof(Sink)+sizeof(Budget)+2*sizeof(JsonDocument);
static_assert(kFixedJsonBytes<kJsonMemoryBytes/2,"SAX workspace must leave a useful document budget");

bool pathFor(char* output,size_t capacity,Stage stage,const Input& input) {
  char encoded[kProfileBytes*3+1]{};size_t count=0;
  static const char hex[]="0123456789ABCDEF";
  for(const unsigned char* p=reinterpret_cast<const unsigned char*>(input.profile.data());*p;++p) {
    if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'||*p=='~')encoded[count++]=char(*p);
    else {encoded[count++]='%';encoded[count++]=hex[*p>>4];encoded[count++]=hex[*p&15];}
  }
  int bytes=-1;
  if(stage==Stage::Account)bytes=std::snprintf(output,capacity,"/api/auth/me");
  if(stage==Stage::Models)bytes=std::snprintf(output,capacity,"/api/hermes/available-models?profile=%s",encoded);
  if(stage==Stage::Installed)bytes=std::snprintf(output,capacity,"/api/coding-agents?profile=%s",encoded);
  if(stage==Stage::NativeModels)bytes=std::snprintf(output,capacity,"/api/coding-agents/models?agent=%s&profile=%s",input.agentId.data(),encoded);
  return bytes>=0&&size_t(bytes)<capacity;
}
bool fetch(const Input& input,Stage stage,Transport& transport,const Cancellation* cancellation,
           Budget& budget,JsonDocument& document,Output& output) {
  output.stage=stage;document.clear();
  char path[kProfileBytes*3+100]{};
  if(!pathFor(path,sizeof(path),stage,input)) {output.result=Result::Invalid;return false;}
  auto sink=std::unique_ptr<Sink>(new(std::nothrow)Sink(stage,document,cancellation));
  if(!sink) {output.result=Result::NoMemory;return false;}
  Request request;request.input=&input;request.path=path;request.stage=stage;
  request.timeoutMs=stage==Stage::NativeModels?25000:stage==Stage::Installed?12000:10000;
  const bool complete=!cancelled(cancellation)&&transport.get(request,*sink);
  output.result=sink->finish(complete);output.httpStatus=sink->status;
  output.retryAfterMs=output.result==Result::Transient?std::max<uint32_t>(2000,sink->retry):0;
  output.jsonPeakBytes=kFixedJsonBytes+budget.peak();
  if(document.overflowed())output.result=Result::NoMemory;
  return output.result==Result::Ok;
}
Result modelFailure(official_studio_models::Result result) {
  using M=official_studio_models::Result;
  if(result==M::OutOfMemory)return Result::NoMemory;
  if(result==M::NeedsConfiguration||result==M::NotAvailable||result==M::ProfileNotAvailable)return Result::Unavailable;
  return Result::Invalid;
}
bool installedAgent(const JsonDocument& document,const char* agent,bool& installed) {
  if(!document["tools"].is<JsonArrayConst>())return false;
  unsigned matches=0;installed=false;
  for(JsonVariantConst tool:document["tools"].as<JsonArrayConst>()) {
    if(!tool.is<JsonObjectConst>()||!official_studio_models::plainString(tool["id"])||!tool["installed"].is<bool>())return false;
    if(official_studio::jsonStringEquals(tool["id"],agent)){++matches;installed=tool["installed"].as<bool>();}
  }
  return matches==1;
}
} // namespace

Input::~Input(){wipe(userJwt.data(),userJwt.size());}
bool validInput(const Input& input) {
  const size_t base=boundedLength(input.baseUrl.data(),kBaseUrlBytes);
  const size_t jwt=boundedLength(input.userJwt.data(),kJwtBytes);
  const size_t profile=boundedLength(input.profile.data(),kProfileBytes);
  const size_t agent=boundedLength(input.agentId.data(),kAgentBytes);
  if(!base||base>kBaseUrlBytes||!jwt||jwt>kJwtBytes||!profile||profile>kProfileBytes||
      !agent||agent>kAgentBytes||!input.epoch||!official_studio::agentRoute(input.agentId.data()))return false;
  const char* authority=nullptr;
  if(!std::strncmp(input.baseUrl.data(),"http://",7))authority=input.baseUrl.data()+7;
  if(!std::strncmp(input.baseUrl.data(),"https://",8))authority=input.baseUrl.data()+8;
  if(!authority||!*authority||*authority=='/'||*authority==':')return false;
  for(const char* p=input.baseUrl.data();*p;++p)if(uint8_t(*p)<=0x20||*p=='\\'||*p=='@'||*p=='?'||*p=='#')return false;
  for(const char* p=input.userJwt.data();*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='.'||*p=='_'||*p=='-'))return false;
  if(!native_chat::validUtf8(input.profile.data(),profile))return false;
  for(const char* p=input.profile.data();*p;++p)if(uint8_t(*p)<0x20||*p==0x7f)return false;
  return !official_studio_models::blank(input.profile.data());
}
Result makeInput(Input& output,const char* base,const char* jwt,const char* profile,const char* agent,uint32_t epoch) {
  // Avoid a second ~5KiB Input on the 12KiB Arduino loop stack, and support
  // even cross-field aliases into output without writing before all copies.
  auto candidate=std::unique_ptr<Input>(new(std::nothrow)Input);
  if(!candidate){clearInput(output);return Result::NoMemory;}
  candidate->epoch=epoch;
  if(!copy(candidate->baseUrl,base)||!copy(candidate->userJwt,jwt)||!copy(candidate->profile,profile)||
      !copy(candidate->agentId,agent)||!validInput(*candidate)){clearInput(output);return Result::Invalid;}
  output=*candidate;return Result::Ok;
}
official_studio::NativeReadiness Output::readiness(const official_studio::Context& context)const {
  official_studio::NativeReadiness value;value.context=context;
  if(result!=Result::Ok||mode!=official_studio::Mode::Global||context.mode!=mode||context.connectionEpoch!=epoch||
      !official_studio::validContext(context)||!official_studio::equal(context.accountId,accountId.data())||
      !official_studio::equal(context.profile,profile.data())||!official_studio::equal(context.agentId,agentId.data()))
    value.result=official_studio::Result::StaleContext;
  else value.result=nativeResult;
  return value;
}

Output resolve(const Input& input,Transport& transport,const Cancellation* cancellation) {
  Output output;output.epoch=input.epoch;output.profile=input.profile;output.agentId=input.agentId;
  if(!validInput(input))return output;
  if(cancelled(cancellation)){output.result=Result::Cancelled;return output;}
  Budget budget(kJsonMemoryBytes-kFixedJsonBytes);JsonDocument document(&budget);
  if(!fetch(input,Stage::Account,transport,cancellation,budget,document,output))return output;
  if(!document["user"]["id"].is<uint64_t>()){output.result=Result::Invalid;return output;}
  const uint64_t account=document["user"]["id"].as<uint64_t>();
  if(!account||account>UINT64_C(9007199254740991)){output.result=Result::Invalid;return output;}
  std::snprintf(output.accountId.data(),output.accountId.size(),"%llu",static_cast<unsigned long long>(account));
  const auto& route=*official_studio::agentRoute(input.agentId.data());
  bool useNative=!route.scoped;
  if(route.scoped) {
    if(!fetch(input,Stage::Models,transport,cancellation,budget,document,output))return output;
    output.selection=official_studio_models::selectProfileDefault(document,input.profile.data(),route.id);
    if(output.selection.result!=official_studio_models::Result::Ok) {
      const auto failure=modelFailure(output.selection.result);
      useNative=route.global && (output.selection.result==official_studio_models::Result::NeedsConfiguration||
                               output.selection.result==official_studio_models::Result::NotAvailable);
      if(!useNative){output.result=failure;return output;}
    }
  }
  bool installed=true;
  if(official_studio::equal(route.source,"coding_agent")) {
    if(!fetch(input,Stage::Installed,transport,cancellation,budget,document,output))return output;
    if(!installedAgent(document,route.id,installed)){output.result=Result::Invalid;return output;}
    if(!installed){output.result=Result::Unavailable;output.nativeResult=official_studio::Result::NotInstalled;return output;}
  }
  if(useNative) {
    output.mode=official_studio::Mode::Global;
    if(!route.global){output.result=Result::Unavailable;return output;}
    if(!fetch(input,Stage::NativeModels,transport,cancellation,budget,document,output))return output;
    if(!document["agents"].is<JsonArrayConst>()){output.result=Result::Invalid;return output;}
    unsigned matches=0;
    for(JsonVariantConst entry:document["agents"].as<JsonArrayConst>()) {
      if(!entry.is<JsonObjectConst>()||!official_studio_models::plainString(entry["agentId"])||
          !official_studio_models::plainString(entry["status"])||!entry["models"].is<JsonArrayConst>()) {
        output.result=Result::Invalid;return output;
      }
      const char* status=entry["status"].as<const char*>();
      if(!official_studio::equal(status,"ready")&&!official_studio::equal(status,"empty")&&
          !official_studio::equal(status,"not_installed")&&!official_studio::equal(status,"auth_required")&&
          !official_studio::equal(status,"unsupported")&&!official_studio::equal(status,"timeout")&&
          !official_studio::equal(status,"error")) {output.result=Result::Invalid;return output;}
      for(JsonVariantConst model:entry["models"].as<JsonArrayConst>()) {
        if(!model.is<JsonObjectConst>()||!official_studio_models::plainString(model["id"])||
            official_studio_models::blank(model["id"].as<const char*>())||
            !official_studio_models::optionalFlag(model,"hidden")) {output.result=Result::Invalid;return output;}
      }
      if(official_studio::jsonStringEquals(entry["agentId"],route.id))++matches;
    }
    if(matches!=1){output.result=Result::Invalid;return output;}
    JsonDocument tools(&budget);tools["tools"][0]["id"]=input.agentId.data();tools["tools"][0]["installed"]=installed;
    if(tools.overflowed()){output.result=Result::NoMemory;return output;}
    official_studio::Context context;context.machineId="selection-validation";context.accountId=output.accountId.data();
    context.profile=input.profile.data();context.agentId=input.agentId.data();context.sessionId="selection-validation";
    context.mode=official_studio::Mode::Global;context.connectionEpoch=input.epoch;
    output.nativeResult=official_studio::inspectNativeReadiness(context,tools.as<JsonVariantConst>(),document.as<JsonVariantConst>()).result;
    output.jsonPeakBytes=kFixedJsonBytes+budget.peak();
    if(output.nativeResult!=official_studio::Result::Ok) {
      output.result=output.nativeResult==official_studio::Result::RetryableUnavailable?Result::Transient:
          output.nativeResult==official_studio::Result::InvalidPayload?Result::Invalid:Result::Unavailable;
      if(output.result==Result::Transient)output.retryAfterMs=5000;
      return output;
    }
  }
  output.result=cancelled(cancellation)?Result::Cancelled:Result::Ok;
  output.stage=Stage::Complete;return output;
}

#ifdef ARDUINO
namespace {
// Pinned Arduino HTTPClient's body loop has no timeout while available()==0.
// Gate the actual client, not only sink.write(), so a silent half-body peer
// cannot keep the task/owned credentials alive forever. No cross-task stop or
// extra watchdog task is involved. SDK DNS/connect/TLS each remain synchronous
// operations with their own timeout; a result arriving after deadline is denied.
template<typename Base> class DeadlineClient final:public Base {
 public:
  using Base::connect;
  using Base::read;
  DeadlineClient(uint32_t start,uint32_t timeout,const Cancellation* cancellation):
      start_(start),timeout_(timeout),cancellation_(cancellation){}
  int available()override{return alive()?std::max(0,Base::available()):0;}
  uint8_t connected()override{return alive()?Base::connected():0;}
  int read()override{return alive()?Base::read():-1;}
  int read(uint8_t* bytes,size_t count)override{return alive()?Base::read(bytes,count):-1;}
  int peek()override{return alive()?Base::peek():-1;}
  size_t write(uint8_t byte)override{return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count)override{return alive()?Base::write(bytes,count):0;}
  int connect(const char* host,uint16_t port,int32_t timeout)override {
    if(!alive())return 0;
    const uint32_t elapsed=uint32_t(millis()-start_);
    if(elapsed>=timeout_){alive();return 0;}
    const uint32_t left=timeout_-elapsed;
    const int result=Base::connect(host,port,std::min<int32_t>(timeout>0?timeout:int32_t(left),int32_t(left)));
    return alive()?result:0;
  }
 private:
  uint32_t start_,timeout_;const Cancellation* cancellation_;bool stopped_=false;
  bool alive() {
    if(stopped_)return false;
    if(cancelled(cancellation_)||uint32_t(millis()-start_)>=timeout_) {
      stopped_=true;Base::stop();static_cast<Stream&>(*this).setTimeout(0);return false;
    }
    return true;
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
// HTTPClient borrows the one protocol-selected client only until this helper
// returns. Plain LAN requests must not construct SDK TLS state at all.
bool getWithClient(WiFiClient& client,const String& url,uint32_t started,
                   const Request& request,ResponseSink& sink) {
    HTTPClient http;
    http.setReuse(false);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(request.timeoutMs);http.setTimeout(request.timeoutMs);
    if(!http.begin(client,url))return false;
    const char* names[]{"Content-Type","Retry-After"};http.collectHeaders(names,2);
    http.addHeader("Authorization",String("Bearer ")+request.input->userJwt.data());
    http.addHeader("Accept","application/json");http.addHeader("Accept-Encoding","identity");
    const int status=http.GET();const String type=http.header("Content-Type"),retry=http.header("Retry-After");
    Headers headers;headers.status=status;headers.contentType=type.c_str();headers.contentLength=http.getSize();
    uint32_t seconds=0;bool valid=retry.length()>0;
    for(size_t n=0;n<retry.length();++n) {
      if(retry[n]<'0'||retry[n]>'9'){valid=false;break;}
      seconds=std::min<uint32_t>(30,seconds*10+uint32_t(retry[n]-'0'));
    }
    if(valid)headers.retryAfterMs=seconds*1000;
    if(!sink.begin(headers)){http.end();return false;}
    SinkStream stream(sink,started,request.timeoutMs);
    const int count=http.writeToStream(&stream);http.end();
    return count>=0&&!stream.failed()&&uint32_t(millis()-started)<request.timeoutMs;
}
class ArduinoTransport final:public Transport {
 public:
  explicit ArduinoTransport(const Cancellation* cancellation):cancellation_(cancellation){}
  bool get(const Request& request,ResponseSink& sink)override {
    const uint32_t started=millis();
    String base(request.input->baseUrl.data());while(base.endsWith("/"))base.remove(base.length()-1);
    const String url=base+request.path;
    if(url.length()!=base.length()+std::strlen(request.path))return false;
    if(base.startsWith("https://")) {
      DeadlineClient<WiFiClientSecure> secure(started,request.timeoutMs,cancellation_);
      native_tls::configureTrust(secure);secure.setHandshakeTimeout((request.timeoutMs+999)/1000);
      return getWithClient(secure,url,started,request,sink);
    }
    DeadlineClient<WiFiClient> plain(started,request.timeoutMs,cancellation_);
    return getWithClient(plain,url,started,request,sink);
  }
 private:
  const Cancellation* cancellation_;
};
} // namespace
#endif

struct Job::State final:Cancellation {
  Input input;Output output;
  std::unique_ptr<Transport> transport;
  std::atomic<unsigned> references{2};
  std::atomic<bool> cancelledFlag{false},done{false};
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
  if(xTaskCreate(worker,"studio-select",kTaskStackBytes,state,1,nullptr)!=pdPASS) {
    state_=nullptr;state->release();state->release();return StartResult::NoMemory;
  }
#else
  // Detached at creation: unlike std::thread(...).detach(), a detach failure
  // cannot leave a live worker while a failed start frees its shared state.
  pthread_attr_t attributes;
  int error=pthread_attr_init(&attributes);
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
  output=state_->output;
  if(state_->cancelled()){output.result=Result::Cancelled;output.retryAfterMs=0;}
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
  } // HTTP/JSON resources are gone before the result becomes visible.
  state->done.store(true,std::memory_order_release);
  state->release(); // Worker and owner each hold a reference; destruction may detach safely.
#ifdef ARDUINO
  vTaskDelete(nullptr);
#endif
}
} // namespace official_selection
