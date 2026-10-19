#include "OfficialSpeechPlan.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace official_speech;
struct Row {std::string id,role,text;};
std::string quote(const std::string& value) {
  std::string result="\"";
  for(unsigned char c:value) {
    if(c=='"'||c=='\\'){result+='\\';result+=char(c);}
    else if(c<0x20){static const char hex[]="0123456789abcdef";result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
    else result+=char(c);
  }
  return result+'"';
}
official_incoming::Snapshot<12> page(const std::vector<Row>& rows,const Request& request,const char* session="session") {
  assert(request.offset<rows.size());
  const size_t end=rows.size()-request.offset,first=end>12?end-12:0;
  std::string json="{\"session\":{\"id\":"+quote(session)+"},\"messages\":[";
  for(size_t n=first;n<end;++n) {
    if(n!=first)json+=',';
    json+="{\"id\":"+quote(rows[n].id)+",\"role\":"+quote(rows[n].role)+",\"content\":"+quote(rows[n].text)+"}";
  }
  json+="],\"total\":"+std::to_string(rows.size())+",\"offset\":"+std::to_string(request.offset)+"}";
  official_incoming::Snapshot<12> snapshot(official_incoming::Kind::History,request.byteOffset);
  bounded_json::Reader<> reader(snapshot);
  for(size_t n=0;n<json.size();n+=7)assert(reader.feed(json.data()+n,std::min<size_t>(7,json.size()-n)));
  assert(reader.finish());return snapshot;
}
Offer offer(Plan& plan,const official_incoming::Snapshot<12>& snapshot,const Request& request) {
  return plan.offer(snapshot,request,9,"session","default");
}
void basicLongTurns() {
  std::vector<Row> rows;
  for(unsigned n=0;n<7;++n)rows.push_back({"old-"+std::to_string(n),"assistant","old response must not be spoken"});
  std::map<std::string,std::string> expected,heard;
  std::vector<std::string> expectedOrder,order;
  std::string longText;
  for(unsigned n=0;n<900;++n)longText+="长中文😀abc";
  for(unsigned n=0;n<57;++n) {
    const std::string id="new-"+std::to_string(n);
    if(n%5==0) {
      rows.push_back({id,"assistant",n%2?"second response":longText});
      expected[id]=rows.back().text;expectedOrder.push_back(id);
    } else rows.push_back({id,n%3?"tool":"user","unspoken row"});
  }
  Plan plan;assert(plan.arm(9,"session","default",7));
  assert(!plan.request(uint32_t(rows.size())).needed());
  assert(plan.complete()&&!plan.complete());
  auto first=plan.request(uint32_t(rows.size()));
  assert(first.offset==rows.size()-19&&first.byteOffset==0&&first.limit==12);
  size_t requests=0;
  while(plan.phase()!=Phase::Done) {
    const auto request=plan.request(uint32_t(rows.size()));
    if(!request.needed()){assert(request.phase==Phase::Done);break;}
    assert(++requests<1000);
    const auto repeated=plan.request(uint32_t(rows.size()));
    assert(repeated.ticket==request.ticket&&repeated.generation==request.generation&&repeated.offset==request.offset);
    const auto snapshot=page(rows,request);const Offer result=offer(plan,snapshot,request);
    assert(result==Offer::Audio||result==Offer::More||result==Offer::Done);
    if(result==Offer::Audio) {
      const Pending* pending=plan.pending();assert(pending&&pending->bytes&&pending->bytes<=1024&&!pending->silent);
      assert(native_chat::validUtf8(pending->text.data(),pending->bytes));
      const std::string id=pending->id.data();
      if(order.empty()||order.back()!=id)order.push_back(id);
      assert(pending->nextByte==heard[id].size());
      assert(pending->ordinal>7&&pending->total==expected[id].size());
      heard[id].append(pending->text.data(),pending->bytes);
      const uint32_t before=pending->nextByte,bytes=pending->bytes;
      assert(!plan.request(uint32_t(rows.size())).needed());
      assert(plan.played(true));assert(plan.progress().nextByte==before+bytes);
      assert(offer(plan,snapshot,request)==Offer::NotRequested); // Same HTTP result cannot acknowledge/replay twice.
    }
  }
  assert(heard==expected&&order==expectedOrder);
  assert(!plan.pending()&&!plan.played(true)&&!plan.skipSilent());
}
void mutationAndContexts() {
  const std::vector<Row> rows{{"user","user","question"},{"answer","assistant",std::string(2300,'x')}};
  {
    Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto request=plan.request(2);const auto snapshot=page(rows,request);
    assert(plan.offer(snapshot,request,10,"session","default")==Offer::Stale);
    assert(plan.offer(snapshot,request,9,"other","default")==Offer::Stale);
    assert(plan.offer(snapshot,request,9,"session","other")==Offer::Stale);
    assert(plan.phase()==Phase::AwaitingPage&&offer(plan,snapshot,request)==Offer::Audio);
    assert(plan.played(true)&&plan.progress().nextByte==1024);
    auto next=plan.request(2);auto changed=rows;changed[1].id="different-answer";
    assert(offer(plan,page(changed,next),next)==Offer::Invalid&&plan.phase()==Phase::Invalid);
    assert(plan.progress().nextByte==1024&&!plan.request(2).needed());
  }
  {
    Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto request=plan.request(2);assert(offer(plan,page(rows,request),request)==Offer::Audio);
    assert(plan.played(true));plan.request(3);assert(plan.phase()==Phase::Invalid&&!plan.pending());
  }
  {
    Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto request=plan.request(2);assert(offer(plan,page(rows,request),request)==Offer::Audio);
    assert(plan.played(true));auto next=plan.request(2);auto changed=rows;changed[1].text+='y';
    assert(offer(plan,page(changed,next),next)==Offer::Invalid);
  }
  {
    Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto old=plan.request(2);const auto stale=page(rows,old);
    assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto current=plan.request(2);assert(old.ticket==current.ticket&&old.generation!=current.generation);
    assert(offer(plan,stale,old)==Offer::Stale&&plan.phase()==Phase::AwaitingPage);
    assert(offer(plan,page(rows,current),current)==Offer::Audio);
  }
  {
    Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto request=plan.request(2);auto wrong=page(rows,request);++wrong.offset;
    assert(offer(plan,wrong,request)==Offer::Invalid);
  }
  {
    Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto request=plan.request(2);auto wrong=page(rows,request);--wrong.rows;
    assert(offer(plan,wrong,request)==Offer::Invalid);
  }
  {
    Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
    const auto request=plan.request(2);auto wrong=page(rows,request);wrong.messages[1].id=wrong.messages[0].id;
    assert(offer(plan,wrong,request)==Offer::Invalid);
  }
  {
    Plan plan;assert(plan.arm(9,"session","default",3)&&plan.complete());
    assert(plan.request(2).phase==Phase::Invalid); // Deletion before the baseline is not guessed around.
  }
}
void failuresAndSilent() {
  const std::vector<Row> rows{{"a","assistant",std::string(2300,'x')}};
  Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
  auto request=plan.request(1);assert(offer(plan,page(rows,request),request)==Offer::Audio);
  assert(plan.played(true)&&plan.progress().nextByte==1024);
  request=plan.request(1);assert(request.byteOffset==1024);
  assert(offer(plan,page(rows,request),request)==Offer::Audio);
  assert(!plan.played(false)&&plan.phase()==Phase::Cancelled&&plan.progress().nextByte==1024);
  assert(!plan.pending()&&!plan.request(1).needed()&&!plan.played(true));
  assert(offer(plan,page(rows,request),request)==Offer::NotRequested);
  const std::vector<Row> silent{{"empty","assistant",""},{"markup","assistant",std::string(1300,'*')},{"actual","assistant","你好"}};
  assert(plan.arm(9,"session","default",0)&&plan.complete());
  size_t skipped=0;std::string heard;
  for(unsigned loop=0;loop<10&&plan.phase()!=Phase::Done;++loop) {
    request=plan.request(3);if(!request.needed())break;
    const auto result=offer(plan,page(silent,request),request);
    if(result==Offer::Silent){assert(!plan.played(true));assert(plan.skipSilent());++skipped;}
    else if(result==Offer::Audio){const auto* pending=plan.pending();heard.append(pending->text.data(),pending->bytes);assert(plan.played(true));}
    else assert(result==Offer::Done||result==Offer::More);
  }
  assert(skipped==2&&heard=="你好"&&plan.phase()==Phase::Done);
}
void toolsAndWindowConflicts() {
  std::vector<Row> rows;
  for(unsigned n=0;n<30;++n)rows.push_back({"tool-"+std::to_string(n),"tool","not speech"});
  rows.push_back({"a","assistant","first"});rows.push_back({"user","user","not speech"});
  rows.push_back({"b","assistant","second"});
  Plan plan;assert(plan.arm(9,"session","default",0)&&plan.complete());
  unsigned toolPages=0;std::string heard;
  while(plan.phase()!=Phase::Done) {
    const auto request=plan.request(uint32_t(rows.size()));if(!request.needed())break;
    const auto snapshot=page(rows,request);const auto result=offer(plan,snapshot,request);
    if(result==Offer::More){assert(snapshot.count==0&&snapshot.rows==12);++toolPages;}
    else if(result==Offer::Audio){heard+=plan.pending()->text.data();assert(plan.played(true));}
    else assert(result==Offer::Done);
  }
  assert(toolPages==2&&heard=="firstsecond");
  const std::vector<Row> longRow{{"a","assistant",std::string(2300,'x')}};
  for(unsigned conflict=0;conflict<5;++conflict) {
    assert(plan.arm(9,"session","default",0)&&plan.complete());
    auto request=plan.request(1);auto first=page(longRow,request);
    assert(offer(plan,first,request)==Offer::Audio&&plan.played(true));
    request=plan.request(1);auto second=page(longRow,request);
    if(conflict==0)--second.messages[0].content.start;
    if(conflict==1)second.messages[0].content.total=1000;
    if(conflict==2)second.messages[0].content.bytes=0;
    if(conflict==3)second.messages[0].content.text[0]=char(0x80);
    if(conflict==4)second.messages[0].index=12;
    assert(offer(plan,second,request)==Offer::Invalid&&plan.phase()==Phase::Invalid);
    assert(plan.progress().nextByte==1024&&!plan.pending());
  }
}
void limitsAndEdges() {
  static_assert(sizeof(Plan)<=2048,"No extra frame/message history may be retained");
  Plan plan;
  assert(!plan.arm(0,"session","default",0));
  assert(!plan.arm(1,"","default",0));
  assert(!plan.arm(1,"session",std::string(225,'p').c_str(),0));
  assert(plan.arm(9,"session","default",UINT32_MAX)&&plan.complete());
  assert(plan.request(UINT32_MAX).phase==Phase::Done); // baseline+1 must not wrap.
  assert(plan.arm(9,"session","default",0)&&plan.complete());assert(plan.request(0).phase==Phase::Done);
  std::string content;
  for(unsigned n=0;n<700;++n)content+="a😀中é";
  const std::vector<Row> rows{{"id","assistant",content}};std::string rebuilt;
  assert(plan.arm(9,"session","default",0)&&plan.complete());
  while(plan.phase()!=Phase::Done) {
    const auto request=plan.request(1);if(!request.needed())break;
    const auto result=offer(plan,page(rows,request),request);assert(result==Offer::Audio);
    const auto* pending=plan.pending();assert(pending->nextByte==rebuilt.size());
    rebuilt.append(pending->text.data(),pending->bytes);assert(plan.played(true));
  }
  assert(rebuilt==content);
  std::printf("Speech plan fixed bytes: %zu\n",sizeof(Plan));
}
} // namespace
int main(){basicLongTurns();mutationAndContexts();failuresAndSilent();toolsAndWindowConflicts();limitsAndEdges();}
