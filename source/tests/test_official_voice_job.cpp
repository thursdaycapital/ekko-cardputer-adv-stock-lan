#include "OfficialVoiceJob.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<int> failAfter{-1};
bool allocationFails(){int count=failAfter.load();if(count<0)return false;if(!count)return true;failAfter=count-1;return false;}
}
void* operator new(std::size_t bytes,const std::nothrow_t&)noexcept {
  if(allocationFails())return nullptr;try{return ::operator new(bytes);}catch(...){return nullptr;}
}
void* operator new[](std::size_t bytes,const std::nothrow_t&)noexcept {
  if(allocationFails())return nullptr;try{return ::operator new[](bytes);}catch(...){return nullptr;}
}
void operator delete(void* pointer,const std::nothrow_t&)noexcept{::operator delete(pointer);}
void operator delete[](void* pointer,const std::nothrow_t&)noexcept{::operator delete[](pointer);}

namespace {
using namespace official_voice_job;
using official_voice::Error;
using Clock=std::chrono::steady_clock;
Context context(){Context value;assert(makeContext(value,"http://studio.test:8650","fixture.jwt-token","中文 profile","session-1",81)==Error::Ok);return value;}
template<class Predicate>void await(Predicate predicate) {
  const auto deadline=Clock::now()+std::chrono::seconds(5);
  while(!predicate()){assert(Clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
struct Evidence {
  std::atomic<int> calls{0},reads{0},writes{0};
  std::atomic<bool> entered{false},finished{false},destroyed{false},readerDestroyed{false};
  std::thread::id worker;
  std::string path,body;size_t expected=0;
};
struct Response {
  std::vector<uint8_t> bytes;std::string media="audio/wav";int status=200;
  int64_t declared=-2;size_t chunk=4096;bool complete=true,silent=false,slowUpload=false;
};
class Pattern final:public official_voice::Reader {
 public:
  Pattern(size_t bytes,std::shared_ptr<Evidence> evidence):remaining_(bytes),evidence_(std::move(evidence)){}
  ~Pattern()override{evidence_->readerDestroyed=true;}
  size_t read(uint8_t* output,size_t capacity)override {
    const size_t count=std::min(remaining_,std::min(capacity,size_t(127)));
    for(size_t n=0;n<count;++n)output[n]=uint8_t((offset_+n)%251);
    offset_+=count;remaining_-=count;return count;
  }
 private:size_t remaining_,offset_=0;std::shared_ptr<Evidence> evidence_;
};
class Fixture final:public official_voice::Transport {
 public:
  Fixture(Response response,std::shared_ptr<Evidence> evidence):response_(std::move(response)),evidence_(std::move(evidence)){}
  ~Fixture()override{evidence_->destroyed=true;}
  bool post(const official_voice::Request& request,official_voice::Reader& source,official_voice::ResponseSink& sink)override {
    ++evidence_->calls;evidence_->worker=std::this_thread::get_id();evidence_->entered.store(true,std::memory_order_release);
    assert(request.cancellation&&request.binding);
    evidence_->path=request.path;evidence_->expected=request.contentLength;
    while(response_.silent&&!request.cancellation->cancelled())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(request.cancellation->cancelled()){evidence_->finished=true;return false;}
    uint8_t buffer[257];
    for(;;) {
      if(request.cancellation->cancelled()){evidence_->finished=true;return false;}
      if(response_.slowUpload)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const size_t count=source.read(buffer,sizeof(buffer));++evidence_->reads;
      if(!count)break;
      assert(count<=sizeof(buffer));evidence_->body.append(reinterpret_cast<const char*>(buffer),count);
    }
    if(request.cancellation->cancelled()||evidence_->body.size()!=request.contentLength){evidence_->finished=true;return false;}
    official_voice::ResponseHeaders headers;headers.status=response_.status;headers.contentType=response_.media.c_str();
    headers.contentLength=response_.declared==-2?int64_t(response_.bytes.size()):response_.declared;
    if(!sink.begin(headers)){evidence_->finished=true;return false;}
    for(size_t at=0;at<response_.bytes.size();) {
      if(request.cancellation->cancelled()){evidence_->finished=true;return false;}
      const size_t count=std::min(response_.chunk,response_.bytes.size()-at);++evidence_->writes;
      if(!sink.write(response_.bytes.data()+at,count)){evidence_->finished=true;return false;}
      at+=count;
    }
    // Binding and owned source must outlive the complete request, even if the
    // originating Context or Job was destroyed while this worker was active.
    assert(!std::strcmp(request.binding->baseUrl,"http://studio.test:8650"));
    assert(!std::strcmp(request.binding->profile,"中文 profile"));
    evidence_->finished=true;return response_.complete;
  }
 private:Response response_;std::shared_ptr<Evidence> evidence_;
};
void put16(std::vector<uint8_t>& bytes,uint16_t value){bytes.push_back(uint8_t(value));bytes.push_back(uint8_t(value>>8));}
void put32(std::vector<uint8_t>& bytes,uint32_t value){for(size_t n=0;n<4;++n)bytes.push_back(uint8_t(value>>(n*8)));}
void literal(std::vector<uint8_t>& bytes,const char* text){bytes.insert(bytes.end(),text,text+std::strlen(text));}
int16_t sample(size_t n){return int16_t(int(n%20001)-10000);}
Response wave(size_t samples) {
  Response response;auto& bytes=response.bytes;
  literal(bytes,"RIFF");put32(bytes,uint32_t(36+samples*2));literal(bytes,"WAVEfmt ");put32(bytes,16);put16(bytes,1);
  put16(bytes,1);put32(bytes,16000);put32(bytes,32000);put16(bytes,2);put16(bytes,16);literal(bytes,"data");put32(bytes,uint32_t(samples*2));
  for(size_t n=0;n<samples;++n)put16(bytes,uint16_t(sample(n)));
  return response;
}
Response transcript(std::string text) {
  Response response;response.media="text/plain; charset=utf-8";response.bytes.assign(text.begin(),text.end());response.chunk=7;return response;
}
std::unique_ptr<official_voice::Transport> transport(Response response,const std::shared_ptr<Evidence>& evidence) {
  return std::unique_ptr<official_voice::Transport>(new Fixture(std::move(response),evidence));
}
std::unique_ptr<official_voice::Reader> reader(size_t bytes,const std::shared_ptr<Evidence>& evidence) {
  return std::unique_ptr<official_voice::Reader>(new Pattern(bytes,evidence));
}
Output done(Job& job) {
  Output output;await([&]{return job.pollDone(output)==Poll::Ready;});assert(!job.busy());return output;
}
Output drain(Job& job,std::vector<int16_t>& samples) {
  Output output;await([&]{
    PcmBlock block;
    while(job.tryTakePcm(block)) {
      assert(block.samples()&&block.samples()<=1024&&!(block.samples()&1)&&block.sampleRate()>=8000&&block.epoch()==81);
      samples.insert(samples.end(),block.data(),block.data()+block.samples());block.reset();
    }
    return job.pollDone(output)==Poll::Ready;
  });return output;
}
void budget() {
  official_voice::detail::NetworkBudget clock(1000,100);
  assert(clock.remaining(1040)==60);clock.pause(1040);
  assert(clock.remaining(1000040)==60);clock.resume(1000040);
  assert(clock.remaining(1000050)==50&&clock.remaining(1000100)==0);
  official_voice::detail::NetworkBudget wrapping(UINT32_MAX-10,100);
  assert(wrapping.remaining(5)==84);wrapping.pause(5);wrapping.resume(1005);
  assert(wrapping.remaining(1010)==79&&wrapping.remaining(1089)==0);
  official_voice::detail::NetworkBudget zero(0,0);assert(!zero.remaining(0));
  official_voice::detail::NetworkBudget expired(0,10);expired.pause(11);expired.resume(111);assert(!expired.remaining(111));
}
void successfulTts() {
  auto binding=context();Job job;Output result;
  assert(job.pollDone(result)==Poll::Idle&&job.queueDrained()&&!job.workerDone());
  auto evidence=std::make_shared<Evidence>();
  assert(job.startTts(binding,"你好",transport(wave(5001),evidence))==StartResult::Started);
  await([&]{return evidence->entered.load(std::memory_order_acquire);});assert(evidence->worker!=std::this_thread::get_id());
  assert(job.startTts(binding,"another")==StartResult::Busy);
  std::vector<int16_t> samples;result=drain(job,samples);
  assert(result.result.ok()&&result.type==Type::Tts&&result.epoch==81&&result.result.httpStatus==200);
  assert(!result.resourcesAvailable && !result.workerStackFreeBytes && !result.internalHeapMinObservedBytes &&
      !result.internalLargestBlockMinObservedBytes && !result.systemHeapLowWaterBytes);
  assert(result.result.samples==5002&&samples.size()==5002&&result.result.sampleRate==16000);
  for(size_t n=0;n<5001;++n)assert(samples[n]==sample(n));assert(samples.back()==0);
  assert(result.transcript[0]==0&&!std::strcmp(result.session.data(),"session-1"));
  assert(evidence->path=="/api/studio/tts/synthesize?profile=%E4%B8%AD%E6%96%87%20profile");
  assert(evidence->calls==1&&evidence->body=="{\"text\":\"你好\",\"options\":{\"format\":\"wav\"}}");await([&]{return evidence->destroyed.load();});

  // Small audio can finish decoding before playback consumes the final slot.
  evidence=std::make_shared<Evidence>();assert(job.startTts(binding,"x",transport(wave(4),evidence))==StartResult::Started);
  await([&]{return job.workerDone();});assert(!job.queueDrained()&&job.pollDone(result)==Poll::Pending);
  PcmBlock block;assert(job.tryTakePcm(block));assert(job.pollDone(result)==Poll::Pending);
  PcmBlock moved(std::move(block));assert(!block&&moved&&moved.samples()==4);moved.reset();
  result=done(job);assert(result.result.ok()&&job.queueDrained());
}
void queueCancellationAndLifetime() {
  auto binding=context();Job job;auto evidence=std::make_shared<Evidence>();
  assert(job.startTts(binding,"long",transport(wave(14000),evidence))==StartResult::Started);
  PcmBlock first,second;await([&]{return job.tryTakePcm(first);});await([&]{return job.tryTakePcm(second);});
  assert(first.data()!=second.data());const int16_t firstValue=first.data()[0],secondValue=second.data()[0];
  assert(!job.tryTakePcm(first));std::this_thread::sleep_for(std::chrono::milliseconds(40));
  assert(!job.workerDone()&&!job.queueDrained()&&!evidence->finished);
  job.cancel();await([&]{return job.workerDone();});Output result;
  assert(job.pollDone(result)==Poll::Pending&&first.data()[0]==firstValue&&second.data()[0]==secondValue);
  first.reset();assert(job.pollDone(result)==Poll::Pending);second.reset();
  result=done(job);assert(result.result.error==Error::Cancelled&&result.transcript[0]==0&&evidence->calls==1);

  // A borrowed fixed slot owns the storage independently of the Job. Worker
  // cancellation/destruction must neither overwrite nor release that buffer.
  evidence=std::make_shared<Evidence>();PcmBlock surviving;
  {
    auto ephemeral=std::unique_ptr<Job>(new Job);auto ownContext=std::unique_ptr<Context>(new Context(context()));
    assert(ephemeral->startTts(*ownContext,"long",transport(wave(14000),evidence))==StartResult::Started);
    await([&]{return ephemeral->tryTakePcm(surviving);});ownContext.reset();
    const auto start=Clock::now();ephemeral.reset();assert(Clock::now()-start<std::chrono::milliseconds(500));
  }
  await([&]{return evidence->destroyed.load();});assert(surviving&&surviving.epoch()==81&&surviving.data()[0]==sample(0));
  PcmBlock moveAssigned;moveAssigned=std::move(surviving);assert(!surviving&&moveAssigned);moveAssigned.reset();
}
void stt() {
  auto binding=context();Job job;
  std::string boundary;for(size_t n=0;n<170;++n)boundary+="中";boundary+="ab";
  for(const std::string& text:{std::string("你好"),std::string(512,'a'),boundary}) {
    auto evidence=std::make_shared<Evidence>();
    assert(job.startStt(binding,reader(32000,evidence),32000,16000,1,0x123456789abcdef0ULL,
        transport(transcript(text),evidence))==StartResult::Started);
    auto output=done(job);assert(output.result.ok()&&output.type==Type::Stt&&output.result.textBytes==text.size());
    assert(std::string(output.transcript.data())==text&&output.epoch==81&&job.queueDrained());
    assert(evidence->path=="/api/studio/voice/proxy/%E4%B8%AD%E6%96%87%20profile/v1/audio/transcriptions?profile=%E4%B8%AD%E6%96%87%20profile");
    assert(evidence->calls==1&&evidence->body.size()==evidence->expected&&evidence->readerDestroyed);
    assert(evidence->body.find("name=\"response_format\"\r\n\r\ntext\r\n")!=std::string::npos);
    const size_t at=evidence->body.find("RIFF");assert(at!=std::string::npos&&evidence->body.substr(at+36,4)=="data");
    for(size_t n=0;n<32000;++n)assert(uint8_t(evidence->body[at+44+n])==uint8_t(n%251));
  }
  for(bool knownLength:{false,true}) {
    auto evidence=std::make_shared<Evidence>();auto reply=transcript(std::string(513,'a'));if(!knownLength)reply.declared=-1;
    assert(job.startStt(binding,reader(8,evidence),8,16000,1,3,transport(reply,evidence))==StartResult::Started);
    auto output=done(job);assert(output.result.error==Error::ResponseTooLarge&&output.result.textBytes==0&&output.transcript[0]==0);
  }
  for(std::string text:{std::string("\xc0\xaf",2),std::string("a\0b",3),std::string(" \n"),std::string("　"),std::string()}) {
    auto evidence=std::make_shared<Evidence>();auto reply=transcript(text);
    assert(job.startStt(binding,reader(8,evidence),8,16000,1,3,transport(reply,evidence))==StartResult::Started);
    auto output=done(job);assert(output.result.error==Error::InvalidText&&output.transcript[0]==0);
  }
}
void networkCancellation() {
  auto binding=context();Job job;auto evidence=std::make_shared<Evidence>();auto response=transcript("never");response.silent=true;
  assert(job.startStt(binding,reader(32000,evidence),32000,16000,1,5,transport(response,evidence))==StartResult::Started);
  await([&]{return evidence->entered.load();});job.cancel();auto output=done(job);
  assert(output.result.error==Error::Cancelled&&evidence->calls==1&&evidence->readerDestroyed&&output.transcript[0]==0);
  evidence=std::make_shared<Evidence>();response=transcript("never");response.slowUpload=true;
  assert(job.startStt(binding,reader(320000,evidence),320000,16000,1,5,transport(response,evidence))==StartResult::Started);
  await([&]{return evidence->reads>=3;});job.cancel();output=done(job);
  assert(output.result.error==Error::Cancelled&&evidence->body.size()<evidence->expected&&evidence->calls==1);
  evidence=std::make_shared<Evidence>();response=wave(8);response.silent=true;
  {
    auto ephemeral=std::unique_ptr<Job>(new Job);
    assert(ephemeral->startTts(binding,"hello",transport(response,evidence))==StartResult::Started);
    await([&]{return evidence->entered.load();});ephemeral.reset();
  }
  await([&]{return evidence->destroyed.load();});assert(evidence->calls==1);
}
void errorsAndReuse() {
  auto binding=context();Job job;
  for(int status:{401,403,429,500,302}) {
    auto evidence=std::make_shared<Evidence>();auto reply=wave(4);reply.status=status;
    assert(job.startTts(binding,"hi",transport(reply,evidence))==StartResult::Started);
    const auto output=done(job);assert(output.result.httpStatus==status&&output.result.error==
        (status==401||status==403?Error::AuthenticationRequired:Error::HttpFailed));
    assert(job.queueDrained()&&evidence->calls==1);
  }
  for(bool complete:{false,true}) {
    auto evidence=std::make_shared<Evidence>();auto reply=wave(6000);
    if(complete)reply.bytes.pop_back();else reply.complete=false;
    assert(job.startTts(binding,"hi",transport(reply,evidence))==StartResult::Started);
    std::vector<int16_t> samples;const auto output=drain(job,samples);
    assert(output.result.error==(complete?Error::InvalidAudio:Error::TransportFailed)&&job.queueDrained());
  }
  for(size_t n=0;n<30;++n) {
    auto evidence=std::make_shared<Evidence>();
    assert(job.startTts(binding,"hi",transport(wave(6),evidence))==StartResult::Started);
    std::vector<int16_t> samples;const auto output=drain(job,samples);assert(output.result.ok()&&samples.size()==6);
  }
}
void validationAndMemory() {
  auto binding=context();Job job;
  assert(job.startTts(binding,"hi")==StartResult::NotSupported);
  for(const std::string& text:{std::string(),std::string("　"),std::string(" \n"),std::string("\xc0\xaf",2),std::string(1025,'x')}) {
    auto evidence=std::make_shared<Evidence>();assert(job.startTts(binding,text.c_str(),transport(wave(4),evidence))==StartResult::Invalid);
    assert(!job.busy()&&evidence->calls==0&&evidence->destroyed);
  }
  auto evidence=std::make_shared<Evidence>();
  assert(job.startStt(binding,reader(8,evidence),8,16000,1,0,transport(transcript("x"),evidence))==StartResult::Invalid&&evidence->readerDestroyed);
  evidence=std::make_shared<Evidence>();assert(job.startStt(binding,reader(8,evidence),7,16000,1,1,transport(transcript("x"),evidence))==StartResult::Invalid);
  Context invalid;assert(makeContext(invalid,"http://user@host","jwt","default","s",1)==Error::InvalidBinding);
  assert(makeContext(invalid,"http://host","jwt\r\nX:","default","s",1)==Error::InvalidBinding);
  assert(makeContext(invalid,"http://host","jwt","　","s",1)==Error::InvalidBinding);
  assert(makeContext(binding,binding.baseUrl.data(),binding.userJwt.data(),binding.session.data(),binding.profile.data(),82)==Error::Ok);
  assert(!std::strcmp(binding.profile.data(),"session-1")&&!std::strcmp(binding.session.data(),"中文 profile"));
  binding=context();
  failAfter=0;const auto build=makeContext(invalid,"http://host","jwt","default","s",1);failAfter=-1;
  assert(build==Error::OutOfMemory&&!invalid.userJwt[0]);
  evidence=std::make_shared<Evidence>();auto fake=transport(wave(4),evidence);failAfter=0;
  const auto failedStart=job.startTts(binding,"x",std::move(fake));failAfter=-1;
  assert(failedStart==StartResult::NoMemory&&!job.busy()&&evidence->destroyed);
  // A valid state followed by decoder allocation failure is an explicit result.
  evidence=std::make_shared<Evidence>();fake=transport(wave(4),evidence);failAfter=1;
  const auto decoderStart=job.startTts(binding,"x",std::move(fake));assert(decoderStart==StartResult::Started);
  const auto decoderFailure=done(job);failAfter=-1;
  assert(decoderFailure.result.error==Error::OutOfMemory&&job.queueDrained());
}
void mp3(const char* path) {
  std::ifstream file(path,std::ios::binary);assert(file);
  Response response;response.media="audio/mpeg";
  response.bytes=std::vector<uint8_t>(std::istreambuf_iterator<char>(file),{});response.chunk=97;
  auto binding=context();Job job;auto evidence=std::make_shared<Evidence>();
  assert(job.startTts(binding,"MP3",transport(response,evidence))==StartResult::Started);
  std::vector<int16_t> samples;const auto output=drain(job,samples);
  assert(output.result.ok()&&output.result.sampleRate==16000&&samples.size()>=16000&&samples.size()<20000);
  double energy=0;for(auto value:samples)energy+=double(value)*value;assert(energy/samples.size()>1000000);
}
} // namespace
int main(int argc,char** argv) {
  assert(argc==2);budget();successfulTts();queueCancellationAndLifetime();stt();networkCancellation();errorsAndReuse();validationAndMemory();mp3(argv[1]);
  std::printf("Official voice job tests passed; fixed job=%zu Context=%zu Output=%zu decoder=%zu stack=%u bytes\n",
      Job::fixedJobHeapBytes(),sizeof(Context),sizeof(Output),official_voice::AudioDecoder::workingMemoryBytes(),kTaskStackBytes);
}
