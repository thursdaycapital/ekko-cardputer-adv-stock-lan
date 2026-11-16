#include "StudioAuthJob.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

using namespace studio_auth;
struct Cancel final:Cancellation {std::atomic<bool> value{false};bool cancelled()const override{return value.load();}};
const std::string identity="{\"device_id\":\"machine\",\"device_public_key\":\"public-key\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}";
struct Fake final:Transport {
  std::string first=identity,second="{\"token\":\"test.jwt.value\",\"profiles\":[\"default\"]}";
  int probes=0,posts=0,status=200;int64_t sizeOverride=-2;
  const char* encoding="";const char* transfer="";Cancel* cancelAfterProbe=nullptr;Cancel* cancelAfterPost=nullptr;
  bool block=false;std::atomic<bool>* started=nullptr;
  uint32_t delayMs=0,postDelayMs=0;
  bool exchange(const Request& request,ResponseSink& sink,const Cancellation* cancel)override {
    if(started)started->store(true);
    if(delayMs)std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    while(block&&!(cancel&&cancel->cancelled()))std::this_thread::yield();
    if(cancel&&cancel->cancelled())return false;
    const bool probe=request.method==Method::Get;
    if(!probe&&postDelayMs)std::this_thread::sleep_for(std::chrono::milliseconds(postDelayMs));
    assert(request.timeoutMs>0&&request.timeoutMs<=kDeadlineMs);
    assert(std::string(request.baseUrl)=="http://127.0.0.1:8748");
    if(probe) {
      ++probes;assert(std::string(request.path)=="/api/devices/link-info");
      assert(!request.body&&!request.bodyBytes&&!request.deviceInstance);
    } else {
      ++posts;assert(std::string(request.path)=="/api/auth/mcu-login");
      assert(std::string(request.deviceInstance)=="device-instance");
      const std::string body(request.body,request.bodyBytes);
      assert(body=="{\"token\":\"device-instance\",\"id\":\"device-instance\",\"account\":\"user\",\"password\":\"p\\\"a\\\\ss\\u000a\"}");
      assert(body.find("profile")==std::string::npos&&body.find("relay")==std::string::npos);
    }
    const auto& response=probe?first:second;
    if(!sink.begin({status,sizeOverride==-2?int64_t(response.size()):sizeOverride,"application/json; charset=utf-8",encoding,transfer}))return false;
    // Deliberate one-byte fragments exercise the production SAX consumer.
    for(unsigned char byte:response)if(!sink.write(&byte,1))return false;
    if(probe&&cancelAfterProbe)cancelAfterProbe->value.store(true);
    if(!probe&&cancelAfterPost)cancelAfterPost->value.store(true);
    return true;
  }
};
Input make(){Input input;assert(makeInput(input,"http://127.0.0.1:8748/","machine|desktop|8748","default","device-instance","user","p\"a\\ss\n",7));return input;}
int main() {
  auto input=make();Fake good;auto result=resolve(input,good);
  assert(result.error==Error::Ok&&result.stage==Stage::Complete&&result.epoch==7);
  assert(std::strcmp(result.jwt.data(),"test.jwt.value")==0&&good.probes==1&&good.posts==1);
  // Official Studio exposes a PEM public key, whose decoded LF/CRLF line
  // breaks are valid metadata. Exercise the actual reauthentication pipeline,
  // including one-byte SAX delivery and the subsequent credential POST.
  for(const std::string& newline:{std::string("\\n"),std::string("\\r\\n")}) {
    Fake pem;
    const std::string key="-----BEGIN PUBLIC KEY-----"+newline+
        "MCowBQYDK2VwAyEA11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo="+newline+
        "-----END PUBLIC KEY-----"+newline;
    pem.first="{\"device_id\":\"machine\",\"device_public_key\":\""+key+
        "\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}";
    auto recovered=resolve(input,pem);
    assert(recovered.error==Error::Ok&&recovered.stage==Stage::Complete&&pem.posts==1);
  }
  Fake changed;changed.first="{\"device_id\":\"other\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}";
  assert(resolve(input,changed).error==Error::BindingChanged&&changed.posts==0);
  Fake port;port.first="{\"device_id\":\"machine\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\",\"http_port\":8749}";
  assert(resolve(input,port).error==Error::BindingChanged&&port.posts==0);
  for(const std::string& broken:{std::string("[]"),identity+"{}",identity.substr(0,identity.size()-1),
      std::string("{\"device_id\":\"machine\",\"device_id\":\"machine\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}"),
      std::string("{\"device_id\":\"machine\\u0000evil\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}"),
      std::string("{\"device_id\":\"machine\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\",\"http_port\":\"8748\"}"),
      std::string("{\"device_id\":\"machine\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\",\"http_port\":8748.0}")}) {
    Fake bad;bad.first=broken;assert(resolve(input,bad).error!=Error::Ok&&bad.posts==0);
  }
  for(const std::string& broken:{
      std::string("{\"device_id\":\"machine\\n\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}"),
      std::string("{\"device_id\":\"machine\",\"device_public_key\":\"key\",\"endpoint_kind\":\"desktop\\r\\n\",\"http_port\":8748}"),
      std::string("{\"device_id\":\"machine\",\"device_public_key\":\"key\\u0000bad\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}"),
      std::string("{\"device_id\":\"machine\",\"device_public_key\":\"key\\tbad\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}"),
      std::string("{\"device_id\":\"machine\",\"device_public_key\":\"key\\u0001bad\",\"endpoint_kind\":\"desktop\",\"http_port\":8748}")}) {
    Fake bad;bad.first=broken;assert(resolve(input,bad).error==Error::InvalidResponse&&bad.posts==0);
  }
  for(int64_t size:{-1,0,8193}){Fake bad;bad.sizeOverride=size;assert(resolve(input,bad).error!=Error::Ok&&bad.posts==0);}
  Fake encoded;encoded.encoding="gzip";assert(resolve(input,encoded).error!=Error::Ok&&encoded.posts==0);
  Fake chunked;chunked.transfer="chunked";assert(resolve(input,chunked).error!=Error::Ok&&chunked.posts==0);
  Fake redirect;redirect.status=302;assert(resolve(input,redirect).error==Error::Transient&&redirect.posts==0);
  Fake denied;denied.status=401;assert(resolve(input,denied).error==Error::Rejected&&denied.posts==0);
  Fake late;Cancel cancel;late.cancelAfterProbe=&cancel;auto cancelled=resolve(input,late,&cancel);
  assert(cancelled.error==Error::Cancelled&&!cancelled.jwt[0]&&late.posts==0);
  Fake timeout;timeout.delayMs=kIdentityDeadlineMs+1;
  auto timed=resolve(input,timeout);assert(timed.error==Error::Transient&&!timed.jwt[0]&&timeout.posts==0);
  Fake lateLogin;lateLogin.postDelayMs=8001;
  auto lateToken=resolve(input,lateLogin);assert(lateToken.error==Error::Transient&&!lateToken.jwt[0]&&lateLogin.posts==1);
  Fake cancelledLogin;Cancel cancelledAfterPost;cancelledLogin.cancelAfterPost=&cancelledAfterPost;
  auto cancelledToken=resolve(input,cancelledLogin,&cancelledAfterPost);assert(cancelledToken.error==Error::Cancelled&&!cancelledToken.jwt[0]);
  for(const std::string& token:{std::string("{\"token\":\"first\",\"token\":\"second\"}"),std::string("{\"token\":\"a\\u0000b\"}"),
      std::string("{\"token\":\"a\\r\\nb\"}"),std::string("{\"token\":null}"),std::string("{\"token\":\"\"}"),std::string("{\"token\":\"")+std::string(4097,'a')+"\"}"}) {
    Fake bad;bad.second=token;auto out=resolve(input,bad);assert(out.error!=Error::Ok&&!out.jwt[0]);
  }
  assert(!makeInput(input,"http://user:password@host:8748","machine|desktop|8748","default","id","user","pass",9));
  assert(input.epoch==7);
  assert(!makeInput(input,"http://host:8748/path","machine|desktop|8748","default","id","user","pass",9));
  assert(!makeInput(input,"http://host:8748?query","machine|desktop|8748","default","id","user","pass",9));
  assert(!makeInput(input,"http://host:8748#fragment","machine|desktop|8748","default","id","user","pass",9));
  assert(!makeInput(input,"http://host:8748","machine|desktop|8748|extra","default","id","user","pass",9));
  assert(!makeInput(input,"http://host:8748","machine|desktop|8748","default","id\r\nInjected: yes","user","pass",9));
  Job job;assert(job.start(input)==StartResult::NotSupported);
  std::atomic<bool> started{false};auto held=std::unique_ptr<Fake>(new Fake);held->block=true;held->started=&started;
  assert(job.start(input,std::move(held))==StartResult::Started);
  while(!started.load())std::this_thread::yield();
  assert(job.busy()&&job.start(input)==StartResult::Busy);job.cancel();
  Output out;while(job.poll(out)==Poll::Pending)std::this_thread::yield();
  assert(out.error==Error::Cancelled&&!out.jwt[0]&&!job.busy());
  assert(job.start(input,std::unique_ptr<Transport>(new Fake))==StartResult::Started);
  while(job.poll(out)==Poll::Pending)std::this_thread::yield();
  assert(out.error==Error::Ok&&!job.busy());
}
