#pragma once
#include <ArduinoJson.h>
#include <cstring>

namespace official_connection {
// ArduinoJson 7 clears String/std::string destinations before writing. Encode
// the object separately so namespace/event framing can never be overwritten.
template<class Text>
Text objectFrame(const char* prefix, const JsonDocument& body, const char* suffix = "") {
  Text json;
  serializeJson(body, json);
  Text frame(prefix);
  frame += json;
  frame += suffix;
  return frame;
}
// A reconnect may have queued cleanup for its old parser. An explicit reset
// starts the NEW transport, so that stale request must be consumed here too.
template<class Reader, class Listener>
void resetParser(Reader& reader, Listener& listener, bool& queued) {
  reader.reset(); listener.reset(); queued = false;
}
// One Engine.IO connection carries independent chat and device-presence
// namespaces. A presence acknowledgement must never unlock chat submission.
class Namespaces {
 public:
  void reset() { chat_ = status_ = false; }
  bool acknowledge(const char* frame) {
    if (packet(frame, "40/chat-run")) { chat_ = true; return true; }
    if (packet(frame, "40/global-agent")) { status_ = true; return true; }
    return false;
  }
  bool chat() const { return chat_; }
  bool status() const { return status_; }
  void statusDisconnected() { status_ = false; }
  static bool packet(const char* frame, const char* prefix) {
    if (!frame || !prefix) return false;
    const auto length = std::strlen(prefix);
    return !std::strncmp(frame, prefix, length) &&
        (frame[length] == '\0' || frame[length] == ',');
  }
 private:
  bool chat_ = false, status_ = false;
};

inline void login(JsonDocument& body, const char* instance,
                  const char* account, const char* password) {
  body.clear(); body["token"] = instance; body["id"] = instance;
  body["account"] = account; body["password"] = password;
  // url/relayMode/remote intentionally absent: this is the official LAN login.
}
inline void chatAuth(JsonDocument& body, const char* jwt) {
  body.clear(); body["token"] = jwt;
}
inline void statusAuth(JsonDocument& body, const char* jwt, const char* instance,
                       const char* code, const char* profile) {
  body.clear(); body["token"] = jwt; body["role"] = "hermes-studio";
  body["instanceId"] = instance; body["deviceCode"] = code; body["profile"] = profile;
}
}  // namespace official_connection
