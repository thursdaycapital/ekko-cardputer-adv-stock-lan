"""Compile the actual recording-finish + deferred cleanup seams with host boundaries.

This verifies state and ownership, not ESP32 allocation, microphone or network IO.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware=Path(__file__).resolve().parents[1]
main=(firmware/'src/main.cpp').read_text()
a=main.index('enum class RecordingStopReason:')
recording_state=main[a:main.index('\nbool recordingActive()',a)]
a=main.index('void finishOfficialRecording(bool success,')
finish=main[a:main.index('\nvoid startOfficialRecording()',a)]
a=main.index('  if(officialVoiceCleanupPending && !officialRecording.active && !officialRecordingStore.readerActive()) {')
cleanup=main[a:main.index('\n  if(officialRecordingRequested)',a)]
compiler=shutil.which('clang++') or shutil.which('g++')
assert compiler, 'C++17 compiler required'
prelude=r'''
#include "DeviceGestureMailbox.h"
#include "MicCaptureQueue.h"
DeviceGestureMailbox gestureMailbox;
void cardputerSuppressGestureUntilReleased(){gestureMailbox.suppressUntilReleased();}
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
namespace test {
int blocks=0,readers=0,startCalls=0,acceptedJobs=0,micEnds=0,audioClears=0,samples=0;
bool contextAvailable=true,readerAvailable=true,readerError=false;
size_t freeHeap=500000,largestHeap=500000;
uint32_t selectedStackBytes=16*1024;
}
struct Block {Block(){++test::blocks;}~Block(){--test::blocks;}};
struct Reader {
 std::shared_ptr<Block> owner;
 explicit Reader(std::shared_ptr<Block> value):owner(std::move(value)){++test::readers;}
 ~Reader(){--test::readers;}
};
namespace ram_recording {enum class Error {Ok,Busy,InvalidState,OutOfMemory};using Result=Error;}
struct Store {
 std::shared_ptr<Block> owner;ram_recording::Error finishResult=ram_recording::Error::Ok;
 size_t recordedBytes()const{return 96000;}size_t encodedBytes()const{return 24000;}
 ram_recording::Error finish(){return finishResult;}
 std::unique_ptr<Reader> openReader(ram_recording::Result& error){
  error=test::readerError?ram_recording::Error::InvalidState:ram_recording::Error::Ok;
  if(!test::readerAvailable){error=ram_recording::Error::OutOfMemory;return {};}
  return std::unique_ptr<Reader>(new Reader(owner));
 }
 ram_recording::Error discard(){if(readerActive())return ram_recording::Error::Busy;owner.reset();return ram_recording::Error::Ok;}
 bool readerActive()const{return test::readers!=0;}
} officialRecordingStore;
namespace official_voice_job {
enum class Type:uint8_t {Stt,Tts};
enum class StartResult:uint8_t {Started,Busy,Invalid,NoMemory,NotSupported};
enum class StartStage:uint8_t {None,Validation,StateAllocation,MutexCreation,SignalCreation,TaskCreation,Started};
struct StartDiagnostics {
 StartStage stage=StartStage::None;StartResult result=StartResult::Invalid;
 uint32_t taskStackBytes=0,beforeStateFreeBytes=0,beforeStateLargestBlockBytes=0,beforeTaskFreeBytes=0,beforeTaskLargestBlockBytes=0;
 bool resourcesAvailable=false;
};
struct Context {};
struct Output {
 struct Result {uint8_t error=0;int httpStatus=0;} result;
 Type type=Type::Stt;bool resourcesAvailable=false;
 uint32_t workerStackFreeBytes=0,internalHeapMinObservedBytes=0,internalLargestBlockMinObservedBytes=0,systemHeapLowWaterBytes=0;
};
uint32_t taskStackBytes(Type type,const Context&){assert(type==Type::Stt);return test::selectedStackBytes;}
struct Job {static size_t fixedJobHeapBytes(){return 1024;}};
}
struct VoiceJob {
 official_voice_job::StartResult result=official_voice_job::StartResult::Started;
 official_voice_job::StartDiagnostics diagnostics;
 std::unique_ptr<Reader> held;
 official_voice_job::StartStage lastStartStage()const{return diagnostics.stage;}
 const official_voice_job::StartDiagnostics& lastStartDiagnostics()const{return diagnostics;}
 official_voice_job::StartResult startStt(const official_voice_job::Context&,std::unique_ptr<Reader> reader,
                                        size_t bytes,uint32_t rate,uint16_t channels,uint64_t){
  ++test::startCalls;assert(bytes==96000&&rate==16000&&channels==1);
  diagnostics={};diagnostics.result=result;diagnostics.taskStackBytes=test::selectedStackBytes;
  diagnostics.stage=result==official_voice_job::StartResult::Busy?official_voice_job::StartStage::Validation:
      result==official_voice_job::StartResult::Started?official_voice_job::StartStage::Started:official_voice_job::StartStage::TaskCreation;
  diagnostics.beforeStateFreeBytes=123456;diagnostics.beforeTaskFreeBytes=65432;diagnostics.resourcesAvailable=true;
  if(result==official_voice_job::StartResult::Started){++test::acceptedJobs;held=std::move(reader);}
  return result;
 }
} officialVoiceJob;
enum class VoiceInputChannel:uint8_t {Undecided,Left,Right,Mixed};
struct Capture {
 bool active=false,failed=false,storageRetained=false;uint32_t sampleRate=16000,bufferBytes=0;
 mic_capture::Error error=mic_capture::Error::None;mic_capture::Stats queue;
} captureFixture;
using CardputerCaptureStats=Capture;
Capture cardputerCaptureStats(){return captureFixture;}
void cardputerAudioClear(){++test::audioClears;}
struct Mic {void end(){++test::micEnds;}};
struct {Mic Mic;} M5Cardputer;
void resetListeningMonitor(){}
void sampleOfficialRecordingMemory(){++test::samples;}
constexpr uint32_t kVoiceVadRmsStart=100,kVoiceVadPeakStart=200,kVoiceVadMinActiveSamples=100;
constexpr uint32_t kVoiceInputSampleRate=16000,kOfficialVoiceHeapCaps=3;
size_t heap_caps_get_free_size(uint32_t){return test::freeHeap;}
size_t heap_caps_get_largest_free_block(uint32_t){return test::largestHeap;}
uint32_t esp_random(){return 123;}
uint32_t millis(){return 1000;}
std::unique_ptr<official_voice_job::Context> officialVoiceContext(){
 if(!test::contextAvailable)return {};return std::unique_ptr<official_voice_job::Context>(new official_voice_job::Context);
}
size_t officialRecordingLastPcmBytes=0,officialRecordingLastEncodedBytes=0,officialRecordingWorkerFree=0,officialRecordingWorkerLargest=0;
uint32_t voiceRecordPeak=0,voiceRecordRms=0,voiceRecordActiveSamples=0,officialVoiceEpoch=0,officialEpoch=7,mcuInteractionUpdatedAtMs=25;
uint8_t officialRecordingAdmissionError=0;
uint8_t officialRecordingStartResult=0,officialRecordingStartStage=0;
official_voice_job::StartDiagnostics officialRecordingStartDiagnostics;
bool voiceRecordHeardSpeech=false,officialVoiceCleanupPending=false,mcuInteractionActive=true,audioBusy=true,officialVoiceCancelled=true,oledDirty=false;
std::string mcuInteractionStatus="listening",notice;
void setChatNotice(const char* value){notice=value?value:"";oledDirty=true;}
struct SerialStub {template<class... T>void printf(const char*,T...){}void println(const char*){}};
[[maybe_unused]] SerialStub Serial;
'''
cases=r'''
void reset(){
 officialVoiceJob.held.reset();officialRecordingStore.owner.reset();assert(!test::blocks&&!test::readers);
 test::startCalls=test::acceptedJobs=test::micEnds=test::audioClears=test::samples=0;
 test::contextAvailable=test::readerAvailable=true;test::readerError=false;test::freeHeap=test::largestHeap=500000;
 test::selectedStackBytes=16*1024;
 officialRecordingStore.owner=std::make_shared<Block>();officialRecordingStore.finishResult=ram_recording::Error::Ok;
 officialRecording=OfficialRecordingState{};officialRecording.active=officialRecording.ready=officialRecording.captureStarted=officialRecording.storeOwned=officialRecording.heardSpeech=true;
 officialRecording.takeId=42;officialRecording.peak=2000;officialRecording.frames=officialRecording.activeSamples=16000;officialRecording.squares=16000000000ULL;
 captureFixture=Capture{};officialVoiceJob.result=official_voice_job::StartResult::Started;
 officialVoiceCleanupPending=false;mcuInteractionActive=audioBusy=officialVoiceCancelled=true;oledDirty=false;
 mcuInteractionStatus="listening";mcuInteractionUpdatedAtMs=25;officialRecordingAdmissionError=0;notice.clear();
 officialRecordingStartResult=officialRecordingStartStage=0;officialRecordingStartDiagnostics={};officialVoiceJob.diagnostics={};
}
void failedTake(int attempts){
 assert(!officialRecording.active&&!audioBusy&&!mcuInteractionActive);
 if(mcuInteractionStatus=="listening"||mcuInteractionStatus=="recording")
  std::fprintf(stderr,"recording finished but stale status=%s; accepted_jobs=%d\n",mcuInteractionStatus.c_str(),test::acceptedJobs);
 assert(mcuInteractionStatus!="listening"&&mcuInteractionStatus!="recording"&&mcuInteractionStatus!="transcribing");
 assert(test::startCalls==attempts&&test::acceptedJobs==0&&test::readers==0);
 assert(mcuInteractionUpdatedAtMs==millis());
 if(attempts){
  assert(officialRecordingStartResult==static_cast<uint8_t>(officialVoiceJob.result));
  assert(officialRecordingStartStage==static_cast<uint8_t>(officialVoiceJob.lastStartStage()));
  assert(officialRecordingStartDiagnostics.beforeStateFreeBytes==123456&&officialRecordingStartDiagnostics.beforeTaskFreeBytes==65432);
 }
 assert(test::micEnds==1&&test::audioClears==1&&!notice.empty());
 // Local Reader ownership must unwind before this later main-loop cleanup.
 if(test::blocks)assert(officialVoiceCleanupPending);
 cleanupTickUnderTest();assert(test::blocks==0&&!officialVoiceCleanupPending);
 const auto calls=test::startCalls;finishOfficialRecording(true);assert(test::startCalls==calls); // Inactive finish is idempotent.
}
int main(){
 reset();officialVoiceJob.result=official_voice_job::StartResult::NoMemory;finishOfficialRecording(true,"已达录音时长上限，发送已录内容");failedTake(1);assert(notice=="语音识别内存不足，本次未发送");
 reset();officialVoiceJob.result=official_voice_job::StartResult::Busy;finishOfficialRecording(true,"已达录音时长上限，发送已录内容");failedTake(1);assert(notice=="识别任务暂不可用，本次语音未发送");
 reset();finishOfficialRecording(false,"录音已取消，本次未发送");failedTake(0);
 reset();officialRecording.heardSpeech=false;officialRecording.peak=officialRecording.activeSamples=0;officialRecording.squares=0;finishOfficialRecording(true);failedTake(0);
 reset();officialRecordingStore.finishResult=ram_recording::Error::InvalidState;finishOfficialRecording(true,"已达录音时长上限，发送已录内容");failedTake(0);assert(notice=="录音保存失败，本次未发送");
 reset();test::contextAvailable=false;finishOfficialRecording(true);failedTake(0);
 reset();test::readerAvailable=false;finishOfficialRecording(true);failedTake(0);
 reset();test::readerError=true;finishOfficialRecording(true);failedTake(0);
 reset();test::freeHeap=0;finishOfficialRecording(true);failedTake(0);
 reset();test::largestHeap=0;finishOfficialRecording(true);failedTake(0);
 reset();captureFixture.failed=true;finishOfficialRecording(true,"已达录音时长上限，发送已录内容");failedTake(0);assert(notice=="录音缓冲溢出或采集失败，本次未发送");
 reset();captureFixture.storageRetained=true;captureFixture.bufferBytes=32768;finishOfficialRecording(true);failedTake(0);
 assert(lastRecordingTake.storageRetained&&recordingCaptureForHealth().storageRetained&&recordingCaptureForHealth().bufferBytes==32768);
 reset();finishOfficialRecording(true);
 assert(!officialRecording.active&&audioBusy&&mcuInteractionActive&&mcuInteractionStatus=="transcribing");
 assert(test::startCalls==1&&test::acceptedJobs==1&&test::readers==1&&test::blocks==1&&officialVoiceCleanupPending);
 assert(officialVoiceEpoch==officialEpoch&&!officialVoiceCancelled&&mcuInteractionUpdatedAtMs==millis());
 cleanupTickUnderTest();assert(test::blocks==1&&officialVoiceCleanupPending); // Worker still owns its Reader.
 officialVoiceJob.held.reset();cleanupTickUnderTest();assert(!test::readers&&!test::blocks&&!officialVoiceCleanupPending);
 // Admission must use the Job API's selected budget rather than a fixed 32 KiB maximum.
 reset();test::freeHeap=40000;test::largestHeap=20000;finishOfficialRecording(true);
 assert(mcuInteractionStatus=="transcribing"&&test::acceptedJobs==1&&officialRecordingStartDiagnostics.taskStackBytes==16384);
 officialVoiceJob.held.reset();cleanupTickUnderTest();assert(!test::blocks&&!test::readers);
 reset();test::freeHeap=40000;test::largestHeap=20000;test::selectedStackBytes=32*1024;finishOfficialRecording(true);failedTake(0);
 std::puts("Actual finishOfficialRecording: 15 scenarios passed; rejected takes leave listening, accept no job, and release readers before later cleanup; selected Job stack/diagnostics project correctly (host boundaries only)");
}
'''
with tempfile.TemporaryDirectory(prefix='cardputer-finish-recording-') as temporary:
    source=Path(temporary)/'finish.cpp';source.write_text(prelude+recording_state+'\n'+finish+'\nvoid cleanupTickUnderTest(){\n'+cleanup+'\n}\n'+cases)
    binary=Path(temporary)/'finish'
    subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(firmware/'include'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

# Exercise the actual production start/tick/finish/cancel entrypoints. Only
# time, buttons, the SDK join and STT job boundary are doubled. PCM queue leases
# and compressed storage/decoding below are the real production classes.
a=main.index('void cancelOfficialVoice(const char* notice) {')
cancel=main[a:main.index('\nstd::unique_ptr<official_voice_job::Context> officialVoiceContext()',a)]
a=main.index('void startOfficialRecording() {')
start_and_tick=main[a:main.index('\nvoid armOfficialSpeech()',a)]
lifecycle_prelude=r'''
#include "RamRecordingStore.h"
#include "MicCaptureQueue.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
struct String:std::string {
 using std::string::string;String(const std::string& s):std::string(s){}
 String(double n,unsigned):std::string(std::to_string(n)){}
};
#define F(value) value
namespace test {
uint32_t now=100,setupMs=0,keys=0,nav=0;bool pressed=true,context=true,online=true;
int stopCalls=0,sttCalls=0;size_t uploadedBytes=0;
std::unique_ptr<mic_capture::Queue> queue;mic_capture::Claim pending;
bool captureActive=false;
}
uint32_t millis(){return test::now;}
uint32_t esp_random(){return 123;}
constexpr int WL_CONNECTED=3;struct {int status(){return test::online?WL_CONNECTED:0;}} WiFi;
struct CardputerCaptureStats {
 bool active=false,failed=false,storageRetained=false;uint32_t sampleRate=0,bufferBytes=0;
 mic_capture::Error error=mic_capture::Error::None;mic_capture::Stats queue;
} lastCaptureStats;
CardputerCaptureStats cardputerCaptureStats(){
 if(!test::queue)return lastCaptureStats;
 CardputerCaptureStats s;s.active=test::captureActive;s.failed=test::queue->failed();s.error=test::queue->error();
 s.storageRetained=!s.active;s.queue=test::queue->stats();s.sampleRate=16000;return s;
}
void cardputerCaptureStop(){
 if(!test::queue||!test::captureActive)return;
 assert(!test::queue->hasLease());++test::stopCalls;test::captureActive=false;test::queue->requestStop();
 test::pending={}; // SDK join drops its unfinished request; no fake completion.
}
bool cardputerCaptureEnd(){
 if(!test::queue)return true;cardputerCaptureStop();lastCaptureStats=cardputerCaptureStats();
 assert(test::queue->resetAfterJoin());lastCaptureStats.queue=test::queue->stats();
 lastCaptureStats.storageRetained=false;test::queue.reset();return true;
}
void cardputerAudioClear(){assert(cardputerCaptureEnd());}
bool cardputerCaptureBegin(uint32_t rate){
 assert(rate==16000);cardputerCaptureEnd();test::now+=test::setupMs;
 test::queue.reset(new mic_capture::Queue);test::pending=test::queue->claim();test::captureActive=true;return true;
}
mic_capture::Lease cardputerCaptureTake(){return test::queue?test::queue->take():mic_capture::Lease{};}
void produce(int16_t value=1200){
 assert(test::queue&&test::pending&&test::captureActive);
 std::fill(test::pending.data,test::pending.data+test::pending.samples,value);
 assert(test::queue->completeExactPointer(test::pending.data,test::pending.samples));
 test::pending=test::queue->claim();
}
void cardputerTickKeyClick(bool){}void cardputerSuppressGestureUntilReleased(){}
struct Mic {void end(){}};struct {Mic Mic;} M5Cardputer;
uint32_t cardputerChatKeyCount(){return test::keys;}uint32_t cardputerNavigationCount(){return test::nav;}
bool cardputerTalkPressed(){return test::pressed;}
bool chatPageOpen(){return test::context;}bool officialStudioDirect(){return true;}
bool officialChatContextActivated(){return test::context;}bool setI2sSampleRate(uint32_t n){return n==16000;}
void resetListeningMonitor(){}void sampleOfficialRecordingMemory(){}
constexpr uint32_t kVoiceInputSampleRate=16000,kOfficialVoiceHeapCaps=3,MALLOC_CAP_DMA=4;
constexpr uint32_t kVoiceRecordMinMs=300,kVoiceStreamReleaseDebounceMs=240;
constexpr uint32_t kVoiceRecordLimitMs=5000,kVoiceRecordDrainTimeoutMs=1000;
constexpr uint32_t kVoiceVadRmsStart=190,kVoiceVadPeakStart=480,kVoiceVadMinActiveSamples=16,kVoiceVadActiveThreshold=260;
constexpr uint32_t kListeningVadRmsStart=420,kListeningVadPeakStart=1200,kListeningVadMinActiveSamples=24,kListeningVadActiveThreshold=520;
size_t heap_caps_get_free_size(uint32_t){return 500000;}size_t heap_caps_get_largest_free_block(uint32_t){return 500000;}
namespace official_voice_job {
enum class Type:uint8_t {Stt,Tts};
enum class StartResult:uint8_t {Started,Busy,Invalid,NoMemory,NotSupported};
enum class StartStage:uint8_t {None,Validation,StateAllocation,MutexCreation,SignalCreation,TaskCreation,Started};
struct StartDiagnostics {uint32_t taskStackBytes=16384;};
struct Context {};
struct Output {
 struct Result {uint8_t error=0;int httpStatus=0;} result;
 Type type=Type::Stt;bool resourcesAvailable=false;
 uint32_t workerStackFreeBytes=0,internalHeapMinObservedBytes=0,internalLargestBlockMinObservedBytes=0,systemHeapLowWaterBytes=0;
};
uint32_t taskStackBytes(Type,const Context&){return 16384;}
struct Job {static size_t fixedJobHeapBytes(){return 1024;}};
}
struct VoiceJob {
 std::unique_ptr<official_voice::Reader> reader;
 bool busy()const{return bool(reader);}void cancel(){}
 official_voice_job::StartStage lastStartStage()const{return official_voice_job::StartStage::Started;}
 official_voice_job::StartDiagnostics lastStartDiagnostics()const{return {};}
 official_voice_job::StartResult startStt(const official_voice_job::Context&,std::unique_ptr<official_voice::Reader> input,
     size_t bytes,uint32_t rate,uint16_t channels,uint64_t){
   assert(rate==16000&&channels==1&&!test::captureActive&&!test::queue);
   ++test::sttCalls;test::uploadedBytes=bytes;reader=std::move(input);return official_voice_job::StartResult::Started;
 }
} officialVoiceJob;
struct IdleJob {bool busy()const{return false;}void cancel(){}} studioAuthJob,studioConnectJob,officialSelectionJob,officialHistoryJob;
struct {uint32_t version=1;} agentSelection;
struct {bool ready()const{return true;}} chatModel;
struct Plan {void cancel(){}} officialSpeechPlan;
ram_recording::Store officialRecordingStore;
ram_recording::Error officialRecordingPrepareResult=ram_recording::Error::Ok,officialRecordingBeginResult=ram_recording::Error::Ok;
bool officialSpeechNeedsFreshTotal=false,officialHistoryForSpeech=false,officialVoiceCancelled=false,officialRecordingRequested=false;
bool officialVoiceDrainPending=false,officialVoiceOutputPlaying=false,officialVoiceCleanupPending=false;
bool chatRemoteRunActive=false,officialResumePending=false,officialResumeQueued=false,officialHistoryQueued=false;
bool audioBusy=false,mcuInteractionActive=false,mcuSocketNamespaceReady=true,wifiReady=true,chatSendUnknown=false,i2sReady=true,es8311Ready=true,oledDirty=false;
uint32_t officialVoiceRate=0,officialVoiceTailFrames=0,officialVoiceDrainAtMs=0,officialVoiceTailAtMs=0;
uint32_t officialEpoch=1,officialVoiceEpoch=0,chatHistoryLoadedVersion=1,mcuInteractionUpdatedAtMs=0;
uint32_t officialRecordingHeapStart=0,officialRecordingHeapMinimum=0,officialRecordingLargestMinimum=0,officialRecordingWorkerFree=0,officialRecordingWorkerLargest=0;
uint32_t voiceRecordPeak=0,voiceRecordRms=0,voiceRecordActiveSamples=0;bool voiceRecordHeardSpeech=false;
size_t officialRecordingLastPcmBytes=0,officialRecordingLastEncodedBytes=0;
uint8_t officialRecordingAdmissionError=0,officialRecordingStartResult=0,officialRecordingStartStage=0;
official_voice_job::StartDiagnostics officialRecordingStartDiagnostics;
std::string chatPendingSend,mcuInteractionStatus,notice;
void setChatNotice(const std::string& s){notice=s;}
void officialControlEvent(const char*){}
struct SerialStub {template<class... T>void printf(const char*,T...){}} Serial;
std::unique_ptr<official_voice_job::Context> officialVoiceContext(){return std::unique_ptr<official_voice_job::Context>(new official_voice_job::Context);}
enum class VoiceInputChannel:uint8_t {Undecided,Left,Right,Mixed};
uint16_t sampleMagnitude(int16_t n){return uint16_t(n<0?-int(n):n);}
void updateVoiceInputChannel(VoiceInputChannel& channel,const int16_t*,size_t){channel=VoiceInputChannel::Left;}
int16_t voiceInputMonoSample(int16_t left,int16_t,VoiceInputChannel){return left;}
bool officialVoiceNetworkBusy();
void cancelOfficialVoice(const char* notice=nullptr);
'''
lifecycle_cases=r'''
void resetTake(uint32_t now=100){
 officialVoiceJob.reader.reset();cardputerCaptureEnd();assert(officialRecordingStore.discard()==ram_recording::Error::Ok);
 officialRecording={};test::now=now;test::setupMs=0;test::keys=test::nav=0;test::pressed=test::context=test::online=true;
 test::stopCalls=test::sttCalls=0;test::uploadedBytes=0;lastCaptureStats={};
 officialVoiceCleanupPending=officialRecordingRequested=officialVoiceCancelled=officialResumeQueued=officialResumePending=chatRemoteRunActive=false;
 chatPendingSend.clear();chatSendUnknown=false;notice.clear();
}
void drain(){for(int n=0;officialRecording.active&&n<20;++n)tickOfficialRecording();assert(!officialRecording.active);}
void assertDecodedBytes(size_t expected){
 assert(officialVoiceJob.reader);std::vector<uint8_t> decoded;uint8_t bytes[4097];
 for(size_t n;(n=officialVoiceJob.reader->read(bytes,sizeof(bytes)))!=0;)decoded.insert(decoded.end(),bytes,bytes+n);
 assert(decoded.size()==expected&&std::any_of(decoded.begin(),decoded.end(),[](uint8_t n){return n!=0;}));
}
int main(){
 assert(kVoiceRecordLimitMs==5000&&kVoiceRecordDrainTimeoutMs==1000);
 resetTake();startOfficialRecording();const auto start=test::now;
 for(int n=0;n<50;++n){produce();test::now=start+uint32_t(n)*60;tickOfficialRecording();}
 test::now=start+4999;tickOfficialRecording();assert(test::stopCalls==0&&officialRecording.active);
 test::now=start+5000;tickOfficialRecording();
 if(test::stopCalls!=1)std::fprintf(stderr,"RED: actual tick did not stop capture at 5000ms; frames=%u\n",officialRecording.frames);
 assert(test::stopCalls==1);drain();
 assert(test::sttCalls==1&&test::uploadedBytes==102400);
 assert(officialRecordingLastPcmBytes==102400&&officialRecordingLastEncodedBytes==25600);
 assert(lastRecordingTake.reason==RecordingStopReason::WallLimit&&lastRecordingTake.wallMs==5000&&lastRecordingTake.frames==51200);
 assert(lastRecordingTake.outcome==RecordingOutcome::Complete&&lastRecordingTake.sttStarted);
 assert(lastRecordingTake.produced==50&&lastRecordingTake.consumed==50&&lastRecordingTake.discarded==1);
 assertDecodedBytes(102400);const auto completedId=lastRecordingTake.takeId;
 // Codec startup (even >5s in this SDK double) is not capture time. Starting
 // the next take preserves the last completed take until it actually finishes.
 resetTake(10);test::setupMs=6200;startOfficialRecording();const auto ready=test::now;
 assert(officialRecording.startedAt==ready&&lastRecordingTake.takeId==completedId);
 produce();tickOfficialRecording();test::now=ready+4999;tickOfficialRecording();assert(!test::stopCalls);
 test::now=ready+5000;tickOfficialRecording();drain();assert(lastRecordingTake.wallMs==5000&&test::uploadedBytes==2048);
 // Stop joins the producer, then drains all complete Ready blocks. The last
 // queued SDK request has no completion callback and never enters the Store.
 resetTake();startOfficialRecording();const auto drainStart=test::now;
 for(int n=0;n<6;++n)produce();
 test::now=drainStart+5000;tickOfficialRecording();
 assert(test::stopCalls==1&&officialRecording.stopping&&officialRecording.active&&test::sttCalls==0);
 drain();assert(test::stopCalls==1&&test::sttCalls==1&&test::uploadedBytes==6*2048);
 assert(lastRecordingTake.produced==6&&lastRecordingTake.consumed==6&&lastRecordingTake.discarded==1);
 assertDecodedBytes(6*2048);
 // The 79th full source block can contribute only 128 remaining mono frames.
 resetTake();startOfficialRecording();const auto sampleStart=test::now;
 for(int n=0;n<79;++n){produce();test::now=sampleStart+uint32_t(n+1)*60;tickOfficialRecording();}
 assert(officialRecording.frames==80000&&officialRecording.stopping&&test::sttCalls==0);
 drain();assert(test::uploadedBytes==160000&&officialRecordingLastEncodedBytes==40000);
 assert(lastRecordingTake.reason==RecordingStopReason::SampleLimit&&lastRecordingTake.consumed==79);
 assertDecodedBytes(160000);tickOfficialRecording();assert(test::sttCalls==1);
 // A real 5s wall deadline can validly retain 78 complete 64ms blocks. The
 // unfinished block is dropped; there is no manufactured tail to reach 80k.
 resetTake();startOfficialRecording();const auto nearFullStart=test::now;
 for(int n=0;n<78;++n){produce();test::now=nearFullStart+uint32_t(n+1)*64;tickOfficialRecording();}
 assert(!test::stopCalls&&officialRecording.frames==79872);test::now=nearFullStart+5000;tickOfficialRecording();drain();
 assert(test::uploadedBytes==159744&&lastRecordingTake.encodedBytes==39936&&lastRecordingTake.wallMs==5000);
 assert(lastRecordingTake.reason==RecordingStopReason::WallLimit);assertDecodedBytes(159744);
 // Ordinary early release remains a release, including its bounded debounce.
 resetTake();startOfficialRecording();const auto releaseStart=test::now;
 for(int n=0;n<4;++n){produce();tickOfficialRecording();}
 test::pressed=false;test::now=releaseStart+3000;tickOfficialRecording();assert(!test::stopCalls);
 test::now+=239;tickOfficialRecording();assert(!test::stopCalls);++test::now;tickOfficialRecording();drain();
 assert(lastRecordingTake.reason==RecordingStopReason::Released&&lastRecordingTake.wallMs==3240&&test::uploadedBytes==8192);
 // Cancellation wins even when the exact wall deadline was reached.
 resetTake();startOfficialRecording();const auto cancelStart=test::now;produce();tickOfficialRecording();
 ++test::keys;test::now=cancelStart+5000;tickOfficialRecording();
 assert(!officialRecording.active&&test::sttCalls==0&&lastRecordingTake.reason==RecordingStopReason::Cancelled);
 assert(lastRecordingTake.outcome==RecordingOutcome::Cancelled&&lastRecordingTake.pcmBytes==2048);
 resetTake();startOfficialRecording();const auto drainCancelStart=test::now;for(int n=0;n<6;++n)produce();
 test::now=drainCancelStart+5000;tickOfficialRecording();++test::nav;tickOfficialRecording();
 assert(test::sttCalls==0&&lastRecordingTake.outcome==RecordingOutcome::Cancelled&&lastRecordingTake.wallMs==5000);
 // An explicit Esc/context cancellation snapshots THIS take before discarding
 // it; a later raw hardware snapshot cannot replace the correlated counters.
 resetTake();startOfficialRecording();produce();produce();cancelOfficialVoice();
 assert(test::sttCalls==0&&lastRecordingTake.outcome==RecordingOutcome::Cancelled);
 assert(lastRecordingTake.produced==2&&lastRecordingTake.consumed==0&&lastRecordingTake.discarded==3);
 lastCaptureStats.queue.producedBlocks=999;assert(recordingCaptureForHealth().queue.producedBlocks==2);
 // Queue failure makes the entire take unsendable, even if a prefix existed
 // and the wall deadline happens in the same tick.
 resetTake();startOfficialRecording();const auto badStart=test::now;produce();tickOfficialRecording();
 for(int n=0;n<8;++n)produce();assert(test::queue->failed());test::now=badStart+5000;tickOfficialRecording();
 assert(!officialRecording.active&&test::sttCalls==0&&lastRecordingTake.reason==RecordingStopReason::CaptureFailed);
 assert(lastRecordingTake.outcome==RecordingOutcome::Failed&&lastRecordingTake.overruns==1&&lastRecordingTake.pcmBytes==2048);
 // Drain has its own deadline. It neither extends capture wall time nor sends
 // an incomplete prefix when the application cannot finish draining in time.
 resetTake();startOfficialRecording();const auto timeoutStart=test::now;
 for(int n=0;n<3;++n)produce();test::now=timeoutStart+5000;tickOfficialRecording();
 test::now+=kVoiceRecordDrainTimeoutMs;tickOfficialRecording();
 assert(!officialRecording.active&&test::sttCalls==0&&lastRecordingTake.reason==RecordingStopReason::DrainTimeout&&lastRecordingTake.wallMs==5000);
 // Silent complete blocks must not inherit the successful-limit notice.
 resetTake();startOfficialRecording();const auto silentStart=test::now;produce(0);tickOfficialRecording();
 test::now=silentStart+5000;tickOfficialRecording();drain();
 assert(test::sttCalls==0&&lastRecordingTake.reason==RecordingStopReason::NoSpeech&&notice=="未检测到清晰声音，本次未发送");
 // All deadline deltas are modulo 2^32; zero is a valid stop/release time.
 resetTake(UINT32_MAX-4999);startOfficialRecording();const auto wrapStart=test::now;produce();tickOfficialRecording();
 test::now=wrapStart+4999;tickOfficialRecording();assert(!test::stopCalls);++test::now;assert(test::now==0);
 tickOfficialRecording();drain();assert(lastRecordingTake.wallMs==5000&&lastRecordingTake.reason==RecordingStopReason::WallLimit);
 resetTake(UINT32_MAX-999);startOfficialRecording();produce();tickOfficialRecording();
 test::pressed=false;test::now=0;tickOfficialRecording();assert(!test::stopCalls);
 test::now=239;tickOfficialRecording();assert(!test::stopCalls);test::now=240;tickOfficialRecording();drain();
 assert(lastRecordingTake.reason==RecordingStopReason::Released&&lastRecordingTake.wallMs==1240);
 // STT numeric watermarks survive TTS and reject stale/cancelled completions.
 official_voice_job::Output metrics;metrics.type=official_voice_job::Type::Stt;metrics.result.httpStatus=200;
 metrics.resourcesAvailable=true;metrics.workerStackFreeBytes=1234;metrics.internalHeapMinObservedBytes=4567;
 metrics.internalLargestBlockMinObservedBytes=2345;metrics.systemHeapLowWaterBytes=3456;
 latchOfficialSttMetrics(metrics,true);const auto sttId=lastSttMetrics.takeId;
 assert(sttId==lastRecordingTake.takeId&&lastSttMetrics.stackFree==1234&&lastSttMetrics.present&&lastSttMetrics.available);
 metrics.type=official_voice_job::Type::Tts;metrics.workerStackFreeBytes=9999;latchOfficialSttMetrics(metrics,true);
 assert(lastSttMetrics.takeId==sttId&&lastSttMetrics.stackFree==1234);
 metrics.type=official_voice_job::Type::Stt;latchOfficialSttMetrics(metrics,false);assert(lastSttMetrics.stackFree==1234);
 resetTake();startOfficialRecording();assert(lastRecordingTake.takeId==sttId&&lastSttMetrics.takeId==sttId);cancelOfficialVoice();
 assert(lastRecordingTake.takeId!=sttId&&lastSttMetrics.takeId==sttId);
 resetTake();
 assert(sizeof(RecordingTakeSnapshot)<=64&&sizeof(SttMetricsSnapshot)<=32);
 std::puts("Actual recording lifecycle: ready clock, 4999/5000ms stop/drain, 50-block prefix, unfinished SDK block, exact 80k cap, release/cancel/failure, silence, wraparound, correlated take/STT snapshots passed (host boundaries only)");
}
'''
with tempfile.TemporaryDirectory(prefix='cardputer-recording-lifecycle-') as temporary:
    source=Path(temporary)/'lifecycle.cpp'
    source.write_text(lifecycle_prelude+recording_state+'\nbool officialVoiceNetworkBusy(){return officialRecording.active || officialVoiceJob.busy() || officialVoiceDrainPending || officialVoiceOutputPlaying;}\n'+cancel+'\n'+finish+'\n'+start_and_tick+'\n'+lifecycle_cases)
    binary=Path(temporary)/'lifecycle'
    subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(firmware/'include'),str(source),str(firmware/'src/RamRecordingStore.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
