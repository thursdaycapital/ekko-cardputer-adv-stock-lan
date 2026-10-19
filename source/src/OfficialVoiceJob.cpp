#include "OfficialVoiceJob.h"
#include "ChatUtf8.h"
#include <atomic>
#include <cstring>
#include <new>
#include <utility>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#else
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <system_error>
#include <thread>
#endif

namespace official_voice_job {
static_assert(kTaskStackBytes>=official_voice::AudioDecoder::recommendedTaskStackBytes(),"voice decoder stack budget");
namespace {
void wipe(void* memory,size_t bytes) {
  auto* next=static_cast<volatile uint8_t*>(memory);while(bytes--)*next++=0;
}
size_t length(const char* text,size_t maximum) {
  if(!text)return maximum+1;
  size_t count=0;while(count<=maximum&&text[count])++count;return count;
}
template<size_t N>bool copy(std::array<char,N>& output,const char* text) {
  const size_t count=length(text,N-1);if(count>=N)return false;
  std::memcpy(output.data(),text,count+1);return true;
}
void clear(Context& context) {
  context.baseUrl.fill(0);wipe(context.userJwt.data(),context.userJwt.size());
  context.profile.fill(0);context.session.fill(0);context.epoch=0;
}
bool nonblank(const char* text,size_t count) {
  for(size_t at=0;at<count;) {
    const auto* p=reinterpret_cast<const uint8_t*>(text+at);size_t width=0;
    if(p[0]==' '||(p[0]>=9&&p[0]<=13))width=1;
    else if(count-at>=2&&p[0]==0xc2&&p[1]==0xa0)width=2;
    else if(count-at>=3&&((p[0]==0xe1&&p[1]==0x9a&&p[2]==0x80)||
        (p[0]==0xe2&&p[1]==0x80&&((p[2]>=0x80&&p[2]<=0x8a)||p[2]==0xa8||p[2]==0xa9||p[2]==0xaf))||
        (p[0]==0xe2&&p[1]==0x81&&p[2]==0x9f)||(p[0]==0xe3&&p[1]==0x80&&p[2]==0x80)||
        (p[0]==0xef&&p[1]==0xbb&&p[2]==0xbf)))width=3;
    if(!width)return true;at+=width;
  }
  return false;
}
bool identifier(const char* text,size_t maximum) {
  const size_t count=length(text,maximum);
  return count&&count<=maximum&&native_chat::boundedIdentifier(text,maximum)&&nonblank(text,count);
}
bool validText(const char* text) {
  const size_t count=length(text,kTextBytes);
  return count&&count<=kTextBytes&&native_chat::validUtf8(text,count)&&nonblank(text,count);
}
enum class SlotState:uint8_t { Free, Ready, Borrowed };
struct Slot {
  std::array<int16_t,kPcmSamples> pcm{};
  size_t samples=0;uint32_t rate=0;SlotState state=SlotState::Free;
};
} // namespace

// Two initial references belong to owner + worker. Each borrowed slot adds a
// reference. No SharedState points back to Job or caller storage. Cancellation
// changes only atomics/queue state; the worker alone touches HTTP and Reader.
struct SharedState final:official_voice::Cancellation {
  Context context;Output output;
  Type type=Type::Stt;std::array<char,kTextBytes+1> text{};
  std::unique_ptr<official_voice::Reader> reader;
  std::unique_ptr<official_voice::Transport> transport;
  size_t pcmBytes=0;uint32_t rate=16000;uint16_t channels=1;uint64_t nonce=0;
  official_voice::RawPcmFormat raw;
  std::array<Slot,kPcmSlots> slots{};size_t writing=0,reading=0;
  std::atomic<unsigned> references{2};std::atomic<bool> cancellation{false},done{false};
#ifdef ARDUINO
  SemaphoreHandle_t mutex=xSemaphoreCreateMutex();
  SemaphoreHandle_t changed=xSemaphoreCreateBinary();
  bool valid()const{return mutex&&changed;}
  void lock(){xSemaphoreTake(mutex,portMAX_DELAY);}
  void unlock(){xSemaphoreGive(mutex);}
  void wake(){xSemaphoreGive(changed);}
  void wait(){xSemaphoreTake(changed,pdMS_TO_TICKS(20));}
#else
  std::mutex mutex;std::mutex waitingMutex;std::condition_variable changed;std::thread thread;
  bool valid()const{return true;}
  void lock(){mutex.lock();}
  void unlock(){mutex.unlock();}
  void wake(){changed.notify_all();}
  void wait(){std::unique_lock<std::mutex> lock(waitingMutex);changed.wait_for(lock,std::chrono::milliseconds(20));}
#endif
  ~SharedState() {
#ifdef ARDUINO
    if(changed)vSemaphoreDelete(changed);if(mutex)vSemaphoreDelete(mutex);
#else
    // Destruction on the final worker reference detaches itself. If owner or
    // final PCM lease wins instead, work has already ended and joining is safe.
    if(thread.joinable()) {
      if(thread.get_id()==std::this_thread::get_id())thread.detach();else thread.join();
    }
#endif
    wipe(text.data(),text.size());wipe(output.transcript.data(),output.transcript.size());
  }
  bool cancelled()const override{return cancellation.load(std::memory_order_relaxed);}
  void retain(){references.fetch_add(1,std::memory_order_relaxed);}
  void release(){if(references.fetch_sub(1,std::memory_order_acq_rel)==1)delete this;}
  void discardReady() {
    lock();for(auto& slot:slots)if(slot.state==SlotState::Ready)slot.state=SlotState::Free;unlock();wake();
  }
  void cancel(){cancellation.store(true,std::memory_order_relaxed);discardReady();}
  void sampleResources() {
#ifdef ARDUINO
    constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    const uint32_t free=heap_caps_get_free_size(caps),largest=heap_caps_get_largest_free_block(caps);
    if(!output.resourcesAvailable || free<output.internalHeapMinObservedBytes) output.internalHeapMinObservedBytes=free;
    if(!output.resourcesAvailable || largest<output.internalLargestBlockMinObservedBytes) output.internalLargestBlockMinObservedBytes=largest;
    output.systemHeapLowWaterBytes=heap_caps_get_minimum_free_size(caps);
    output.workerStackFreeBytes=uxTaskGetStackHighWaterMark(nullptr);
    output.resourcesAvailable=true;
#endif
  }
  bool drained(){lock();bool all=true;for(const auto& slot:slots)all=all&&slot.state==SlotState::Free;unlock();return all;}
  bool enqueue(const int16_t* pcm,size_t samples,uint32_t rate) {
    if(!pcm||!samples||samples>kPcmSamples||(samples&1)||rate<8000||rate>96000)return false;
    for(;;) {
      lock();
      if(cancelled()){unlock();return false;}
      auto& slot=slots[writing];
      if(slot.state==SlotState::Free) {
        std::memcpy(slot.pcm.data(),pcm,samples*sizeof(int16_t));slot.samples=samples;slot.rate=rate;
        slot.state=SlotState::Ready;writing=(writing+1)%kPcmSlots;unlock();return true;
      }
      unlock();wait(); // A cancel wakes immediately; timed check prevents lost notification stalls.
    }
  }
  static bool accept(void* context,const int16_t* pcm,size_t count,uint32_t rate) {
    auto* state=static_cast<SharedState*>(context);
    state->sampleResources();return state->enqueue(pcm,count,rate);
  }
};

Context::~Context(){wipe(userJwt.data(),userJwt.size());}
bool validContext(const Context& context) {
  const size_t base=length(context.baseUrl.data(),kBaseUrlBytes),jwt=length(context.userJwt.data(),kJwtBytes);
  if(!base||base>kBaseUrlBytes||!jwt||jwt>kJwtBytes||!context.epoch||
      !identifier(context.profile.data(),kProfileBytes)||!identifier(context.session.data(),kSessionBytes))return false;
  const char* authority=nullptr;
  if(!std::strncmp(context.baseUrl.data(),"http://",7))authority=context.baseUrl.data()+7;
  if(!std::strncmp(context.baseUrl.data(),"https://",8))authority=context.baseUrl.data()+8;
  if(!authority||!*authority||*authority=='/'||*authority==':')return false;
  for(const char* p=context.baseUrl.data();*p;++p)if(uint8_t(*p)<=0x20||*p=='\\'||*p=='@'||*p=='?'||*p=='#')return false;
  for(const char* p=context.userJwt.data();*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||
      (*p>='0'&&*p<='9')||*p=='.'||*p=='_'||*p=='-'))return false;
  return true;
}
official_voice::Error makeContext(Context& output,const char* base,const char* jwt,const char* profile,const char* session,uint32_t epoch) {
  auto candidate=std::unique_ptr<Context>(new(std::nothrow)Context);
  if(!candidate){clear(output);return official_voice::Error::OutOfMemory;}
  candidate->epoch=epoch;
  if(!copy(candidate->baseUrl,base)||!copy(candidate->userJwt,jwt)||!copy(candidate->profile,profile)||
      !copy(candidate->session,session)||!validContext(*candidate)){clear(output);return official_voice::Error::InvalidBinding;}
  output=*candidate;return official_voice::Error::Ok;
}

PcmBlock::~PcmBlock(){reset();}
PcmBlock::PcmBlock(PcmBlock&& other)noexcept:state_(other.state_),slot_(other.slot_){other.state_=nullptr;}
PcmBlock& PcmBlock::operator=(PcmBlock&& other)noexcept {
  if(this!=&other){reset();state_=other.state_;slot_=other.slot_;other.state_=nullptr;}return *this;
}
const int16_t* PcmBlock::data()const{return state_?state_->slots[slot_].pcm.data():nullptr;}
size_t PcmBlock::samples()const{return state_?state_->slots[slot_].samples:0;}
uint32_t PcmBlock::sampleRate()const{return state_?state_->slots[slot_].rate:0;}
uint32_t PcmBlock::epoch()const{return state_?state_->context.epoch:0;}
void PcmBlock::reset() {
  if(!state_)return;
  SharedState* state=state_;state_=nullptr;
  state->lock();state->slots[slot_].state=SlotState::Free;state->unlock();state->wake();state->release();
}

Job::~Job(){if(state_){state_->cancel();state_->release();}}
size_t Job::fixedJobHeapBytes(){return sizeof(SharedState);}
StartResult Job::startTts(const Context& context,const char* text,std::unique_ptr<official_voice::Transport> transport,official_voice::RawPcmFormat raw) {
  return start(context,Type::Tts,text,{},0,0,0,0,std::move(transport),raw);
}
StartResult Job::startStt(const Context& context,std::unique_ptr<official_voice::Reader> reader,size_t bytes,
                          uint32_t rate,uint16_t channels,uint64_t nonce,std::unique_ptr<official_voice::Transport> transport) {
  return start(context,Type::Stt,nullptr,std::move(reader),bytes,rate,channels,nonce,std::move(transport),{});
}
StartResult Job::start(const Context& context,Type type,const char* text,std::unique_ptr<official_voice::Reader> reader,
                       size_t bytes,uint32_t rate,uint16_t channels,uint64_t nonce,
                       std::unique_ptr<official_voice::Transport> transport,official_voice::RawPcmFormat raw) {
  if(state_)return StartResult::Busy;
  if(!validContext(context))return StartResult::Invalid;
  if(type==Type::Tts&&!validText(text))return StartResult::Invalid;
  if(type==Type::Stt&&(!reader||!bytes||bytes>16*1024*1024||(channels!=1&&channels!=2)||
      rate<8000||rate>96000||bytes%(channels*2)||!nonce))return StartResult::Invalid;
#ifndef ARDUINO
  if(!transport)return StartResult::NotSupported;
#endif
  SharedState* state=nullptr;
#ifdef ARDUINO
  state=new(std::nothrow)SharedState;
#else
  try{state=new(std::nothrow)SharedState;}
  catch(const std::system_error&){return StartResult::NoMemory;}
  catch(const std::bad_alloc&){return StartResult::NoMemory;}
#endif
  if(!state)return StartResult::NoMemory;
  if(!state->valid()){state->release();state->release();return StartResult::NoMemory;}
  state->context=context;state->type=type;state->reader=std::move(reader);state->transport=std::move(transport);
  state->pcmBytes=bytes;state->rate=rate;state->channels=channels;state->nonce=nonce;state->raw=raw;
  if(text)copy(state->text,text);
  state->output.type=type;state->output.epoch=context.epoch;state->output.profile=context.profile;state->output.session=context.session;
  state_=state;
#ifdef ARDUINO
  if(xTaskCreate(worker,"studio-voice",kTaskStackBytes,state,1,nullptr)!=pdPASS) {
    state_=nullptr;state->release();state->release();return StartResult::NoMemory;
  }
#else
  try{state->thread=std::thread(&Job::worker,state);}
  catch(const std::system_error&){state_=nullptr;state->release();state->release();return StartResult::NoMemory;}
  catch(const std::bad_alloc&){state_=nullptr;state->release();state->release();return StartResult::NoMemory;}
#endif
  return StartResult::Started;
}
bool Job::tryTakePcm(PcmBlock& block) {
  if(!state_||block)return false;
  state_->lock();auto& slot=state_->slots[state_->reading];
  if(state_->cancelled()||slot.state!=SlotState::Ready){state_->unlock();return false;}
  state_->retain();block.state_=state_;block.slot_=state_->reading;slot.state=SlotState::Borrowed;
  state_->reading=(state_->reading+1)%kPcmSlots;state_->unlock();return true;
}
Poll Job::pollDone(Output& output) {
  if(!state_)return Poll::Idle;
  if(!state_->done.load(std::memory_order_acquire)||!state_->drained())return Poll::Pending;
  output=state_->output;
  if(state_->cancelled()){output.result.error=official_voice::Error::Cancelled;output.result.textBytes=0;output.transcript.fill(0);}
  SharedState* finished=state_;state_=nullptr;finished->release();return Poll::Ready;
}
bool Job::workerDone()const{return state_&&state_->done.load(std::memory_order_acquire);}
bool Job::queueDrained()const{return !state_||state_->drained();}
bool Job::busy()const{return state_!=nullptr;}
void Job::cancel(){if(state_)state_->cancel();}
void Job::worker(void* parameter) {
  auto* state=static_cast<SharedState*>(parameter);
  state->sampleResources();
  {
#ifdef ARDUINO
    official_voice::ArduinoTransport defaultTransport;
    auto& transport=state->transport?*state->transport:defaultTransport;
#else
    auto& transport=*state->transport;
#endif
    official_voice::Binding binding;binding.baseUrl=state->context.baseUrl.data();
    binding.userJwt=state->context.userJwt.data();binding.profile=state->context.profile.data();
    if(state->type==Type::Stt) {
      official_voice::Recording recording;recording.pcm=state->reader.get();recording.pcmBytes=state->pcmBytes;
      recording.sampleRate=state->rate;recording.channels=state->channels;
      state->output.result=official_voice::transcribe(transport,binding,recording,state->nonce,
          state->output.transcript.data(),state->output.transcript.size(),state);
      if(state->output.result.ok()&&!nonblank(state->output.transcript.data(),state->output.result.textBytes)) {
        state->output.result.error=official_voice::Error::InvalidText;
        state->output.result.textBytes=0;state->output.transcript.fill(0);
      }
    } else state->output.result=official_voice::synthesize(transport,binding,state->text.data(),SharedState::accept,state,state->raw,state);
    if(state->cancelled()){state->output.result.error=official_voice::Error::Cancelled;state->output.result.textBytes=0;state->output.transcript.fill(0);}
    if(!state->output.result.ok())state->discardReady();
    state->reader.reset();state->transport.reset();wipe(state->text.data(),state->text.size());
  } // HTTP, decoder, Reader and Transport are gone before reporting completion.
  state->sampleResources();
  state->done.store(true,std::memory_order_release);state->wake();state->release();
#ifdef ARDUINO
  vTaskDelete(nullptr);
#endif
}
} // namespace official_voice_job
