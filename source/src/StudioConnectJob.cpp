#include "StudioConnectJob.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#ifdef ARDUINO
#include "NativeTlsTrust.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>
#include <lwip/sockets.h>
#endif

namespace studio_connect {
namespace {
size_t boundedLength(const char* text,size_t maximum) {
  if(!text)return maximum+1;
  size_t n=0;while(n<=maximum&&text[n])++n;return n;
}
template<size_t N> bool copy(std::array<char,N>& out,const char* text) {
  const size_t bytes=boundedLength(text,N-1);if(bytes>=N)return false;
  std::memcpy(out.data(),text,bytes+1);return true;
}
}
bool makeInput(Input& output,const char* host,uint16_t port,bool tls,const char* path,uint32_t epoch) {
  Input candidate;candidate.port=port;candidate.tls=tls;candidate.epoch=epoch;
  if(!copy(candidate.host,host)||!copy(candidate.path,path)||!validInput(candidate))return false;
  output=candidate;return true;
}
bool validInput(const Input& input) {
  const size_t host=boundedLength(input.host.data(),kHostBytes),path=boundedLength(input.path.data(),kPathBytes);
  if(!host||host>kHostBytes||!path||path>kPathBytes||!input.port||input.path[0]!='/')return false;
  for(size_t n=0;n<host;++n) {
    const char c=input.host[n];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c=='_'||c==':'))return false;
  }
  for(size_t n=0;n<path;++n)if(uint8_t(input.path[n])<=0x20||uint8_t(input.path[n])>=0x7f||input.path[n]=='#')return false;
  return true;
}
uint32_t taskStackBytes(const Input& input){return input.tls?kTlsStackBytes:kPlainStackBytes;}

struct Job::Shared {
  Input input;Output output;
  std::atomic<bool> cancelled{false},done{false};
  std::atomic<uint32_t> references{2};
  void release(){if(references.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;}
#ifdef ARDUINO
  void sample() {
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    const uint32_t free=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
    if(!output.resourcesAvailable) {output.heapMinimum=free;output.largestBlockMinimum=largest;}
    else {output.heapMinimum=std::min(output.heapMinimum,free);output.largestBlockMinimum=std::min(output.largestBlockMinimum,largest);}
    output.resourcesAvailable=true;
  }
#endif
};
size_t requiredHeapBytes(const Input& input){return sizeof(Job::Shared)+taskStackBytes(input)+(input.tls?32:12)*1024;}
Job::~Job(){if(state_){state_->cancelled.store(true,std::memory_order_relaxed);state_->release();}}
StartResult Job::start(const Input& input) {
  if(state_)return StartResult::Busy;
  if(!validInput(input))return StartResult::Invalid;
#ifndef ARDUINO
  return StartResult::NotSupported;
#else
  constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  if(heap_caps_get_free_size(caps)<requiredHeapBytes(input)||
      heap_caps_get_largest_free_block(caps)<std::max<size_t>(sizeof(Shared),taskStackBytes(input))+512)
    return StartResult::NoMemory;
  Shared* shared=new(std::nothrow)Shared;
  if(!shared)return StartResult::NoMemory;
  shared->input=input;shared->output.epoch=input.epoch;state_=shared;
  if(xTaskCreate(worker,"studio-connect",taskStackBytes(input),shared,1,nullptr)!=pdPASS) {
    state_=nullptr;shared->release();shared->release();return StartResult::NoMemory;
  }
  return StartResult::Started;
#endif
}
Poll Job::poll(Output& output) {
  if(!state_)return Poll::Idle;
  if(!state_->done.load(std::memory_order_acquire))return Poll::Pending;
  Shared* finished=state_;state_=nullptr;
  output=std::move(finished->output);
  if(finished->cancelled.load(std::memory_order_relaxed)) {
#ifdef ARDUINO
    if(output.client)output.client->stop();
    output.client.reset();
#endif
    output.error=Error::Cancelled;
  }
  finished->release();return Poll::Ready;
}
void Job::cancel(){if(state_)state_->cancelled.store(true,std::memory_order_relaxed);}
bool Job::busy()const{return state_!=nullptr;}

void Job::worker(void* parameter) {
#ifdef ARDUINO
  auto* shared=static_cast<Shared*>(parameter);
  const Input& input=shared->input;Output& output=shared->output;
  const uint32_t started=millis(),deadline=input.tls?20000:8000;
  auto alive=[&](){return !shared->cancelled.load(std::memory_order_relaxed)&&millis()-started<deadline;};
  auto run=[&]()->Error {
    shared->sample();
    if(!alive())return Error::Cancelled;
    std::unique_ptr<WiFiClient> client;
    if(input.tls) {
      auto* secure=new(std::nothrow)WiFiClientSecure;
      if(!secure)return Error::NoMemory;
      client.reset(secure);native_tls::configureTrust(*secure);secure->setHandshakeTimeout(15);
    } else client.reset(new(std::nothrow)WiFiClient);
    if(!client)return Error::NoMemory;
    if(!client->connect(input.host.data(),input.port,5000))return alive()?Error::ConnectFailed:Error::Deadline;
    shared->sample();
    if(!alive())return Error::Deadline;
    client->setTimeout(1);client->setNoDelay(true);
    uint8_t random[16];esp_fill_random(random,sizeof(random));
    unsigned char key[25]{};size_t keyBytes=0;
    if(mbedtls_base64_encode(key,sizeof(key),&keyBytes,random,sizeof(random))||keyBytes!=24)return Error::InvalidInput;
    char acceptSource[61]{};
    std::memcpy(acceptSource,key,24);
    std::memcpy(acceptSource+24,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11",36);
    uint8_t digest[20]{};unsigned char expected[29]{};size_t acceptBytes=0;
    if(mbedtls_sha1_ret(reinterpret_cast<const uint8_t*>(acceptSource),60,digest)||
        mbedtls_base64_encode(expected,sizeof(expected),&acceptBytes,digest,sizeof(digest))||acceptBytes!=28)
      return Error::InvalidInput;
    char request[kPathBytes+kHostBytes+256];
    const bool ipv6=std::strchr(input.host.data(),':')!=nullptr;
    const int requestBytes=std::snprintf(request,sizeof(request),
      "GET %s HTTP/1.1\r\nHost: %s%s%s:%u\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
      "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: %s\r\nUser-Agent: EkkoStudio-CardputerADV\r\n\r\n",
      input.path.data(),ipv6?"[":"",input.host.data(),ipv6?"]":"",input.port,key);
    if(requestBytes<=0||size_t(requestBytes)>=sizeof(request))return Error::InvalidInput;
    size_t written=0;
    while(written<size_t(requestBytes)&&alive()) {
      if(!client->connected())return Error::Closed;
      size_t count=0;
      if(input.tls) count=client->write(reinterpret_cast<const uint8_t*>(request)+written,size_t(requestBytes)-written);
      else {
        // Pinned WiFiClient::write retries select(1s) ten times and resets its
        // retry counter on progress. A slow peer can outlive any outer deadline.
        // Only plain TCP may use send directly; TLS must retain encryption.
        const int n=send(client->fd(),request+written,size_t(requestBytes)-written,MSG_DONTWAIT);
        if(n>0)count=size_t(n);
        else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)return Error::WriteFailed;
      }
      if(count>size_t(requestBytes)-written)return Error::WriteFailed;
      written+=count;if(!count)delay(1);
    }
    if(written!=size_t(requestBytes))return Error::Deadline;
    studio_handshake::Parser parser(reinterpret_cast<const char*>(expected),deadline);
    while(alive()&&parser.state()==studio_handshake::State::Reading) {
      if(client->available()>0) {
        const int byte=client->read();
        if(byte<0)continue;
        if(!parser.feed(uint8_t(byte),millis()-started))break;
      } else {
        if(!client->connected())return Error::Closed;
        delay(1);
      }
    }
    output.httpStatus=parser.status();output.handshakeError=parser.error();shared->sample();
    if(!alive())return Error::Deadline;
    if(parser.state()!=studio_handshake::State::Complete)return Error::HandshakeFailed;
    output.client=std::move(client);return Error::Ok;
  };
  output.error=run();
  if(shared->cancelled.load(std::memory_order_relaxed))output.error=Error::Cancelled;
  if(output.error!=Error::Ok&&output.client){output.client->stop();output.client.reset();}
  output.elapsedMs=millis()-started;shared->sample();output.workerStackFreeBytes=uxTaskGetStackHighWaterMark(nullptr);
  shared->done.store(true,std::memory_order_release);shared->release();vTaskDelete(nullptr);
#else
  (void)parameter;
#endif
}
} // namespace studio_connect
