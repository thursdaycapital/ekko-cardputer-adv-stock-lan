#include "ManagementRequestGate.h"
#include <cassert>
#include <string>
using namespace management_request;
Result inspect(Gate& gate,const std::string& bytes,uint32_t elapsed=1) {
  return gate.inspect(reinterpret_cast<const uint8_t*>(bytes.data()),std::min(bytes.size(),kHeaderBytes),bytes.size(),elapsed);
}
int main() {
  const std::string get="GET /health HTTP/1.1\r\nHost: device\r\nConnection: close\r\n\r\n";
  Gate basic;assert(inspect(basic,get)==Result::Ready);assert(basic.headerBytes()==get.size()&&!basic.bodyBytes());
  for(size_t split=0;split<=get.size();++split) {
    Gate gate;const auto partial=get.substr(0,split);inspect(gate,partial);
    assert(inspect(gate,get,2)==Result::Ready);
  }
  const std::string header="POST /device/login HTTP/1.1\r\nHost: device\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 8\r\n\r\n";
  const std::string post=header+"abc=1234";
  for(size_t split=0;split<=post.size();++split) {
    Gate gate;const auto partial=post.substr(0,split);inspect(gate,partial);
    assert(inspect(gate,post,2)==Result::Ready&&gate.bodyBytes()==8&&gate.headerBytes()==header.size());
  }
  Gate bytewise;for(size_t n=0;n<post.size();++n)assert(inspect(bytewise,post.substr(0,n))==Result::Waiting);
  assert(inspect(bytewise,post)==Result::Ready);
  Gate shortBody;assert(inspect(shortBody,header+"abc=12")==Result::Waiting);
  assert(inspect(shortBody,header+"abc=12",kDeadlineMs)==Result::Rejected&&shortBody.error()==Error::Deadline);
  for(const std::string& bad:{std::string("GET / HTTP/1.1\n\n"),std::string("GET / HTTP/1.1\rX"),
      std::string("GET http://host/ HTTP/1.1\r\n\r\n"),std::string("GET / HTTP/2.0\r\n\r\n"),
      std::string("GET / HTTP/1.1\r\n Folded: x\r\n\r\n"),std::string("GET / HTTP/1.1\r\nBad Header: x\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nContent-Length: 2, 2\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nContent-Length: 9999999999999999999999\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nExpect: 100-continue\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=x\r\n\r\n"),
      std::string("POST / HTTP/1.1\r\nContent-Encoding: gzip\r\n\r\n"),
      std::string("GET / HTTP/1.1\r\nContent-Length: 1\r\n\r\nx"),get+"GET /next HTTP/1.1\r\n\r\n",post+"extra"}) {
    Gate gate;assert(inspect(gate,bad)==Result::Rejected);
  }
  Gate longLine;assert(inspect(longLine,"GET /"+std::string(kLineBytes,'a')+" HTTP/1.1\r\n\r\n")==Result::Rejected&&longLine.error()==Error::LineTooLong);
  std::string large="GET / HTTP/1.1\r\n";while(large.size()<kHeaderBytes)large+="X: "+std::string(200,'a')+"\r\n";
  Gate longHeader;assert(inspect(longHeader,large)==Result::Rejected&&longHeader.error()==Error::HeaderTooLarge);
  Gate huge;assert(inspect(huge,std::string(kRequestBytes+1,'a'))==Result::Rejected&&huge.error()==Error::RequestTooLarge);
  Gate stalled;assert(stalled.inspect(nullptr,0,0,kDeadlineMs)==Result::Rejected&&stalled.error()==Error::Deadline);
  Gate changed;assert(inspect(changed,get.substr(0,12))==Result::Waiting);assert(inspect(changed,get.substr(0,11))==Result::Rejected&&changed.error()==Error::SnapshotChanged);
  // A complete large body is counted without retaining its bytes in Gate.
  std::string bodyHeader="POST /wifi HTTP/1.1\r\nContent-Length: 3000\r\n\r\n";
  Gate body;assert(inspect(body,bodyHeader+std::string(3000,'a'))==Result::Ready);
}
