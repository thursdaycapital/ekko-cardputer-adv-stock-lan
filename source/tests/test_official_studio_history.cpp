#include "OfficialStudioHistory.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>

namespace {
std::atomic<int> failAfter{-1};
bool allocationFails() {
  int count=failAfter.load();
  if(count<0)return false;
  if(!count)return true;
  failAfter.store(count-1);return false;
}
}
void* operator new(std::size_t bytes,const std::nothrow_t&)noexcept {
  if(allocationFails())return nullptr;
  try{return ::operator new(bytes);}catch(...){return nullptr;}
}
void* operator new[](std::size_t bytes,const std::nothrow_t&)noexcept {
  if(allocationFails())return nullptr;
  try{return ::operator new[](bytes);}catch(...){return nullptr;}
}
void operator delete(void* pointer,const std::nothrow_t&)noexcept{::operator delete(pointer);}
void operator delete[](void* pointer,const std::nothrow_t&)noexcept{::operator delete[](pointer);}

namespace {
using namespace official_history;
using Clock=std::chrono::steady_clock;
std::string quote(const std::string& input) {
  std::string output="\"";static const char hex[]="0123456789abcdef";
  for(uint8_t byte:input) {
    if(byte=='"'||byte=='\\'){output+='\\';output+=char(byte);}
    else if(byte<0x20){output+="\\u00";output+=hex[byte>>4];output+=hex[byte&15];}
    else output+=char(byte);
  }
  return output+'"';
}
Input input(uint32_t offset=0,uint32_t byteOffset=0,const char* session="session-1",const char* profile="default") {
  Input value;
  assert(makeInput(value,"http://127.0.0.1:19036","test.jwt-fixture",profile,session,27,offset,byteOffset)==Result::Ok);
  return value;
}
std::string page(const std::string& text="你好",uint32_t total=1,uint32_t offset=0,
                 const std::string& session="session-1",const std::string& profile="default") {
  return "{\"session\":{\"id\":"+quote(session)+",\"profile\":"+quote(profile)+"},\"messages\":[{\"id\":9007199254740993,\"role\":\"assistant\",\"content\":"+
      quote(text)+"}],\"total\":"+std::to_string(total)+",\"offset\":"+std::to_string(offset)+
      ",\"limit\":12,\"hasMore\":"+(uint64_t(offset)+1<total?"true":"false")+"}";
}
std::string replace(std::string text,const std::string& from,const std::string& to) {
  const auto at=text.find(from);assert(at!=std::string::npos);text.replace(at,from.size(),to);return text;
}
class Flag final:public Cancellation {
 public:std::atomic<bool> value{false};bool cancelled()const override{return value.load();}
};
class Fixture final:public Transport {
 public:
  explicit Fixture(std::string body):body(std::move(body)){}
  std::string body,path;size_t step=7,limit=SIZE_MAX;int status=200,calls=0;
  const char* type="application/json; charset=utf-8";const char* encoding=nullptr;
  int64_t declared=-2;bool complete=true,duplicateBegin=false;
  Flag* cancelAtEnd=nullptr;
  bool get(const Request& request,ResponseSink& sink)override {
    ++calls;path=request.path;
    assert(request.timeoutMs==8000&&request.input&&validInput(*request.input));
    Headers headers;headers.status=status;headers.contentType=type;headers.contentEncoding=encoding;
    headers.contentLength=declared==-2?int64_t(body.size()):declared;
    if(!sink.begin(headers))return false;
    if(duplicateBegin&&!sink.begin(headers))return false;
    const size_t end=std::min(body.size(),limit);
    for(size_t at=0;at<end;) {
      const size_t count=std::min(step,end-at);
      if(!sink.write(reinterpret_cast<const uint8_t*>(body.data()+at),count))return false;
      at+=count;
    }
    if(cancelAtEnd)cancelAtEnd->value=true;
    return complete;
  }
};
void validPages() {
  auto request=input();
  for(size_t step:{size_t(1),size_t(2),size_t(7),size_t(113),size_t(65536)}) {
    Fixture fixture(page());fixture.step=step;
    auto result=resolve(request,fixture);
    assert(result.result==Result::Ok&&result.httpCode==200&&result.snapshot);
    assert(result.epoch==27&&result.offset==0&&result.byteOffset==0);
    assert(!std::strcmp(result.profile.data(),"default")&&!std::strcmp(result.session.data(),"session-1"));
    assert(result.snapshot->count==1&&result.snapshot->rows==1&&result.snapshot->total==1);
    assert(!std::strcmp(result.snapshot->messages[0].id.data(),"9007199254740993"));
    assert(!std::strcmp(result.snapshot->messages[0].content.text.data(),"你好"));
    assert(fixture.path=="/api/studio/sessions/conversations/session-1/messages/paginated?limit=12&offset=0&profile=default");
  }
  auto older=input(4);
  Fixture olderPage(page("old",40,4));olderPage.declared=-1;
  auto result=resolve(older,olderPage);
  assert(result.result==Result::Ok&&result.snapshot->hasMore&&result.offset==4);
  const char* session="sess/中 ?&%#";const char* profile="项 目/&";
  auto encoded=input(UINT32_MAX,0,session,profile);
  const std::string empty="{\"session\":{\"id\":"+quote(session)+",\"profile\":"+quote(profile)+
      "},\"messages\":[],\"total\":0,\"offset\":4294967295,\"hasMore\":false}";
  Fixture unicode(empty);result=resolve(encoded,unicode);
  assert(result.result==Result::Ok&&result.snapshot&&result.snapshot->count==0);
  assert(unicode.path=="/api/studio/sessions/conversations/sess%2F%E4%B8%AD%20%3F%26%25%23/messages/paginated?limit=12&offset=4294967295&profile=%E9%A1%B9%20%E7%9B%AE%2F%26");
  Fixture mixed("{\"session\":{\"id\":\"session-1\",\"profile\":\"default\"},\"messages\":[{\"id\":1,\"role\":\"tool\",\"content\":\"tool\"},{\"id\":2,\"role\":\"assistant\",\"content\":\"answer\"}],\"total\":2,\"offset\":0,\"hasMore\":false}");
  result=resolve(request,mixed);
  assert(result.result==Result::Ok&&result.snapshot->rows==2&&result.snapshot->count==1);
  assert(result.snapshot->messages[0].index==1);
  Fixture capitals(page());capitals.type="Application/JSON \t; charset=UTF-8";capitals.encoding="IDENTITY";
  assert(resolve(request,capitals).result==Result::Ok);
}
void invalidBodies() {
  const auto request=input();const auto good=page();
  const std::string invalid[]={
      good+" garbage",good+"{}",good.substr(0,good.size()-1),
      replace(good,"\"session-1\"","\"other\""),
      replace(good,"\"profile\":\"default\"","\"profile\":\"other\""),
      replace(good,"\"profile\":\"default\"","\"profile\":null"),
      replace(good,"\"profile\":\"default\"","\"profile\":\"default\",\"profile\":\"default\""),
      replace(good,"\"id\":\"session-1\"","\"id\":null"),
      replace(good,"\"id\":\"session-1\"","\"id\":\"session-1\",\"id\":\"session-1\""),
      replace(good,"\"total\":1,",""),replace(good,"\"offset\":0,",""),
      replace(good,"\"total\":1","\"total\":null"),replace(good,"\"total\":1","\"total\":1e0"),
      replace(good,"\"total\":1","\"total\":4294967296"),
      replace(good,"\"total\":1","\"total\":1,\"total\":1"),
      replace(good,"\"offset\":0","\"offset\":1"),replace(good,"\"offset\":0","\"offset\":false"),
      replace(good,"\"total\":1","\"total\":0"),
      replace(good,"\"hasMore\":false","\"hasMore\":true"),
      replace(good,"\"hasMore\":false","\"hasMore\":null"),
      replace(good,",\"hasMore\":false",""),
      replace(good,"\"messages\":[","\"messages\":{\"row\":"),
      "[]","null","{\"session\":{},\"messages\":[],\"total\":0,\"offset\":0,\"hasMore\":false}",
      "{\"session\":{\"id\":\"session-1\",\"profile\":\"default\"},\"messages\":[null],\"total\":1,\"offset\":0,\"hasMore\":false}",
      replace(good,"你好",std::string("\xc0\xaf",2)),
      replace(good,"session-1","session-1\\u0000suffix")};
  for(const auto& body:invalid)for(size_t step:{size_t(1),size_t(17),body.size()+1}) {
    Fixture fixture(body);fixture.step=step;
    const auto result=resolve(request,fixture);
    if(result.result!=Result::Invalid)std::fprintf(stderr,"Unexpected result %d: %.300s\n",int(result.result),body.c_str());
    assert(result.result==Result::Invalid&&!result.snapshot);
  }
  std::string tooMany="{\"session\":{\"id\":\"session-1\",\"profile\":\"default\"},\"messages\":[";
  for(size_t n=0;n<13;++n) {
    if(n)tooMany+=',';tooMany+="{\"id\":"+std::to_string(n)+",\"role\":\"tool\",\"content\":\"x\"}";
  }
  tooMany+="],\"total\":13,\"offset\":0,\"hasMore\":false}";
  Fixture excessive(tooMany);assert(resolve(request,excessive).result==Result::Invalid);
}
void transportErrors() {
  const auto request=input();
  for(auto entry:{std::make_pair(401,Result::AuthInvalid),std::make_pair(404,Result::Empty),
      std::make_pair(403,Result::Invalid),std::make_pair(302,Result::Invalid),std::make_pair(400,Result::Invalid),
      std::make_pair(-1,Result::Transient),std::make_pair(408,Result::Transient),std::make_pair(429,Result::Transient),
      std::make_pair(503,Result::Transient)}) {
    Fixture fixture("not JSON and no secret copied");fixture.status=entry.first;
    const auto result=resolve(request,fixture);
    assert(result.result==entry.second&&result.httpCode==entry.first&&!result.snapshot);
  }
  for(const char* type:{static_cast<const char*>(nullptr),"text/html","application/jsonp","application/jsonX"}) {
    Fixture fixture(page());fixture.type=type;const auto result=resolve(request,fixture);
    assert(result.result==Result::Invalid&&!result.snapshot);
  }
  Fixture gzip(page());gzip.encoding="gzip";assert(resolve(request,gzip).result==Result::Invalid);
  Fixture shortBody(page());shortBody.declared=int64_t(shortBody.body.size()+1);
  assert(resolve(request,shortBody).result==Result::Transient);
  Fixture longBody(page());longBody.declared=int64_t(longBody.body.size()-1);
  assert(resolve(request,longBody).result==Result::Invalid);
  Fixture interrupted(page());interrupted.complete=false;
  assert(resolve(request,interrupted).result==Result::Transient);
  Fixture prefix(page()+"trailing not received");prefix.limit=page().size();prefix.complete=false;
  const auto noCommit=resolve(request,prefix);assert(noCommit.result==Result::Transient&&!noCommit.snapshot);
  Fixture twice(page());twice.duplicateBegin=true;assert(resolve(request,twice).result==Result::Invalid);
  Flag flag;flag.value=true;Fixture unused(page());assert(resolve(request,unused,&flag).result==Result::Cancelled&&unused.calls==0);
  flag.value=false;Fixture late(page());late.cancelAtEnd=&flag;
  auto cancelled=resolve(request,late,&flag);assert(cancelled.result==Result::Cancelled&&!cancelled.snapshot);
}
void longMessages() {
  std::string original;
  for(size_t n=0;n<140000;++n)original+="A中😀";
  original.insert(1024,1,'\0');
  std::string shown=original;std::replace(shown.begin(),shown.end(),'\0',' ');
  for(uint32_t offset:{uint32_t(0),uint32_t(1023),uint32_t(524289),uint32_t(original.size()-100),uint32_t(original.size()+40)}) {
    auto request=input(0,offset);Fixture fixture(page(original));fixture.step=997;fixture.declared=-1;
    auto result=resolve(request,fixture);assert(result.result==Result::Ok&&result.snapshot);
    const auto& window=result.snapshot->messages[0].content;
    assert(window.total==original.size()&&window.closed&&window.bytes<=1024);
    assert(native_chat::validUtf8(window.text.data(),window.bytes));
    assert(std::string(window.text.data(),window.bytes)==shown.substr(window.start,window.bytes));
    assert(window.start<=std::min<size_t>(offset,original.size()));
    assert(std::min<size_t>(offset,original.size())-window.start<4);
  }
  // Non-display fields must still be fully parsed, without retaining their
  // size or treating an early complete display projection as a full document.
  auto request=input();Fixture unknown(replace(page(),"\"limit\":12","\"ignored\":"+quote(original)));
  unknown.step=4096;assert(resolve(request,unknown).result==Result::Ok);
}
void inputsAndMemory() {
  auto value=input();
  assert(makeInput(value,value.baseUrl.data(),value.userJwt.data(),value.session.data(),value.profile.data(),28,9,1024)==Result::Ok);
  assert(!std::strcmp(value.profile.data(),"session-1")&&!std::strcmp(value.session.data(),"default"));
  for(const char* base:{"ftp://host","http:///host","http://user@host","http://host?x","http://host#x","http://host\\evil","http://host\r\nX:"}) {
    assert(makeInput(value,base,"test.jwt","default","s",1)==Result::Invalid);
    assert(value.epoch==0&&!value.userJwt[0]&&!value.session[0]);
  }
  for(const char* text:{""," ","　"," ","\xef\xbb\xbf","bad\nvalue","\xc0\xaf"})
    assert(makeInput(value,"http://host","test.jwt",text,"s",1)==Result::Invalid);
  assert(makeInput(value,"http://host","jwt\r\nInjected","default","s",1)==Result::Invalid);
  assert(makeInput(value,"http://host","test.jwt","default","s",0)==Result::Invalid);
  const std::string hugeSession(225,'s');assert(makeInput(value,"http://host","test.jwt","default",hugeSession.c_str(),1)==Result::Invalid);
  auto request=input();
  for(int allocation=0;allocation<3;++allocation) {
    Fixture fixture(page());failAfter=allocation;auto result=resolve(request,fixture);failAfter=-1;
    assert(result.result==Result::NoMemory&&!result.snapshot&&fixture.calls==0);
  }
  failAfter=0;const auto failedInput=makeInput(value,"http://host","test.jwt","default","s",1);failAfter=-1;
  assert(failedInput==Result::NoMemory&&!value.userJwt[0]);
  Job job;failAfter=0;const auto failedJob=job.start(request,std::unique_ptr<Transport>(nullptr));failAfter=-1;
  assert(failedJob==StartResult::NotSupported&&!job.busy());
  std::unique_ptr<Transport> fixture(new Fixture(page()));failAfter=0;
  const auto noState=job.start(request,std::move(fixture));failAfter=-1;
  assert(noState==StartResult::NoMemory&&!job.busy());
  alignas(Input) unsigned char storage[sizeof(Input)];
  auto* privateInput=new(storage)Input;assert(makeInput(*privateInput,"http://host","test.jwt","default","s",1)==Result::Ok);
  const size_t jwtAt=size_t(reinterpret_cast<unsigned char*>(privateInput->userJwt.data())-storage);
  privateInput->~Input();for(size_t n=0;n<kJwtBytes+1;++n)assert(storage[jwtAt+n]==0);
}

struct Gate {
  std::mutex mutex;std::condition_variable changed;bool entered=false,proceed=false,destroyed=false;
  std::thread::id worker;
};
class Blocked final:public Transport {
 public:
  explicit Blocked(std::shared_ptr<Gate> gate):gate_(std::move(gate)){}
  ~Blocked()override {
    std::lock_guard<std::mutex> lock(gate_->mutex);gate_->destroyed=true;gate_->changed.notify_all();
  }
  bool get(const Request& request,ResponseSink& sink)override {
    {
      std::unique_lock<std::mutex> lock(gate_->mutex);gate_->entered=true;gate_->worker=std::this_thread::get_id();gate_->changed.notify_all();
      assert(gate_->changed.wait_for(lock,std::chrono::seconds(5),[this]{return gate_->proceed;}));
    }
    // The caller's Input/Job may already have been destroyed. State must still
    // own the complete request while this detached worker unwinds.
    assert(!std::strcmp(request.input->session.data(),"session-1"));
    Fixture fixture(page());return fixture.get(request,sink);
  }
 private:std::shared_ptr<Gate> gate_;
};
void awaitEntered(const std::shared_ptr<Gate>& gate) {
  std::unique_lock<std::mutex> lock(gate->mutex);
  assert(gate->changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate->entered;}));
  assert(gate->worker!=std::this_thread::get_id());
}
void release(const std::shared_ptr<Gate>& gate) {
  std::lock_guard<std::mutex> lock(gate->mutex);gate->proceed=true;gate->changed.notify_all();
}
void awaitDestroyed(const std::shared_ptr<Gate>& gate) {
  std::unique_lock<std::mutex> lock(gate->mutex);
  assert(gate->changed.wait_for(lock,std::chrono::seconds(5),[&]{return gate->destroyed;}));
}
Output awaitReady(Job& job) {
  Output result;const auto deadline=Clock::now()+std::chrono::seconds(5);
  while(job.poll(result)!=Poll::Ready) {
    assert(job.busy()&&Clock::now()<deadline);std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(!job.busy());return result;
}
void jobs() {
  auto request=input();Job job;Output output;
  assert(job.poll(output)==Poll::Idle&&job.start(request)==StartResult::NotSupported);
  auto gate=std::make_shared<Gate>();
  const auto started=Clock::now();assert(job.start(request,std::unique_ptr<Transport>(new Blocked(gate)))==StartResult::Started);
  assert(Clock::now()-started<std::chrono::milliseconds(500));
  awaitEntered(gate);assert(job.busy()&&job.poll(output)==Poll::Pending);
  assert(job.start(request)==StartResult::Busy);release(gate);
  output=awaitReady(job);assert(output.result==Result::Ok&&output.snapshot);awaitDestroyed(gate);
  assert(job.poll(output)==Poll::Idle);

  gate=std::make_shared<Gate>();assert(job.start(request,std::unique_ptr<Transport>(new Blocked(gate)))==StartResult::Started);
  awaitEntered(gate);job.cancel();assert(job.busy()&&job.poll(output)==Poll::Pending);release(gate);
  output=awaitReady(job);assert(output.result==Result::Cancelled&&!output.snapshot);awaitDestroyed(gate);

  // Cancel after parsing but before poll still cannot publish a stale page.
  gate=std::make_shared<Gate>();assert(job.start(request,std::unique_ptr<Transport>(new Blocked(gate)))==StartResult::Started);
  awaitEntered(gate);release(gate);awaitDestroyed(gate);job.cancel();output=awaitReady(job);
  assert(output.result==Result::Cancelled&&!output.snapshot);

  gate=std::make_shared<Gate>();
  {
    auto ephemeral=std::unique_ptr<Job>(new Job);auto ephemeralInput=std::unique_ptr<Input>(new Input(input()));
    assert(ephemeral->start(*ephemeralInput,std::unique_ptr<Transport>(new Blocked(gate)))==StartResult::Started);
    awaitEntered(gate);ephemeralInput.reset();
    const auto destroying=Clock::now();ephemeral.reset();assert(Clock::now()-destroying<std::chrono::milliseconds(500));
  }
  release(gate);awaitDestroyed(gate);
  for(size_t n=0;n<50;++n) {
    assert(job.start(request,std::unique_ptr<Transport>(new Fixture(page())))==StartResult::Started);
    output=awaitReady(job);assert(output.result==Result::Ok&&output.snapshot);
  }
}
} // namespace
int main() {
  validPages();invalidBodies();transportErrors();longMessages();inputsAndMemory();jobs();
  std::printf("Official Studio history tests passed; Input=%zu Output=%zu Snapshot=%zu Reader=%zu bytes\n",
      sizeof(Input),sizeof(Output),sizeof(Snapshot),sizeof(bounded_json::Reader<32,256,64>));
}
