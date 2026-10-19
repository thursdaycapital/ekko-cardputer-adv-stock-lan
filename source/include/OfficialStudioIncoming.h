#pragma once
#include "BoundedJsonReader.h"
#include "ChatUtf8.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace official_incoming {

// A reloadable UTF-8 window, not a truncation of the authoritative message.
// total counts all decoded bytes, including bytes outside this window. NUL has
// a one-byte display-space representation so offsets still match the server.
struct Window {
  std::array<char, 1025> text{};
  uint32_t requested = 0, start = 0, total = 0;
  uint16_t bytes = 0;
  bool began = false, closed = false, capped = false;
  void reset(uint32_t offset = 0) { *this = Window{}; requested = start = offset; }
  bool append(const char* data, size_t count) {
    if (count > UINT32_MAX - total) return false;
    for (size_t pos = 0; pos < count;) {
      const size_t scalar = native_chat::codepointBytes(static_cast<uint8_t>(data[pos]));
      if (scalar > count - pos) return false;
      if (total < requested && total + scalar > requested) start = total;
      if (total >= start && !capped) {
        if (bytes + scalar > 1024) capped=true;
        else for (size_t n = 0; n < scalar; ++n) text[bytes++] = data[pos+n] ? data[pos+n] : ' ';
      }
      total += scalar; pos += scalar;
    }
    text[bytes] = 0; return true;
  }
};

struct Message {
  std::array<char,129> id{};
  std::array<char,16> role{};
  Window content;
  uint64_t index = 0;
  uint32_t seen = 0, keys = 0;
  bool valid() const {
    return id[0] && (!std::strcmp(role.data(),"user") || !std::strcmp(role.data(),"assistant")) && content.closed;
  }
};

enum class Kind { SocketEvent, History };
template<size_t Capacity=12> class Snapshot : public bounded_json::Listener {
 public:
  explicit Snapshot(Kind kind, uint32_t offset = 0) : kind_(kind), windowOffset_(offset) {}
  std::array<char,64> event{};
  std::array<char,225> session{};
  std::array<char,129> run{}, messageId{};
  std::array<char,65> queue{};
  std::array<char,129> command{};
  std::array<char,513> error{};
  Window text, commandMessage;
  std::array<Message,Capacity> messages{};
  size_t count = 0;
  uint64_t rows = 0;
  uint32_t total = 0, offset = 0;
  bool working = false, hasWorking = false, hasMore = false, hasMoreBefore = false;
  bool commandOk = false, hasCommandOk = false, commandTerminal = false, hasCommandTerminal = false;
  bool overflow = false;
  bool hasTotal() const { return totalUsed_; }

  bool onEvent(const bounded_json::Event& value) override {
    using namespace bounded_json;
    const auto path = value.path;
    if (kind_ == Kind::SocketEvent && path.size == 1 && index(path,0,0))
      return copyScalar(value, event.data(),event.size(), eventUsed_);
    size_t base = kind_ == Kind::SocketEvent ? 1 : 0;
    if (base && (!path.size || !index(path,0,1))) return true;
    if (kind_ == Kind::History && path.size == 2 && key(path,0,"session") && key(path,1,"id"))
      {if(value.type==Type::Key) return registerKey(keys_,1);return scalar(value,session.data(),session.size(),sessionUsed_,1);}
    if (path.size == base + 1) {
      if(value.type==Type::Key) {
        uint32_t bit=key(path,base,"session_id")?1:key(path,base,"run_id")?2:key(path,base,"queue_id")?4:
            key(path,base,"message_id")?8:key(path,base,"error")?16:key(path,base,"messages")?64:
            key(path,base,"isWorking")?128:key(path,base,"hasMore")?256:key(path,base,"hasMoreBefore")?512:
            (key(path,base,"total")||key(path,base,"messageTotal"))?1024:key(path,base,"offset")?2048:
            key(path,base,"event")?4096:key(path,base,"delta")?8192:key(path,base,"output")?16384:
            key(path,base,"session")?32768:key(path,base,"command")?131072:
            key(path,base,"message")?262144:key(path,base,"ok")?524288:key(path,base,"terminal")?1048576:0;
        return !bit||registerKey(keys_,bit);
      }
      if (key(path,base,"session_id")) return scalar(value,session.data(),session.size(),sessionUsed_,1);
      if (key(path,base,"run_id")) return scalar(value,run.data(),run.size(),runUsed_,2);
      if (key(path,base,"queue_id")) return scalar(value,queue.data(),queue.size(),queueUsed_,4);
      if (key(path,base,"message_id")) return scalar(value,messageId.data(),messageId.size(),messageIdUsed_,8,false,true);
      if (key(path,base,"error")) return scalar(value,error.data(),error.size(),errorUsed_,16, true);
      if (key(path,base,"command")) return scalar(value,command.data(),command.size(),commandUsed_,131072);
      if (key(path,base,"message") && !std::strcmp(event.data(),"session.command"))
        return window(value,commandMessage,262144);
      if (key(path,base,"ok") || key(path,base,"terminal")) {
        if (value.type!=Type::Boolean) return false;
        const bool isOk=key(path,base,"ok");
        if (!once(isOk?524288:1048576)) return false;
        if (isOk) {commandOk=value.booleanValue;hasCommandOk=true;}
        else {commandTerminal=value.booleanValue;hasCommandTerminal=true;}
      }
      if ((key(path,base,"delta") && !std::strcmp(event.data(),"message.delta")) ||
          (key(path,base,"output") && !std::strcmp(event.data(),"run.completed"))) return window(value,text,32);
      if (key(path,base,"event")) {
        if (!scalar(value,payloadEvent_.data(),payloadEvent_.size(),payloadEventUsed_,4096)) return false;
        if (value.type==Type::StringEnd && std::strcmp(event.data(),payloadEvent_.data())) return false;
        return true;
      }
      if (key(path,base,"messages")) return value.type==Type::ArrayBegin ? once(64) : value.type==Type::ArrayEnd;
      if (key(path,base,"isWorking")) {
        if(value.type!=Type::Boolean) return false;
        if (!once(128)) return false; working=value.booleanValue; hasWorking=true;
      }
      if (key(path,base,"hasMore") || key(path,base,"hasMoreBefore")) {
        if(value.type!=Type::Boolean) return false;
        if (!once(key(path,base,"hasMore") ? 256 : 512)) return false;
        if (key(path,base,"hasMore")) hasMore=value.booleanValue; else hasMoreBefore=value.booleanValue;
      }
      if (key(path,base,"total") || key(path,base,"messageTotal")) return number(value,total,totalUsed_,1024);
      if (key(path,base,"offset")) return number(value,offset,offsetUsed_,2048);
    }
    if (path.size == base+2 && key(path,base,"error") && key(path,base+1,"message"))
      {if(value.type==Type::Key)return registerKey(keys_,65536);return scalar(value,error.data(),error.size(),errorUsed_,16,true);}
    if (path.size >= base+2 && key(path,base,"messages") && path.segments[base+1].kind == SegmentKind::Index) {
      if (!Capacity) return true;
      if (path.size == base+2) {
        if (value.type == Type::ObjectBegin) {
          current_ = Message{}; current_.index=path.segments[base+1].index;
          current_.content.reset(windowOffset_); ++rows;
        } else if (value.type == Type::ObjectEnd && current_.valid()) {
          if (count == messages.size()) {
            for (size_t n=1;n<count;++n) messages[n-1]=messages[n];
            --count;
          }
          messages[count++]=current_;
        }
      } else if (path.size == base+3) {
        const size_t field=base+2;
        if(value.type==Type::Key) {
          const uint32_t bit=key(path,field,"id")?1:key(path,field,"role")?2:key(path,field,"content")?4:0;
          return !bit||registerKey(current_.keys,bit);
        }
        if (key(path,field,"id")) return messageScalar(value,current_.id.data(),current_.id.size(),idUsed_,1);
        if (key(path,field,"role")) return messageScalar(value,current_.role.data(),current_.role.size(),roleUsed_,2);
        if (key(path,field,"content")) {
          if (value.type==Type::StringBegin && !messageOnce(4)) return false;
          return windowValue(value,current_.content);
        }
      }
    }
    return true;
  }

 private:
  Kind kind_;
  std::array<char,64> payloadEvent_{};
  size_t payloadEventUsed_=0;
  uint32_t windowOffset_ = 0, seen_ = 0, keys_ = 0;
  Message current_;
  size_t eventUsed_=0,sessionUsed_=0,runUsed_=0,queueUsed_=0,messageIdUsed_=0,errorUsed_=0,idUsed_=0,roleUsed_=0,commandUsed_=0;
  bool totalUsed_=false,offsetUsed_=false;
  bool errorCapped_=false;
  static bool key(bounded_json::PathView path,size_t at,const char* name) {
    const auto& part=path.segments[at];
    return part.kind==bounded_json::SegmentKind::Key && part.keyBytes==std::strlen(name) && !std::memcmp(part.key,name,part.keyBytes);
  }
  static bool index(bounded_json::PathView path,size_t at,uint64_t value) {
    const auto& part=path.segments[at]; return part.kind==bounded_json::SegmentKind::Index && part.index==value;
  }
  bool once(uint32_t bit) { if (seen_ & bit) return false; seen_ |= bit; return true; }
  bool registerKey(uint32_t& keys,uint32_t bit) {if(keys&bit)return false;keys|=bit;return true;}
  bool messageOnce(uint32_t bit) { if (current_.seen & bit) return false; current_.seen |= bit; return true; }
  bool copyScalar(const bounded_json::Event& value,char* destination,size_t capacity,size_t& used, bool clip=false) {
    using bounded_json::Type;
    if (value.type==Type::StringBegin || value.type==Type::NumberBegin) { used=0; destination[0]=0;if(clip)errorCapped_=false; }
    else if (value.type==Type::StringChunk || value.type==Type::NumberChunk) {
      if (std::memchr(value.data,0,value.size)) return false;
      if(clip&&errorCapped_)return true;
      size_t bytes=value.size;
      if (used+bytes >= capacity) {
        if (!clip) { overflow=true; return false; }
        errorCapped_=true;
        bytes=native_chat::prefixBytes(value.data,value.size,capacity-used-1);
      }
      std::memcpy(destination+used,value.data,bytes); used+=bytes; destination[used]=0;
    }
    return true;
  }
  bool scalar(const bounded_json::Event& value,char* dst,size_t cap,size_t& used,uint32_t bit,bool clip=false,bool allowNumber=false) {
    if (value.type==bounded_json::Type::NumberBegin && !allowNumber) return false;
    if ((value.type==bounded_json::Type::StringBegin || value.type==bounded_json::Type::NumberBegin) && !once(bit)) return false;
    return copyScalar(value,dst,cap,used,clip);
  }
  bool messageScalar(const bounded_json::Event& value,char* dst,size_t cap,size_t& used,uint32_t bit) {
    if ((value.type==bounded_json::Type::StringBegin || value.type==bounded_json::Type::NumberBegin) && !messageOnce(bit)) return false;
    return copyScalar(value,dst,cap,used);
  }
  bool windowValue(const bounded_json::Event& value,Window& target) {
    if (value.type==bounded_json::Type::StringBegin) target.began=true;
    if (value.type==bounded_json::Type::StringChunk) return target.append(value.data,value.size);
    if (value.type==bounded_json::Type::StringEnd) { target.closed=true; if(target.start>target.total) target.start=target.total; }
    return true;
  }
  bool window(const bounded_json::Event& value,Window& target,uint32_t bit) {
    if (value.type==bounded_json::Type::StringBegin) { if(!once(bit)) return false; target.reset(); }
    return windowValue(value,target);
  }
  bool number(const bounded_json::Event& value,uint32_t& target,bool& started,uint32_t bit) {
    if(value.type!=bounded_json::Type::NumberBegin && value.type!=bounded_json::Type::NumberChunk &&
       value.type!=bounded_json::Type::NumberEnd) return false;
    if (value.type==bounded_json::Type::NumberBegin) { if(!once(bit)) return false; target=0; started=true; }
    if (value.type==bounded_json::Type::NumberChunk) {
      for (size_t n=0;n<value.size;++n) {
        if(value.data[n]<'0' || value.data[n]>'9') return false;
        uint32_t digit=static_cast<uint32_t>(value.data[n]-'0');
        if(target>(UINT32_MAX-digit)/10) { overflow=true; return false; }
        target=target*10+digit;
      }
    }
    return true;
  }
};
} // namespace official_incoming
