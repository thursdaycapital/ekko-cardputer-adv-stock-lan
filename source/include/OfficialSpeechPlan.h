#pragma once

#include "OfficialStudioIncoming.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

// Bounded, task-free playback cursor for official chronological history pages.
// The caller supplies an authoritative post-run total, cancels on new input /
// history edits, and offers ONLY completely validated HTTP snapshots. The API
// has no history revision: same-total external replacement is unobservable and
// cannot be made safe by guessing IDs. Detectable mutations invalidate the plan.
namespace official_speech {
enum class Phase:uint8_t {Idle,Armed,Reading,AwaitingPage,Pending,Done,Cancelled,Invalid};
enum class Offer:uint8_t {Audio,Silent,More,Done,Stale,NotRequested,Invalid};
struct Request {
  Phase phase=Phase::Idle;
  uint64_t generation=0,ticket=0;
  uint32_t offset=0,byteOffset=0;
  uint8_t limit=12;
  bool needed()const{return phase==Phase::AwaitingPage;}
};
struct Pending {
  std::array<char,1025> text{};
  std::array<char,129> id{};
  uint32_t ordinal=0,nextByte=0,total=0; // ordinal is raw, chronological, 1-based.
  uint16_t bytes=0;
  bool silent=false;
};
class Plan {
 public:
  bool arm(uint32_t epoch,const char* session,const char* profile,uint32_t baselineRows) {
    std::array<char,225> newSession{},newProfile{};
    if(!epoch||!native_chat::boundedIdentifier(session,224)||!native_chat::boundedIdentifier(profile,224)||
        generation_==UINT64_MAX) {invalidate();return false;}
    std::memcpy(newSession.data(),session,std::strlen(session)+1);
    std::memcpy(newProfile.data(),profile,std::strlen(profile)+1);
    session_=newSession;profile_=newProfile;epoch_=epoch;baseline_=baselineRows;
    ++generation_;ticket_=0;total_=0;frozen_=false;active_=false;
    nextOrdinal_=uint64_t(baselineRows)+1;pending_=Pending{};request_=Request{};
    phase_=Phase::Armed;return true;
  }
  bool complete() {
    if(phase_!=Phase::Armed)return false;
    phase_=Phase::Reading;return true;
  }
  void cancel() {phase_=Phase::Cancelled;clearText();}
  Phase phase()const{return phase_;}
  // Owned by this Plan, borrowed only until its next mutation. Copy into the
  // voice worker's owned input before starting an asynchronous TTS request.
  const Pending* pending()const{return phase_==Phase::Pending?&pending_:nullptr;}
  const Pending& progress()const{return pending_;}

  Request request(uint32_t totalRows) {
    if(phase_==Phase::Armed||terminal())return statusRequest();
    if(!frozen_) {
      if(totalRows<baseline_){invalidate();return statusRequest();}
      total_=totalRows;frozen_=true;
    } else if(totalRows!=total_){invalidate();return statusRequest();}
    if(phase_==Phase::Pending)return statusRequest();
    if(phase_==Phase::AwaitingPage)return request_;
    if(nextOrdinal_>total_){phase_=Phase::Done;return statusRequest();}
    if(ticket_==UINT64_MAX){invalidate();return statusRequest();}
    // offset counts backward from newest, while the returned array is oldest
    // first. Start with the first 12 raw rows AFTER the baseline, not newest 12.
    const uint64_t end=std::min<uint64_t>(total_,nextOrdinal_+11);
    request_.phase=Phase::AwaitingPage;request_.generation=generation_;request_.ticket=++ticket_;
    request_.offset=total_-uint32_t(end);request_.byteOffset=active_?pending_.nextByte:0;request_.limit=12;
    phase_=Phase::AwaitingPage;return request_;
  }

  Offer offer(const official_incoming::Snapshot<12>& page,const Request& issued,
              uint32_t epoch,const char* session,const char* profile) {
    if(epoch!=epoch_||!same(session,session_.data())||!same(profile,profile_.data())||
        issued.generation!=generation_)return Offer::Stale;
    if(phase_!=Phase::AwaitingPage)return Offer::NotRequested;
    if(issued.ticket!=request_.ticket)return Offer::Stale;
    if(!issued.needed()||issued.offset!=request_.offset||issued.byteOffset!=request_.byteOffset||issued.limit!=12)
      return invalidOffer();
    if(page.overflow||!page.hasTotal()||page.total!=total_||page.offset!=issued.offset||
        !same(page.session.data(),session_.data())||issued.offset>=total_)return invalidOffer();
    const uint32_t expectedRows=std::min<uint32_t>(12,total_-issued.offset);
    if(page.rows!=expectedRows||page.count>12||page.count>page.rows)return invalidOffer();
    const uint32_t first=total_-issued.offset-expectedRows+1;
    const uint32_t last=total_-issued.offset;
    if(nextOrdinal_<first||nextOrdinal_>last)return invalidOffer();
    for(size_t n=0;n<page.count;++n) {
      const auto& message=page.messages[n];const auto& window=message.content;
      if(message.index>=page.rows||(n&&message.index<=page.messages[n-1].index)||
          !native_chat::boundedIdentifier(message.id.data(),128)||
          (!same(message.role.data(),"assistant")&&!same(message.role.data(),"user"))||
          !window.began||!window.closed||window.bytes>1024||window.start>window.total||
          window.bytes>window.total-window.start||window.text[window.bytes]!=0||
          !native_chat::validUtf8(window.text.data(),window.bytes)||
          (window.start<window.total&&!window.bytes))return invalidOffer();
      for(size_t previous=0;previous<n;++previous)
        if(same(message.id.data(),page.messages[previous].id.data()))return invalidOffer();
    }
    while(nextOrdinal_<=last) {
      const official_incoming::Message* message=nullptr;
      for(size_t n=0;n<page.count;++n)
        if(uint64_t(first)+page.messages[n].index==nextOrdinal_){message=&page.messages[n];break;}
      if(!message||same(message->role.data(),"user")) {
        if(active_)return invalidOffer(); // An active assistant cannot turn into a filtered/tool row.
        ++nextOrdinal_;continue;
      }
      const auto& window=message->content;
      if(active_) {
        if(pending_.ordinal!=nextOrdinal_||!same(pending_.id.data(),message->id.data())||
            pending_.total!=window.total||window.start!=pending_.nextByte)return invalidOffer();
      } else {
        if(window.start!=0)return invalidOffer();
        pending_=Pending{};pending_.id=message->id;pending_.ordinal=uint32_t(nextOrdinal_);
        pending_.total=window.total;active_=true;
      }
      if(!window.total) {active_=false;++nextOrdinal_;continue;}
      if(!window.bytes||window.bytes>pending_.total-pending_.nextByte)return invalidOffer();
      std::memcpy(pending_.text.data(),window.text.data(),window.bytes);
      pending_.text[window.bytes]=0;pending_.bytes=window.bytes;pending_.silent=onlyMarkup(pending_.text.data(),pending_.bytes);
      phase_=Phase::Pending;return pending_.silent?Offer::Silent:Offer::Audio;
    }
    phase_=nextOrdinal_>total_?Phase::Done:Phase::Reading;
    return phase_==Phase::Done?Offer::Done:Offer::More;
  }

  // A failed/cancelled playback is terminal. No automatic retry, re-arm or
  // replay of an already heard prefix is performed. The cursor never advances
  // before a successful playback acknowledgement.
  bool played(bool success) {
    if(phase_!=Phase::Pending)return false;
    if(!success){cancel();return false;}
    if(pending_.silent)return false; // Use the explicit silent-skip path.
    return advance();
  }
  // Only an empty/ASCII whitespace/known markup-only window can be skipped.
  // This is explicit, distinct from claiming audio was played successfully.
  bool skipSilent() {return phase_==Phase::Pending&&pending_.silent&&advance();}

 private:
  std::array<char,225> session_{},profile_{};
  Pending pending_;
  Request request_;
  uint64_t generation_=0,ticket_=0,nextOrdinal_=0;
  uint32_t epoch_=0,baseline_=0,total_=0;
  Phase phase_=Phase::Idle;
  bool frozen_=false,active_=false;
  static bool same(const char* a,const char* b){return a&&b&&!std::strcmp(a,b);}
  bool terminal()const{return phase_==Phase::Idle||phase_==Phase::Done||phase_==Phase::Cancelled||phase_==Phase::Invalid;}
  void clearText(){pending_.text[0]=0;pending_.bytes=0;pending_.silent=false;}
  void invalidate(){phase_=Phase::Invalid;clearText();}
  Offer invalidOffer(){invalidate();return Offer::Invalid;}
  Request statusRequest()const {Request value;value.phase=phase_;value.generation=generation_;return value;}
  bool advance() {
    if(!active_||!pending_.bytes||pending_.bytes>pending_.total-pending_.nextByte){invalidate();return false;}
    pending_.nextByte+=pending_.bytes;clearText();
    if(pending_.nextByte==pending_.total){active_=false;nextOrdinal_=uint64_t(pending_.ordinal)+1;}
    phase_=Phase::Reading;return true;
  }
  static bool onlyMarkup(const char* text,size_t bytes) {
    if(!bytes)return true;
    for(size_t n=0;n<bytes;++n) {
      const char c=text[n];
      if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n'&&c!='*'&&c!='_'&&c!='~'&&c!='`'&&c!='#'&&
          c!='>'&&c!='|'&&c!='-'&&c!='['&&c!=']'&&c!='('&&c!=')')return false;
    }
    return true;
  }
};
static_assert(sizeof(Plan)<=2048,"Speech plan must remain within its 2KiB fixed-memory cap");
} // namespace official_speech
