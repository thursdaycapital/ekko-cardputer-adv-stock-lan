#include "OfficialStudioConnection.h"
#include "WebSocketFrameReader.h"
#include "DeviceWebTarget.h"
#include "McuReauth.h"
#include <cassert>
#include <cstdio>
#include <string>

static void officialRejectedJwtRequestsReauthentication() {
  // Actual official 0.7.36 /chat-run connect_error. A valid frame is not an
  // ACK: its exact fixed message must enter the saved-credential retry policy.
  const std::string packet = "44/chat-run,{\"message\":\"Authentication failed\"}";
  assert(official_connection::Namespaces::packet(packet.c_str(), "44/chat-run"));
  JsonDocument error;
  assert(!deserializeJson(error, packet.substr(packet.find(',') + 1)));
  const auto action = native_reauth::combineNamespaceActions(
      native_reauth::namespaceAction(error["code"] | "", error["message"] | ""),
      native_reauth::namespaceAction(error["data"]["code"] | "", error["data"]["message"] | ""));
  assert(action == native_reauth::NamespaceAction::Reauthenticate);
  error["data"]["message"] = "Profile access denied";
  assert(native_reauth::combineNamespaceActions(action,
      native_reauth::namespaceAction("", error["data"]["message"] | "")) ==
      native_reauth::NamespaceAction::RequireManual);
}

static void officialLoginAndNamespaceScopes() {
  JsonDocument document;
  document["url"] = "wss://synthetic-relay.invalid";
  document["remote"] = true;
  document["relayMode"] = "remote";
  official_connection::login(document, "device-instance-fixture", "fixture-user", "fixture-password");
  assert(document.size() == 4);
  assert(document["token"] == "device-instance-fixture");
  assert(document["id"] == "device-instance-fixture");
  assert(document["account"] == "fixture-user" && document["password"] == "fixture-password");
  for (const char* field : {"url", "remote", "relayMode", "profile", "role"})
    assert(document[field].isUnbound());

  official_connection::statusAuth(document, "fixture.jwt.token", "device-instance-fixture",
      "device-code-fixture", "工作 空间");
  assert(document.size() == 5 && document["token"] == "fixture.jwt.token");
  assert(document["role"] == "hermes-studio");
  assert(document["instanceId"] == "device-instance-fixture");
  assert(document["deviceCode"] == "device-code-fixture" && document["profile"] == "工作 空间");
  for (const char* field : {"password", "account", "id", "chatProtocol", "selectionVersion"})
    assert(document[field].isUnbound());

  official_connection::chatAuth(document, "fixture.jwt.token");
  assert(document.size() == 1 && document["token"] == "fixture.jwt.token");
  for (const char* field : {"password", "account", "role", "deviceCode", "instanceId", "profile"})
    assert(document[field].isUnbound());
  std::string encoded;
  serializeJson(document, encoded);
  assert(encoded == "{\"token\":\"fixture.jwt.token\"}");
}

static void stableWebTargetsSurviveDiscoveryReorder() {
  using namespace device_web_target;
  const View stock{"stock-machine|web|19038","http://192.0.2.10:19038","lan"};
  const View desktop{"desktop-machine|desktop|8748","http://192.0.2.10:8748","lan"};
  assert(valid(stock) && valid(desktop) && !same(stock,desktop));
  // Login page selected stock at index 0. Discovery then puts desktop first.
  // The submitted stable target still resolves stock; index 0 is irrelevant.
  const View reordered[]={desktop,stock};
  int selected=-1;
  for(int n=0;n<2;++n) if(same(stock,reordered[n])) selected=n;
  assert(selected==1 && !same(stock,reordered[0]));
  for(const View invalid: {View{nullptr,stock.url,"lan"},View{stock.key,"","lan"},
      View{stock.key,stock.url,nullptr},View{stock.key,stock.url,""},
      View{stock.key,stock.url,"other"},View{"stock-machine|web|0",stock.url,"lan"},
      View{stock.key,"http://user:password@192.0.2.10:19038","lan"},
      View{stock.key,"http://192.0.2.10:19038\r\nInjected: x","lan"}})
    assert(!valid(invalid) && !same(invalid,stock));
  assert(!same({stock.key,desktop.url,"lan"},stock)); // Port remains part of target.
  assert(!same({desktop.key,stock.url,"lan"},stock)); // URL match cannot replace identity.
  assert(!same({stock.key,stock.url,"remote"},stock));
  const char* oldProof="0123456789abcdef0123456789abcdef";
  const char* newProof="abcdef0123456789abcdef0123456789";
  assert(profileMatches(stock,stock,stock,oldProof,oldProof));
  assert(!profileMatches(stock,desktop,desktop,oldProof,oldProof));
  assert(!profileMatches(stock,stock,desktop,oldProof,oldProof));
  assert(!profileMatches(stock,stock,stock,oldProof,newProof)); // Same target, newer login/page.
  assert(!profileMatches(stock,stock,stock,"",oldProof));
  assert(!profileMatches(stock,stock,stock,oldProof,"")); // Completed form cannot replay.
  assert(!validProof("short") && !validProof("0123456789abcdef0123456789abcdefx"));
}

static void serializationPreservesCompleteSocketIoFrame() {
  JsonDocument body;
  body["token"] = "fixture.jwt.token";
  std::string broken = "40/chat-run,";
  serializeJson(body, broken);
  // The real pinned ArduinoJson std::string writer also clears its output.
  // An auth-body-only test would pass while the on-wire frame was malformed.
  assert(broken == "{\"token\":\"fixture.jwt.token\"}");
  assert(broken.find("40/chat-run,") == std::string::npos);

  const auto connect = official_connection::objectFrame<std::string>("40/chat-run,", body);
  assert(connect == "40/chat-run,{\"token\":\"fixture.jwt.token\"}");
  JsonDocument decoded;
  assert(!deserializeJson(decoded, connect.substr(std::strlen("40/chat-run,"))));
  assert(decoded["token"] == "fixture.jwt.token");

  const char* message = "你好，\"引号\"和反斜线\\\n第二行：42/chat-run,[\"run\",{}]";
  body.clear();
  body["input"] = message;
  body["profile"] = "项目 \"A\"\\测试";
  body["nested"]["ready"] = true;
  struct Event { const char* prefix; const char* name; };
  const Event events[] = {
    {"42/global-agent,[\"mcu.ready\",", "mcu.ready"},
    {"42/global-agent,[\"mcu.status\",", "mcu.status"},
    {"42/chat-run,[\"run\",", "run"},
    {"42/chat-run,[\"resume\",", "resume"},
  };
  for (const auto& event : events) {
    const auto frame = official_connection::objectFrame<std::string>(event.prefix, body, "]");
    assert(frame.compare(0, std::strlen(event.prefix), event.prefix) == 0);
    assert(frame.back() == ']');
    const size_t array = frame.find('[');
    assert(array != std::string::npos);
    decoded.clear();
    assert(!deserializeJson(decoded, frame.substr(array)));
    assert(decoded.is<JsonArray>() && decoded.size() == 2);
    assert(decoded[0] == event.name);
    assert(decoded[1]["input"] == message);
    assert(decoded[1]["profile"] == "项目 \"A\"\\测试");
    assert(decoded[1]["nested"]["ready"].as<bool>());
  }
}

static void independentAcknowledgementsAndBoundaries() {
  official_connection::Namespaces namespaces;
  assert(!namespaces.chat() && !namespaces.status());
  assert(namespaces.acknowledge("40/global-agent,{\"sid\":\"presence\"}"));
  assert(namespaces.status() && !namespaces.chat()); // Presence cannot authorize a send.
  assert(namespaces.acknowledge("40/chat-run,{\"sid\":\"chat\"}"));
  assert(namespaces.chat() && namespaces.status());
  namespaces.statusDisconnected();
  assert(namespaces.chat() && !namespaces.status()); // Presence failure preserves live chat.
  namespaces.statusDisconnected();
  assert(namespaces.chat() && !namespaces.status()); // Repeated status failures are harmless.
  assert(namespaces.acknowledge("40/global-agent,{\"sid\":\"presence-retry\"}"));
  assert(namespaces.chat() && namespaces.status());
  namespaces.reset();
  assert(!namespaces.chat() && !namespaces.status());
  namespaces.statusDisconnected();
  assert(!namespaces.chat() && !namespaces.status());
  assert(namespaces.acknowledge("40/chat-run"));
  assert(namespaces.chat() && !namespaces.status());
  namespaces.reset();
  const char* spoofed[] = {nullptr, "", "40", "40/chat", "40/chat-run-extra,{}",
       "40/chat-run/other,{}", "40/chat-run ,{}", "40/chat-runner,{}",
       "40/global-agent-other,{}", "40/global-agents,{}", "42/chat-run,[]",
       "44/chat-run,{\"message\":\"Authentication failed\"}",
       "40/other,{}", "x40/chat-run,{}"};
  for (const char* spoof : spoofed) {
    assert(!namespaces.acknowledge(spoof));
    assert(!namespaces.chat() && !namespaces.status());
  }
  assert(!official_connection::Namespaces::packet(nullptr, "40/chat-run"));
  assert(!official_connection::Namespaces::packet("40/chat-run", nullptr));
  assert(official_connection::Namespaces::packet("42/chat-run,[\"run.started\",{}]", "42/chat-run"));
  assert(!official_connection::Namespaces::packet("42/chat-run-other,[]", "42/chat-run"));
}

namespace {
class EngineOpenListener final : public studio_ws::Listener {
 public:
  std::string message;
  unsigned starts = 0, ends = 0;
  void reset() { message.clear(); starts = ends = 0; }
  bool messageBegin(uint8_t opcode) override { ++starts; return opcode == 1; }
  bool messageByte(uint8_t byte) override { message += static_cast<char>(byte); return true; }
  bool messageEnd() override { ++ends; return true; }
  bool control(uint8_t, const uint8_t*, size_t) override { return true; }
};
}

static void reconnectConsumesOldDeferredReset() {
  // Normal Engine.IO v4 open packet, sent as an unmasked RFC6455 text frame.
  // The TCP split can occur in either header byte or anywhere in its body.
  const std::string open = "0{\"sid\":\"AbcdEfghIjklMnopqrst\",\"upgrades\":[],"
      "\"pingInterval\":25000,\"pingTimeout\":20000,\"maxPayload\":1000000}";
  assert(open.size() < 126);
  std::string frame(1, static_cast<char>(0x81));
  frame += static_cast<char>(open.size());
  frame += open;
  const auto* bytes = reinterpret_cast<const uint8_t*>(frame.data());
  const size_t splits[] = {1, 2, 20, frame.size() - 2};
  for (const size_t split : splits) {
    {
      EngineOpenListener listener;
      studio_ws::Reader reader(listener);
      bool queued = true; // A previous transport queued deferred destruction.
      // Old reconnect reset: it starts a fresh reader but leaves this flag.
      reader.reset(); listener.reset();
      assert(reader.feed(bytes, split));
      if (queued) { reader.reset(); listener.reset(); queued = false; }
      // The next outer loop mistakes the rest of this frame for a new header.
      assert(!reader.feed(bytes + split, frame.size() - split));
      assert(reader.error() == studio_ws::Error::ReservedBits);
      assert(!listener.ends);
    }
    {
      EngineOpenListener listener;
      studio_ws::Reader reader(listener);
      bool queued = true;
      official_connection::resetParser(reader, listener, queued);
      assert(!queued && reader.idle());
      assert(reader.feed(bytes, split));
      // This is the real outer-loop policy. The explicit new-transport reset
      // consumed the old work, so no reset can erase the partial new frame.
      if (queued) official_connection::resetParser(reader, listener, queued);
      assert(reader.feed(bytes + split, frame.size() - split));
      assert(reader.idle() && reader.error() == studio_ws::Error::None);
      assert(listener.starts == 1 && listener.ends == 1 && listener.message == open);
      // A later genuine disconnect can queue and consume another clean reset.
      queued = true;
      official_connection::resetParser(reader, listener, queued);
      assert(!queued && reader.idle() && listener.message.empty());
    }
  }
}

int main() {
  officialRejectedJwtRequestsReauthentication();
  officialLoginAndNamespaceScopes();
  stableWebTargetsSurviveDiscoveryReorder();
  serializationPreservesCompleteSocketIoFrame();
  independentAcknowledgementsAndBoundaries();
  reconnectConsumesOldDeferredReset();
  std::puts("Official connection: complete serialized frames, LAN auth, stable web targets/Profile proofs, independent namespaces, spoof rejection and split-frame reconnect regression passed");
}
