#pragma once
#include "ManagementRequestGate.h"
#include <WebServer.h>
#include <array>
#include <cerrno>
#include <lwip/sockets.h>

// Adapter for Arduino-ESP32 2.0.17's protected WebServer state machine. It
// accepts a fresh client itself so base.handleClient() never reaches the
// synchronous parser until this complete bounded request is already buffered.
// This bounds REQUEST waiting only. Existing handlers/response writes are
// still synchronous; it is not an asynchronous HTTP server or an audio bridge.
class BoundedManagementServer:public WebServer {
 public:
  using WebServer::WebServer;
  bool hasPendingManagement(){return bool(_currentClient)||_server.hasClient();}
  void handleClient()override {
    if(_currentStatus==HC_NONE) {
      _currentClient=_server.available();
      if(!_currentClient)return;
      _currentStatus=HC_WAIT_READ;_statusChange=millis();gate_=management_request::Gate{};
      // pinned WiFiClient::available()/connected() do not consume RX bytes;
      // its private rxBuffer remains empty until base._parseRequest runs.
      // Stream timeout zero is a defensive final check after the byte gate.
      static_cast<Stream&>(_currentClient).setTimeout(0);
    }
    if(_currentStatus!=HC_WAIT_READ){drop();return;}
    if(uint32_t(millis()-_statusChange)>=management_request::kDeadlineMs){drop();return;}
    const int descriptor=_currentClient.fd();
    if(descriptor<0){drop();return;}
    const int count=recv(descriptor,peek_.data(),peek_.size(),MSG_PEEK|MSG_DONTWAIT);
    if(count<0) {
      if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return;
      drop();return;
    }
    if(!count){drop();return;}
    // Recheck after peek, because more TCP bytes may have arrived during recv.
    const int available=_currentClient.available();
    if(available<count){drop();return;}
    const auto result=gate_.inspect(peek_.data(),size_t(count),size_t(available),millis()-_statusChange);
    clearPeek(); // A peek may include login-form bytes after its header.
    if(result==management_request::Result::Rejected){drop();return;}
    if(result==management_request::Result::Waiting)return;
    // Neither Gate nor recv has consumed a byte. The pinned base implementation
    // now reads the original request using its normal native routes/form logic.
    WebServer::handleClient();
    if(_currentStatus==HC_NONE)gate_=management_request::Gate{};
  }
 private:
  management_request::Gate gate_;
  std::array<uint8_t,management_request::kHeaderBytes> peek_{};
  void clearPeek(){auto* p=static_cast<volatile uint8_t*>(peek_.data());for(size_t n=0;n<peek_.size();++n)p[n]=0;}
  void drop() {
    clearPeek();
    _currentClient.stop();_currentClient=WiFiClient();_currentStatus=HC_NONE;
    _currentUpload.reset();_currentRaw.reset();gate_=management_request::Gate{};
  }
};
