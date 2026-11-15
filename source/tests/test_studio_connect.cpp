#include "StudioConnectJob.h"
#include <cassert>
#include <string>
#include <vector>

using namespace studio_handshake;
const char* accept="s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
std::string valid="HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "+std::string(accept)+"\r\n\r\n";
Parser parse(const std::string& text,uint32_t elapsed=0) {
  Parser parser(accept);
  for(unsigned char byte:text)if(!parser.feed(byte,elapsed))break;
  return parser;
}
int main() {
  assert(parse(valid).state()==State::Complete);
  // Every possible fragmentation boundary is an input scheduling boundary,
  // not a separate buffer or reparse of already received bytes.
  for(size_t split=0;split<=valid.size();++split) {
    Parser parser(accept);
    for(size_t n=0;n<split;++n)assert(parser.feed(uint8_t(valid[n]),1));
    for(size_t n=split;n<valid.size();++n)assert(parser.feed(uint8_t(valid[n]),2));
    assert(parser.state()==State::Complete);
  }
  std::string mixed="HTTP/1.1 101 OK\r\nuPgRaDe: WebSocket\r\nConnection: keep-alive, UpGrAdE\r\nsec-websocket-accept:\t"+std::string(accept)+" \t\r\nContent-Length: 0\r\n\r\n";
  assert(parse(mixed).state()==State::Complete);
  assert(parse(valid,8000).error()==Error::Deadline);
  Parser slow(accept,8);
  for(size_t n=0;n<7;++n)assert(slow.feed(uint8_t(valid[n]),uint32_t(n)));
  assert(!slow.feed(uint8_t(valid[7]),8)&&slow.error()==Error::Deadline);
  Parser idle(accept,10);assert(!idle.expired(10)&&idle.error()==Error::Deadline);
  assert(parse("HTTP/1.1 200 OK\r\n\r\n").error()==Error::InvalidStatus);
  assert(parse("HTTP/1.0 101 OK\r\n\r\n").error()==Error::InvalidStatus);
  assert(parse("HTTP/1.1 1010 OK\r\n\r\n").error()==Error::InvalidStatus);
  assert(parse("HTTP/1.1 101 OK\n\n").error()==Error::InvalidLine);
  assert(parse("HTTP/1.1 101 OK\rX").error()==Error::InvalidLine);
  assert(parse("HTTP/1.1 101 OK\r\n Fold: value\r\n\r\n").error()==Error::InvalidHeader);
  assert(parse("HTTP/1.1 101 OK\r\n\r\n").error()==Error::MissingUpgrade);
  assert(parse("HTTP/1.1 101 OK\r\nUpgrade: websocket\r\n\r\n").error()==Error::MissingConnection);
  assert(parse("HTTP/1.1 101 OK\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n").error()==Error::MissingAccept);
  std::string duplicate=valid;duplicate.insert(duplicate.size()-2,"Sec-WebSocket-Accept: "+std::string(accept)+"\r\n");
  assert(parse(duplicate).error()==Error::DuplicateAccept);
  std::string wrong=valid;wrong[wrong.find(accept)]='A';assert(parse(wrong).error()==Error::InvalidAccept);
  for(const char* extra:{"Sec-WebSocket-Protocol: unexpected\r\n","Sec-WebSocket-Extensions: permessage-deflate\r\n","Content-Length: 12\r\n","Transfer-Encoding: chunked\r\n"}) {
    std::string text=valid;text.insert(text.size()-2,extra);assert(parse(text).state()==State::Failed);
  }
  assert(parse("HTTP/1.1 101 OK\r\nX: "+std::string(kLineBytes,'x')+"\r\n\r\n").error()==Error::LineTooLong);
  std::string many="HTTP/1.1 101 OK\r\n";
  while(many.size()<=kHeaderBytes)many+="X: "+std::string(200,'x')+"\r\n";
  assert(parse(many).error()==Error::TooLarge);
  Parser coalesced(accept);const std::string both=valid+std::string("\x81\x00",2);size_t consumed=0;
  while(coalesced.state()==State::Reading&&consumed<both.size())assert(coalesced.feed(uint8_t(both[consumed++]),1));
  assert(consumed==valid.size()&&coalesced.state()==State::Complete);
  studio_connect::Input input;
  assert(studio_connect::makeInput(input,"192.168.0.104",8748,false,"/socket.io/?EIO=4&transport=websocket&profile=default",10));
  assert(studio_connect::validInput(input)&&input.epoch==10);
  assert(!studio_connect::makeInput(input,"good\r\nInjected: yes",8748,false,"/",11));
  assert(input.epoch==10); // Invalid replacements do not corrupt an accepted input.
  assert(!studio_connect::makeInput(input,"host",0,false,"/",1));
  assert(!studio_connect::makeInput(input,"host",80,false,"/path#fragment",1));
  assert(!studio_connect::makeInput(input,"host",80,false,"/path\r\nInjected",1));
  assert(!studio_connect::makeInput(input,"host",80,false,"relative",1));
  assert(!studio_connect::makeInput(input,"host",80,false,("/"+std::string(studio_connect::kPathBytes,'a')).c_str(),1));
  assert(studio_connect::makeInput(input,"::1",80,false,"/",2));
  studio_connect::Job job;assert(job.start(input)==studio_connect::StartResult::NotSupported);
  studio_connect::Output output;assert(job.poll(output)==studio_connect::Poll::Idle);job.cancel();assert(!job.busy());
}
