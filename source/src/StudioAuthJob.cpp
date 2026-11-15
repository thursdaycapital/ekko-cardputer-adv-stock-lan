#include "StudioAuthJob.h"
#include "BoundedJsonReader.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>
#ifndef ARDUINO
#include <thread>
#else
#include "NativeTlsTrust.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace studio_auth {
namespace {
void wipe(void* memory,size_t bytes){auto* next=static_cast<volatile uint8_t*>(memory);while(bytes--)*next++=0;}
size_t length(const char* text,size_t maximum){if(!text)return maximum+1;size_t n=0;while(n<=maximum&&text[n])++n;return n;}
template<size_t N>bool copy(std::array<char,N>& out,const char* text){const size_t n=length(text,N-1);if(n>=N)return false;std::memcpy(out.data(),text,n+1);return true;}
uint32_t nowMs(){
#ifdef ARDUINO
  return millis();
#else
  return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
bool origin(const char* url,bool& tls,uint16_t& port) {
  if(!url)return false;
  const char* authority=nullptr;
  if(!std::strncmp(url,"http://",7)){authority=url+7;tls=false;port=80;}
  else if(!std::strncmp(url,"https://",8)){authority=url+8;tls=true;port=443;}
  else return false;
  const char* at=authority;bool bracket=false;
  if(*at=='['){bracket=true;++at;}
  const char* host=at;
  while(*at&&*at!='/'&&*at!='?'&&*at!='#'&&*at!='@'&&(bracket?*at!=']':*at!=':')) {
    const char c=*at++;
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'||(bracket&&c==':')))return false;
  }
  if(at==host||size_t(at-host)>253)return false;
  if(bracket){if(*at!=']')return false;++at;}
  if(*at==':') {
    ++at;const char* first=at;uint32_t number=0;
    while(*at>='0'&&*at<='9'){number=number*10+uint32_t(*at++-'0');if(number>65535)return false;}
    if(at==first||!number)return false;port=uint16_t(number);
  }
  return !*at||(*at=='/'&&!at[1]);
}
bool binding(const char* key) {
  const char* first=std::strchr(key,'|');if(!first||first==key)return false;
  const char* second=std::strchr(first+1,'|');if(!second||second==first+1||std::strchr(second+1,'|'))return false;
  uint32_t port=0;for(const char* n=second+1;*n;++n){if(*n<'0'||*n>'9')return false;port=port*10+uint32_t(*n-'0');if(port>65535)return false;}
  return port>0;
}
bool cancelled(const Cancellation* token){return token&&token->cancelled();}
bool jsonContent(const char* type) {
  if(!type)return false;
  constexpr char expected[]="application/json";
  for(size_t n=0;n<sizeof(expected)-1;++n){char c=type[n];if(!c)return false;if(c>='A'&&c<='Z')c=char(c+'a'-'A');if(c!=expected[n])return false;}
  const char* tail=type+sizeof(expected)-1;while(*tail==' '||*tail=='\t')++tail;return !*tail||*tail==';';
}
enum class Field:uint8_t {Skip,Id,Kind,PublicKey,Port,Token};
class Sink final:public ResponseSink,public bounded_json::Listener {
 public:
  Sink(Stage stage,Output& out):stage_(stage),out_(out),reader_(*this){}
  Error error=Error::InvalidResponse;bool began=false;size_t received=0,expected=0;
  std::array<char,257> id{};std::array<char,64> kind{};uint16_t port=0;
  bool begin(const Headers& headers)override {
    if(began)return false;began=true;out_.httpStatus=headers.status;
    if(headers.status==401||headers.status==403){error=Error::Rejected;return false;}
    if(headers.status<200||headers.status>=300){error=Error::Transient;return false;}
    if(headers.contentLength<=0||headers.contentLength>int64_t(kResponseBytes)) {
      error=headers.contentLength>int64_t(kResponseBytes)?Error::ResponseTooLarge:Error::InvalidResponse;return false;
    }
    if(!jsonContent(headers.contentType)||(headers.contentEncoding&&*headers.contentEncoding)||
        (headers.transferEncoding&&*headers.transferEncoding))return false;
    expected=size_t(headers.contentLength);return true;
  }
  bool write(const uint8_t* bytes,size_t count)override {
    if(!began||!expected||!bytes||count>expected-received)return false;
    received+=count;return reader_.feed(bytes,count);
  }
  bool finish() {
    if(!began||received!=expected||!reader_.finish()||!root_)return false;
    if(stage_==Stage::Identity)return seen_[1]&&seen_[2]&&seen_[3]&&seen_[4]&&id[0]&&kind[0]&&port&&publicBytes_;
    return seen_[5]&&out_.jwt[0];
  }
  bool onEvent(const bounded_json::Event& event)override {
    using bounded_json::Type;
    if(!event.path.size) {
      if(event.type==Type::ObjectBegin){root_=true;return true;}
      return event.type==Type::ObjectEnd;
    }
    const Field field=wanted(event.path);if(field==Field::Skip)return true;
    if(event.type==Type::Key){if(seen_[unsigned(field)])return false;seen_[unsigned(field)]=true;return true;}
    if(event.type==Type::StringBegin||event.type==Type::NumberBegin) {
      if((field==Field::Port)!=(event.type==Type::NumberBegin))return false;
      used_=0;number_=0;return true;
    }
    if(event.type==Type::StringChunk||event.type==Type::NumberChunk) {
      if(std::memchr(event.data,0,event.size))return false;
      if(field==Field::Port) {
        for(size_t n=0;n<event.size;++n){const char c=event.data[n];if(c<'0'||c>'9')return false;number_=number_*10+uint32_t(c-'0');if(number_>65535)return false;}
        used_+=event.size;return used_<=5;
      }
      const size_t cap=field==Field::Id?256:field==Field::Kind?63:field==Field::Token?kJwtBytes:4096;
      if(event.size>cap-used_)return false;
      for(size_t n=0;n<event.size;++n)if(uint8_t(event.data[n])<0x20||uint8_t(event.data[n])==0x7f||
          ((field==Field::Id||field==Field::Kind)&&event.data[n]=='|'))return false;
      if(field!=Field::PublicKey) {
        char* target=field==Field::Id?id.data():field==Field::Kind?kind.data():out_.jwt.data();
        std::memcpy(target+used_,event.data,event.size);target[used_+event.size]=0;
      }
      used_+=event.size;return true;
    }
    if(event.type==Type::StringEnd){if(!used_)return false;if(field==Field::PublicKey)publicBytes_=used_;return true;}
    if(event.type==Type::NumberEnd){if(!used_||!number_)return false;port=uint16_t(number_);return true;}
    return false;
  }
 private:
  Stage stage_;Output& out_;bounded_json::Reader<16,128,64> reader_;
  bool root_=false;std::array<bool,6> seen_{};size_t used_=0,publicBytes_=0;uint32_t number_=0;
  Field wanted(bounded_json::PathView path)const {
    if(path.size!=1||path.segments[0].kind!=bounded_json::SegmentKind::Key)return Field::Skip;
    const auto& key=path.segments[0];
    auto same=[&](const char* value){return std::strlen(value)==key.keyBytes&&!std::memcmp(key.key,value,key.keyBytes);};
    if(stage_==Stage::Login)return same("token")?Field::Token:Field::Skip;
    if(same("device_id"))return Field::Id;if(same("endpoint_kind"))return Field::Kind;
    if(same("device_public_key"))return Field::PublicKey;if(same("http_port"))return Field::Port;
    return Field::Skip;
  }
};
struct LoginBody {
  std::array<char,kResponseBytes+1> bytes{};size_t count=0;
  ~LoginBody(){wipe(bytes.data(),bytes.size());}
  bool literal(const char* text){const size_t n=std::strlen(text);if(n>kResponseBytes-count)return false;std::memcpy(bytes.data()+count,text,n);count+=n;return true;}
  bool quoted(const char* text) {
    if(!literal("\""))return false;
    for(const auto* at=reinterpret_cast<const uint8_t*>(text);*at;++at) {
      if(*at=='"'){if(!literal("\\\""))return false;}
      else if(*at=='\\'){if(!literal("\\\\"))return false;}
      else if(*at<0x20){char escaped[7];std::snprintf(escaped,sizeof(escaped),"\\u%04x",*at);if(!literal(escaped))return false;}
      else {if(count==kResponseBytes)return false;bytes[count++]=char(*at);}
    }
    return literal("\"");
  }
};
}
Input::~Input(){wipe(account.data(),account.size());wipe(password.data(),password.size());}
Output::~Output(){clearSecret();}
void Output::clearSecret(){wipe(jwt.data(),jwt.size());}
Output::Output(Output&& other)noexcept{*this=std::move(other);}
Output& Output::operator=(Output&& other)noexcept {
  if(this==&other)return *this;
  clearSecret();error=other.error;stage=other.stage;epoch=other.epoch;elapsedMs=other.elapsedMs;
  workerStackFreeBytes=other.workerStackFreeBytes;httpStatus=other.httpStatus;jwt=other.jwt;
  other.clearSecret();return *this;
}
bool validInput(const Input& input) {
  bool tls=false;uint16_t port=0;
  const bool bounded=length(input.baseUrl.data(),kBaseUrlBytes)<=kBaseUrlBytes&&origin(input.baseUrl.data(),tls,port)&&
    length(input.machineKey.data(),kMachineKeyBytes)<=kMachineKeyBytes&&binding(input.machineKey.data())&&
    length(input.profile.data(),kProfileBytes)>0&&length(input.profile.data(),kProfileBytes)<=kProfileBytes&&
    length(input.instance.data(),kInstanceBytes)>0&&length(input.instance.data(),kInstanceBytes)<=kInstanceBytes&&
    length(input.account.data(),kAccountBytes)>0&&length(input.account.data(),kAccountBytes)<=kAccountBytes&&
    length(input.password.data(),kPasswordBytes)>0&&length(input.password.data(),kPasswordBytes)<=kPasswordBytes;
  if(!bounded)return false;
  // This value becomes a header; reject rather than escaping or truncating it.
  for(const char* p=input.instance.data();*p;++p)if(uint8_t(*p)<=0x20||uint8_t(*p)>=0x7f)return false;
  for(const char* p=input.machineKey.data();*p;++p)if(uint8_t(*p)<0x20||uint8_t(*p)==0x7f)return false;
  for(const char* p=input.profile.data();*p;++p)if(uint8_t(*p)<0x20||uint8_t(*p)==0x7f)return false;
  return true;
}
bool makeInput(Input& output,const char* baseUrl,const char* machineKey,const char* profile,
               const char* instance,const char* account,const char* password,uint32_t epoch) {
  Input candidate;candidate.epoch=epoch;
  if(!copy(candidate.baseUrl,baseUrl)||!copy(candidate.machineKey,machineKey)||!copy(candidate.profile,profile)||
      !copy(candidate.instance,instance)||!copy(candidate.account,account)||!copy(candidate.password,password)||!validInput(candidate))return false;
  size_t n=std::strlen(candidate.baseUrl.data());if(n&&candidate.baseUrl[n-1]=='/')candidate.baseUrl[n-1]=0;
  output=candidate;return true;
}
uint32_t taskStackBytes(const Input& input){return !std::strncmp(input.baseUrl.data(),"https://",8)?kTlsStackBytes:kPlainStackBytes;}
Output resolve(const Input& input,Transport& transport,const Cancellation* cancellation) {
  Output output;output.epoch=input.epoch;const uint32_t started=nowMs();
  auto finish=[&](Error error){output.error=cancelled(cancellation)?Error::Cancelled:error;output.elapsedMs=nowMs()-started;
    if(output.error!=Error::Ok)output.clearSecret();return std::move(output);};
  if(!validInput(input))return finish(Error::InvalidInput);
  if(cancelled(cancellation))return finish(Error::Cancelled);
  output.stage=Stage::Identity;
  auto identity=std::unique_ptr<Sink>(new(std::nothrow)Sink(Stage::Identity,output));
  if(!identity)return finish(Error::NoMemory);
  Request probe{Method::Get,input.baseUrl.data(),"/api/devices/link-info",nullptr,0,nullptr,kIdentityDeadlineMs};
  const bool read=transport.exchange(probe,*identity,cancellation);
  if(cancelled(cancellation))return finish(Error::Cancelled);
  if(nowMs()-started>=kIdentityDeadlineMs)return finish(Error::Transient);
  if(!read||!identity->finish())return finish(identity->error);
  bool tls=false;uint16_t expectedPort=0;if(!origin(input.baseUrl.data(),tls,expectedPort))return finish(Error::InvalidInput);
  char observed[kMachineKeyBytes+1];const int count=std::snprintf(observed,sizeof(observed),"%s|%s|%u",identity->id.data(),identity->kind.data(),identity->port);
  if(count<=0||size_t(count)>=sizeof(observed)||identity->port!=expectedPort||std::strcmp(observed,input.machineKey.data()))
    return finish(Error::BindingChanged);
  identity.reset();
  auto body=std::unique_ptr<LoginBody>(new(std::nothrow)LoginBody);
  if(!body)return finish(Error::NoMemory);
  if(!body->literal("{\"token\":")||!body->quoted(input.instance.data())||!body->literal(",\"id\":")||
      !body->quoted(input.instance.data())||!body->literal(",\"account\":")||!body->quoted(input.account.data())||
      !body->literal(",\"password\":")||!body->quoted(input.password.data())||!body->literal("}"))return finish(Error::InvalidInput);
  output.stage=Stage::Login;output.httpStatus=0;
  auto login=std::unique_ptr<Sink>(new(std::nothrow)Sink(Stage::Login,output));if(!login)return finish(Error::NoMemory);
  if(cancelled(cancellation))return finish(Error::Cancelled);
  if(nowMs()-started>=kDeadlineMs)return finish(Error::Transient);
  Request request{Method::Post,input.baseUrl.data(),"/api/auth/mcu-login",body->bytes.data(),body->count,input.instance.data(),std::min<uint32_t>(8000,kDeadlineMs-(nowMs()-started))};
  const uint32_t loginStarted=nowMs();
  const bool logged=transport.exchange(request,*login,cancellation);
  if(cancelled(cancellation))return finish(Error::Cancelled);
  if(nowMs()-started>=kDeadlineMs||nowMs()-loginStarted>=request.timeoutMs)return finish(Error::Transient);
  if(!logged||!login->finish())return finish(login->error);
  output.stage=Stage::Complete;return finish(Error::Ok);
}

namespace {
#ifdef ARDUINO
template<typename Base>class DeadlineClient final:public Base {
 public:
  using Base::connect;using Base::read;
  DeadlineClient(uint32_t started,uint32_t timeout,const Cancellation* cancel)
    :started_(started),timeout_(timeout),cancel_(cancel){}
  int available()override{return alive()?std::max(0,Base::available()):0;}
  uint8_t connected()override{return alive()?Base::connected():0;}
  int read()override{return alive()?Base::read():-1;}
  int read(uint8_t* bytes,size_t count)override{return alive()?Base::read(bytes,count):-1;}
  int peek()override{return alive()?Base::peek():-1;}
  size_t write(uint8_t byte)override{return write(&byte,1);}
  size_t write(const uint8_t* bytes,size_t count)override{return alive()?Base::write(bytes,count):0;}
  int connect(const char* host,uint16_t port,int32_t timeout)override {
    if(!alive())return 0;
    const uint32_t left=timeout_-uint32_t(millis()-started_);
    const int result=Base::connect(host,port,std::min<int32_t>(timeout>0?timeout:int32_t(left),int32_t(left)));
    return alive()?result:0;
  }
 private:
  uint32_t started_,timeout_;const Cancellation* cancel_;bool stopped_=false;
  bool alive() {
    if(stopped_)return false;
    if(cancelled(cancel_)||uint32_t(millis()-started_)>=timeout_) {
      stopped_=true;Base::stop();static_cast<Stream&>(*this).setTimeout(0);return false;
    }
    return true;
  }
};
class ArduinoTransport final:public Transport {
 public:
  bool exchange(const Request& request,ResponseSink& sink,const Cancellation* cancel)override {
    if(cancelled(cancel)||!request.timeoutMs)return false;
    const uint32_t started=millis();HTTPClient http;http.setReuse(false);http.useHTTP10(true);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(std::min<uint32_t>(request.timeoutMs,8000));http.setTimeout(std::min<uint32_t>(request.timeoutMs,8000));
    std::unique_ptr<WiFiClient> client;
    if(!std::strncmp(request.baseUrl,"https://",8)) {
      auto* secure=new(std::nothrow)DeadlineClient<WiFiClientSecure>(started,request.timeoutMs,cancel);if(!secure)return false;
      native_tls::configureTrust(*secure);secure->setHandshakeTimeout((std::min<uint32_t>(request.timeoutMs,8000)+999)/1000);client.reset(secure);
    } else client.reset(new(std::nothrow)DeadlineClient<WiFiClient>(started,request.timeoutMs,cancel));
    if(!client)return false;
    const String url=String(request.baseUrl)+request.path;
    if(!http.begin(*client,url))return false;
    const char* names[]{"Content-Type","Content-Encoding","Transfer-Encoding"};http.collectHeaders(names,3);
    int code=0;
    if(request.method==Method::Get)code=http.GET();
    else {
      http.addHeader("Content-Type","application/json");http.addHeader("X-Hermes-Device-Id",request.deviceInstance);
      http.addHeader("X-Hermes-Device-Name","Ekko Studio Cardputer ADV");
      code=http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(request.body)),request.bodyBytes);
    }
    const String type=http.header("Content-Type"),encoding=http.header("Content-Encoding"),transfer=http.header("Transfer-Encoding");
    const int count=http.getSize();
    bool ok=millis()-started<request.timeoutMs&&!cancelled(cancel)&&sink.begin({code,count,type.c_str(),encoding.c_str(),transfer.c_str()});
    size_t read=0;auto* stream=http.getStreamPtr();
    while(ok&&read<size_t(count)) {
      if(cancelled(cancel)||millis()-started>=request.timeoutMs){ok=false;break;}
      const int available=stream->available();
      if(available>0) {
        uint8_t bytes[256];const size_t wanted=std::min<size_t>(sizeof(bytes),std::min<size_t>(available,size_t(count)-read));
        const int n=stream->read(bytes,wanted);if(n<=0||!sink.write(bytes,size_t(n))){ok=false;break;}read+=size_t(n);
      } else {if(!stream->connected()){ok=false;break;}delay(1);}
    }
    http.end();return ok&&read==size_t(count)&&!cancelled(cancel)&&millis()-started<request.timeoutMs;
  }
};
#endif
}
struct Job::Shared final:public Cancellation {
  Input input;Output output;std::unique_ptr<Transport> transport;
  std::atomic<bool> cancellation{false},done{false};std::atomic<uint32_t> refs{2};
#ifndef ARDUINO
  std::thread thread;
  ~Shared(){if(thread.joinable()){if(thread.get_id()==std::this_thread::get_id())thread.detach();else thread.join();}}
#endif
  bool cancelled()const override{return cancellation.load(std::memory_order_relaxed);}
  void release(){if(refs.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;}
};
size_t requiredHeapBytes(const Input& input){return sizeof(Job::Shared)+taskStackBytes(input)+24*1024;}
Job::~Job(){if(state_){state_->cancellation.store(true,std::memory_order_relaxed);state_->release();}}
StartResult Job::start(const Input& input,std::unique_ptr<Transport> transport) {
  if(state_)return StartResult::Busy;if(!validInput(input))return StartResult::Invalid;
#ifndef ARDUINO
  if(!transport)return StartResult::NotSupported;
#else
  constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  if(heap_caps_get_free_size(caps)<requiredHeapBytes(input)||heap_caps_get_largest_free_block(caps)<std::max<size_t>(sizeof(Shared),taskStackBytes(input))+512)
    return StartResult::NoMemory;
  if(!transport)transport.reset(new(std::nothrow)ArduinoTransport);
  if(!transport)return StartResult::NoMemory;
#endif
  Shared* shared=nullptr;
#ifndef ARDUINO
  try {shared=new(std::nothrow)Shared;}catch(const std::bad_alloc&){return StartResult::NoMemory;}
#else
  shared=new(std::nothrow)Shared;
#endif
  if(!shared)return StartResult::NoMemory;
  shared->input=input;shared->transport=std::move(transport);state_=shared;
#ifdef ARDUINO
  if(xTaskCreate(worker,"studio-auth",taskStackBytes(input),shared,1,nullptr)!=pdPASS) {
    state_=nullptr;shared->release();shared->release();return StartResult::NoMemory;
  }
#else
  try{shared->thread=std::thread(&Job::worker,shared);}catch(...){state_=nullptr;shared->release();shared->release();return StartResult::NoMemory;}
#endif
  return StartResult::Started;
}
Poll Job::poll(Output& output) {
  if(!state_)return Poll::Idle;if(!state_->done.load(std::memory_order_acquire))return Poll::Pending;
  Shared* finished=state_;state_=nullptr;output=std::move(finished->output);
  if(finished->cancelled()){output.error=Error::Cancelled;output.clearSecret();}
  finished->release();return Poll::Ready;
}
void Job::cancel(){if(state_)state_->cancellation.store(true,std::memory_order_relaxed);}
bool Job::busy()const{return state_!=nullptr;}
void Job::worker(void* parameter) {
  auto* state=static_cast<Shared*>(parameter);state->output=resolve(state->input,*state->transport,state);
#ifdef ARDUINO
  state->output.workerStackFreeBytes=uxTaskGetStackHighWaterMark(nullptr);
#endif
  state->done.store(true,std::memory_order_release);state->release();
#ifdef ARDUINO
  vTaskDelete(nullptr);
#endif
}
} // namespace studio_auth
