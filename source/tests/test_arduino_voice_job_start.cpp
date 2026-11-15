// Compile the real ARDUINO Job implementation. Only heap accounting and the
// FreeRTOS scheduler are doubled; the worker uses the real streaming STT code.
#include "OfficialVoiceJob.h"
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace fake {
// Explicit free holes, separated by an occupied region. The first-fit model
// intentionally makes a state allocation split the only stack-sized hole.
// These are a deterministic allocation scenario, not measured ESP-IDF sizes.
struct Block {size_t bytes=0;bool free=false;void* pointer=nullptr;};
Block blocks[128];size_t count=0,live=0,lowWater=0;
bool model=false,failNextState=false,failMutex=false,failSignal=false,failTask=false;
size_t taskStackRequested=0,largestBeforeTask=0;unsigned taskCreates=0,semaphores=0;
TaskFunction_t pending=nullptr;void* parameter=nullptr;void* taskStack=nullptr;void* taskControl=nullptr;
bool taskDeleted=false;
size_t freeBytes(){size_t n=0;for(size_t i=0;i<count;++i)if(blocks[i].free)n+=blocks[i].bytes;return n;}
size_t largest(){size_t n=0;for(size_t i=0;i<count;++i)if(blocks[i].free)n=std::max(n,blocks[i].bytes);return n;}
void reset(size_t first=36852,size_t second=24004) {
  assert(!live&&!semaphores&&!pending&&!taskStack&&!taskControl);
  count=3;blocks[0]={first,true,nullptr};blocks[1]={64,false,nullptr};blocks[2]={second,true,nullptr};
  lowWater=first+second;model=true;failNextState=failMutex=failSignal=failTask=false;
  taskStackRequested=largestBeforeTask=0;taskCreates=0;taskDeleted=false;
}
void* allocate(size_t bytes) {
  if(!model)return std::malloc(bytes?bytes:1);
  const size_t aligned=(bytes+15)&~size_t(15);
  for(size_t i=0;i<count;++i)if(blocks[i].free&&blocks[i].bytes>=aligned) {
    void* pointer=std::malloc(bytes?bytes:1);if(!pointer)return nullptr;
    const size_t remaining=blocks[i].bytes-aligned;
    if(remaining){assert(count<128);for(size_t at=count;at>i+1;--at)blocks[at]=blocks[at-1];blocks[i+1]={remaining,true,nullptr};++count;}
    blocks[i]={aligned,false,pointer};++live;lowWater=std::min(lowWater,freeBytes());return pointer;
  }
  return nullptr;
}
bool release(void* pointer) {
  if(!pointer)return true;
  for(size_t i=0;i<count;++i)if(blocks[i].pointer==pointer) {
    assert(!blocks[i].free&&live);blocks[i].free=true;blocks[i].pointer=nullptr;--live;
    for(size_t at=0;at+1<count;) {
      if(blocks[at].free&&blocks[at+1].free){blocks[at].bytes+=blocks[at+1].bytes;for(size_t n=at+1;n+1<count;++n)blocks[n]=blocks[n+1];--count;}
      else ++at;
    }
    std::free(pointer);return true;
  }
  return false;
}
void runWorker() {
  assert(pending&&taskStack&&taskControl);
  const auto run=pending;void* arg=parameter;pending=nullptr;parameter=nullptr;
  run(arg);assert(taskDeleted);
  assert(release(taskControl)&&release(taskStack));taskControl=taskStack=nullptr;
}
void clean(){assert(!live&&!semaphores&&!pending&&!taskStack&&!taskControl);}
}

// Only nothrow allocations join the modeled internal heap. The real Job and
// voice decoder use this allocation form; the test's fixtures stay external.
void* operator new(size_t bytes,const std::nothrow_t&)noexcept {
  if(fake::failNextState){fake::failNextState=false;return nullptr;}
  return fake::allocate(bytes);
}
void* operator new[](size_t bytes,const std::nothrow_t&)noexcept{return fake::allocate(bytes);}
void* operator new(size_t bytes){if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{if(!fake::release(p))std::free(p);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{::operator delete(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,size_t)noexcept{::operator delete(p);}

struct FakeSemaphore {bool available;};
SemaphoreHandle_t semaphore(bool available,bool fail) {
  if(fail)return nullptr;
  void* p=fake::allocate(96);if(!p)return nullptr;
  ++fake::semaphores;return new(p)FakeSemaphore{available};
}
SemaphoreHandle_t xSemaphoreCreateMutex(){return semaphore(true,fake::failMutex);}
SemaphoreHandle_t xSemaphoreCreateBinary(){return semaphore(false,fake::failSignal);}
BaseType_t xSemaphoreTake(SemaphoreHandle_t p,TickType_t){assert(p);if(!p->available)return pdFALSE;p->available=false;return pdTRUE;}
BaseType_t xSemaphoreGive(SemaphoreHandle_t p){assert(p);p->available=true;return pdTRUE;}
void vSemaphoreDelete(SemaphoreHandle_t p){assert(p&&fake::semaphores);--fake::semaphores;p->~FakeSemaphore();assert(fake::release(p));}
BaseType_t xTaskCreate(TaskFunction_t run,const char*,uint32_t bytes,void* parameter,UBaseType_t,TaskHandle_t*) {
  ++fake::taskCreates;fake::taskStackRequested=bytes;fake::largestBeforeTask=fake::largest();
  if(fake::failTask)return pdFAIL;
  void* stack=fake::allocate(bytes);if(!stack)return pdFAIL;
  void* control=fake::allocate(256);if(!control){assert(fake::release(stack));return pdFAIL;}
  assert(!fake::pending);fake::pending=run;fake::parameter=parameter;fake::taskStack=stack;fake::taskControl=control;return pdPASS;
}
void vTaskDelete(TaskHandle_t task){assert(!task);fake::taskDeleted=true;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t){return 4096;} // Sentinel, not a real stack measurement.
size_t heap_caps_get_free_size(uint32_t){return fake::freeBytes();}
size_t heap_caps_get_largest_free_block(uint32_t){return fake::largest();}
size_t heap_caps_get_minimum_free_size(uint32_t){return fake::lowWater;}
namespace official_voice {
bool ArduinoTransport::post(const Request&,Reader&,ResponseSink&){assert(timeoutMs_&&false&&"test supplies an owned transport");return false;}
}

namespace {
using namespace official_voice_job;
using official_voice::Error;
int readerDestroyed=0,transportDestroyed=0,posts=0;
struct Reader final:official_voice::Reader {
  size_t remaining=32000;
  ~Reader()override{++readerDestroyed;}
  size_t read(uint8_t* out,size_t capacity)override{const size_t count=std::min(capacity,remaining);std::memset(out,0,count);remaining-=count;return count;}
};
struct Transport final:official_voice::Transport {
  ~Transport()override{++transportDestroyed;}
  bool post(const official_voice::Request& request,official_voice::Reader& body,official_voice::ResponseSink& response)override {
    ++posts;assert(std::strstr(request.path,"/audio/transcriptions?profile=default"));
    uint8_t buffer[257];size_t total=0;for(size_t count;(count=body.read(buffer,sizeof(buffer)));)total+=count;
    assert(total==request.contentLength);
    official_voice::ResponseHeaders headers;headers.status=200;headers.contentType="text/plain";headers.contentLength=5;
    return response.begin(headers)&&response.write(reinterpret_cast<const uint8_t*>("hello"),5);
  }
};
Context context(){Context value;assert(official_voice_job::makeContext(value,"http://studio.test:8650","fixture.jwt","default","session",1)==Error::Ok);return value;}
StartResult startStt(Job& job,const Context& value) {
  return job.startStt(value,std::unique_ptr<official_voice::Reader>(new Reader),32000,16000,1,123,
      std::unique_ptr<official_voice::Transport>(new Transport));
}
void fragmentedStt() {
  const auto binding=context();fake::reset();Job job;
  const int readers=readerDestroyed,transports=transportDestroyed;
  const auto result=startStt(job,binding);
  std::printf("fragmented STT: initial free=60856/largest=36852, state=%zu, pre-task largest=%zu, stack=%zu, result=%u\n",
      Job::fixedJobHeapBytes(),fake::largestBeforeTask,fake::taskStackRequested,unsigned(result));std::fflush(stdout);
  assert(result==StartResult::Started); // RED against the old shared 32 KiB budget.
  assert(fake::taskStackRequested==16*1024&&fake::largestBeforeTask<32*1024);
  assert(taskStackBytes(Type::Stt,binding)==fake::taskStackRequested);
  const auto& diagnostics=job.lastStartDiagnostics();
  assert(job.lastStartStage()==StartStage::Started&&job.lastStartResult()==StartResult::Started);
  assert(diagnostics.resourcesAvailable&&diagnostics.taskStackBytes==16*1024);
  assert(diagnostics.beforeStateFreeBytes==60856&&diagnostics.beforeStateLargestBlockBytes==36852);
  assert(diagnostics.beforeTaskFreeBytes<60856&&diagnostics.beforeTaskLargestBlockBytes==fake::largestBeforeTask);
  assert(job.busy()&&job.queueDrained());fake::runWorker();
  Output output;assert(job.pollDone(output)==Poll::Ready&&output.result.ok()&&!std::strcmp(output.transcript.data(),"hello"));
  assert(!job.busy()&&job.pollDone(output)==Poll::Idle&&readerDestroyed==readers+1&&transportDestroyed==transports+1);
  fake::clean();
}
void retainedTtsBudget() {
  const auto binding=context();fake::reset();Job job;
  assert(job.startTts(binding,"hello",std::unique_ptr<official_voice::Transport>(new Transport))==StartResult::NoMemory);
  assert(fake::taskStackRequested==32*1024&&!job.busy());fake::clean();
  assert(job.lastStartStage()==StartStage::TaskCreation&&job.lastStartResult()==StartResult::NoMemory);
  assert(taskStackBytes(Type::Tts,binding)==32*1024&&job.lastStartDiagnostics().taskStackBytes==32*1024);
  fake::reset(80*1024,24*1024);
  assert(job.startTts(binding,"hello",std::unique_ptr<official_voice::Transport>(new Transport))==StartResult::Started);
  assert(fake::taskStackRequested==32*1024);job.cancel();fake::runWorker();
  Output output;assert(job.pollDone(output)==Poll::Ready&&output.result.error==Error::Cancelled);fake::clean();
}
void retainedTlsBudget() {
  Context binding;assert(makeContext(binding,"https://studio.test:8650","fixture.jwt","default","session",1)==Error::Ok);
  fake::reset();Job job;
  assert(startStt(job,binding)==StartResult::NoMemory);
  assert(fake::taskStackRequested==32*1024&&taskStackBytes(Type::Stt,binding)==32*1024);
  assert(job.lastStartStage()==StartStage::TaskCreation&&!job.busy());fake::clean();
  fake::reset(80*1024,24*1024);
  assert(startStt(job,binding)==StartResult::Started&&fake::taskStackRequested==32*1024);
  fake::runWorker();Output output;assert(job.pollDone(output)==Poll::Ready&&output.result.ok());fake::clean();
  Context unknown;assert(taskStackBytes(Type::Stt,unknown)==32*1024);
}
void failureCleanup() {
  const auto binding=context();Job job;
  for(unsigned point=0;point<4;++point) {
    fake::reset();const int readers=readerDestroyed,transports=transportDestroyed;
    if(point==0)fake::failNextState=true;
    if(point==1)fake::failMutex=true;
    if(point==2)fake::failSignal=true;
    if(point==3)fake::failTask=true;
    assert(startStt(job,binding)==StartResult::NoMemory);
    const StartStage stages[]={StartStage::StateAllocation,StartStage::MutexCreation,StartStage::SignalCreation,StartStage::TaskCreation};
    assert(job.lastStartStage()==stages[point]&&job.lastStartResult()==StartResult::NoMemory);
    const auto& diagnostics=job.lastStartDiagnostics();
    assert(diagnostics.resourcesAvailable&&diagnostics.beforeStateFreeBytes==60856&&diagnostics.beforeStateLargestBlockBytes==36852);
    assert((diagnostics.beforeTaskFreeBytes!=0)==(point==3));
    Output output;assert(!job.busy()&&job.queueDrained()&&!job.workerDone()&&job.pollDone(output)==Poll::Idle);
    assert(readerDestroyed==readers+1&&transportDestroyed==transports+1);fake::clean();
  }
  // Failed starts must leave the same Job reusable, not just leak-free.
  fake::reset();assert(startStt(job,binding)==StartResult::Started);fake::runWorker();
  Output output;assert(job.pollDone(output)==Poll::Ready&&output.result.ok());fake::clean();
}
void invalidAndBusyOwnership() {
  fake::reset();Job job;Context invalid;
  assert(job.lastStartStage()==StartStage::None);
  int readers=readerDestroyed,transports=transportDestroyed;
  assert(startStt(job,invalid)==StartResult::Invalid);
  assert(job.lastStartStage()==StartStage::Validation&&!job.lastStartDiagnostics().resourcesAvailable);
  assert(readerDestroyed==readers+1&&transportDestroyed==transports+1);fake::clean();
  const auto binding=context();assert(startStt(job,binding)==StartResult::Started);
  readers=readerDestroyed;transports=transportDestroyed;const size_t live=fake::live;
  assert(startStt(job,binding)==StartResult::Busy);
  assert(job.lastStartStage()==StartStage::Validation&&job.lastStartResult()==StartResult::Busy);
  assert(fake::live==live&&readerDestroyed==readers+1&&transportDestroyed==transports+1);
  fake::runWorker();Output output;assert(job.pollDone(output)==Poll::Ready&&output.result.ok());fake::clean();
}
}
int main() {
  fragmentedStt();retainedTtsBudget();retainedTlsBudget();failureCleanup();invalidAndBusyOwnership();
  assert(posts==4);std::puts("Actual Arduino voice Job start, fragmented-heap, TLS/TTS budget and failure cleanup tests passed");
}
