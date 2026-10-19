#include "NativeEnvelope.h"
#include <cassert>
#include <cstdio>
#include <string>

namespace {

void remoteWireMapping() {
  for (const char* logical : {"mcu.agent.select", "mcu.chat.send", "mcu.chat.history"}) {
    assert(std::strcmp(nativeRemoteWireEvent(logical, true), "mcu.status") == 0);
    assert(nativeRemoteWireEvent(logical, false) == logical);
  }
  for (const char* logical : {"mcu.ready", "mcu.status", "voice.stream.start",
      "voice.stream.chunk", "voice.stream.end", "voice.stream.abort", "voice.recorded",
      "mcu.interrupt", "mcu.session.clear", "audio.started", "audio.done",
      "audio.interrupted", "mcu.event", "unknown.event", "mcu.chat.send.extra", ""}) {
    assert(nativeRemoteWireEvent(logical, true) == logical);
    assert(nativeRemoteWireEvent(logical, false) == logical);
  }
  assert(nativeRemoteWireEvent(nullptr, true) == nullptr);
  assert(nativeRemoteWireEvent(nullptr, false) == nullptr);
}

void remotePayloadRoundTrips() {
  const char* payloads[] = {
    R"({"type":"mcu.agent.select","agentId":"claude-code","profile":"中文工作区","requestId":"select-1","selectionVersion":7})",
    R"({"type":"mcu.chat.send","agentId":"claude-code","profile":"中文工作区","requestId":"send-2","selectionVersion":7,"text":"你好，请回复：远程连接成功。"})",
    R"({"type":"mcu.chat.history","agentId":"claude-code","profile":"中文工作区","requestId":"history-3","selectionVersion":7,"messageId":"历史消息","offset":128,"limit":1024})",
  };
  const char* logicalEvents[] = {"mcu.agent.select", "mcu.chat.send", "mcu.chat.history"};
  for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); ++i) {
    JsonDocument payload;
    assert(prepareNativeEnvelope(payload, payloads[i], "host-test-token", "hermes", 11));
    std::string original;
    serializeJson(payload, original);

    const char* logical = payload["type"].as<const char*>();
    JsonDocument frame;
    auto packet = frame.to<JsonArray>();
    packet.add(nativeRemoteWireEvent(logical, true));
    packet.add(payload.as<JsonObjectConst>());
    std::string wire;
    serializeJson(frame, wire);
    JsonDocument received;
    assert(!deserializeJson(received, wire));
    assert(received.is<JsonArray>() && received.size() == 2);
    assert(received[0] == "mcu.status");
    assert(received[1]["type"] == logicalEvents[i]);
    assert(received[1]["agentId"] == "claude-code");
    assert(received[1]["profile"] == "中文工作区");
    assert(received[1]["apiToken"] == "host-test-token");
    assert(received[1]["selectionVersion"] == 7);
    assert(received[1]["requestId"] == payload["requestId"]);
    if (i == 1) assert(received[1]["text"] == "你好，请回复：远程连接成功。");
    if (i == 2) {
      assert(received[1]["messageId"] == "历史消息");
      assert(received[1]["offset"] == 128 && received[1]["limit"] == 1024);
    }
    std::string decodedPayload, unchangedPayload;
    serializeJson(received[1], decodedPayload);
    serializeJson(payload, unchangedPayload);
    assert(decodedPayload == original && unchangedPayload == original);
  }
}

}  // namespace

int main() {
  remoteWireMapping();
  remotePayloadRoundTrips();
  JsonDocument doc, parsed;
  assert(prepareNativeEnvelope(doc, "{}", "test-token", "codex", 3));
  std::string wire;
  serializeJson(doc, wire);
  assert(!deserializeJson(parsed, wire));
  assert(parsed["agentId"] == "codex" && parsed["selectionVersion"] == 3);
  assert(parsed["apiToken"] == "test-token");
  assert(prepareNativeEnvelope(doc, R"({"meta":{"agentId":"old","apiToken":"nested"},"text":"apiToken"})",
                               "real-token", "pi", 4));
  assert(doc["agentId"] == "pi" && doc["apiToken"] == "real-token");
  assert(doc["meta"]["agentId"] == "old");
  assert(prepareNativeEnvelope(doc, R"({"type":"mcu.agent.select","agentId":"cursor"})",
                               "token", "codex", 5));
  assert(doc["agentId"] == "cursor"); // selection target is explicit
  assert(!prepareNativeEnvelope(doc, "[]", "token", "codex", 3));
  assert(!prepareNativeEnvelope(doc, "{broken}", "token", "codex", 3));
  for (const char* invalid : {
      R"({"agentId":"pi","selectionVersion":3})",
      R"({"agentId":"codex","selectionVersion":1})",
      R"({"agentId":"codex","selectionVersion":0})",
      R"({"agentId":"codex","selectionVersion":"3"})",
      R"({"agentId":"codex","selectionVersion":null})",
      R"({"agentId":null,"selectionVersion":3})"}) {
    assert(!deserializeJson(doc, invalid));
    assert(!nativeEventContextMatches(doc, "codex", 3));
  }
  assert(!deserializeJson(doc, R"({"agentId":"codex","selectionVersion":3})"));
  assert(nativeEventContextMatches(doc, "codex", 3));
  assert(!deserializeJson(doc, R"({"type":"mcu.auth"})"));
  assert(nativeEventContextMatches(doc, "codex", 3));
  std::puts("native envelope, remote wire/payload and stale-event tests passed");
}
