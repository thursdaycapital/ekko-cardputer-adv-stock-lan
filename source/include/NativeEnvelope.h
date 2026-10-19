#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <cstring>

// Only the relay's wire event changes. The native payload retains its logical
// type and authentication/context fields for the desktop's normal dispatcher.
inline const char* nativeRemoteWireEvent(const char* logical, bool remote) {
  if (remote && logical &&
      (std::strcmp(logical, "mcu.agent.select") == 0 ||
       std::strcmp(logical, "mcu.chat.send") == 0 ||
       std::strcmp(logical, "mcu.chat.history") == 0)) return "mcu.status";
  return logical;
}

inline bool prepareNativeEnvelope(JsonDocument& document, const char* json,
                                  const char* token, const char* agent,
                                  uint32_t version) {
  if (deserializeJson(document, json) || !document.is<JsonObject>()) return false;
  if (token && *token) document["apiToken"] = token;
  if (document["agentId"].isUnbound()) document["agentId"] = agent;
  if (document["selectionVersion"].isUnbound()) document["selectionVersion"] = version;
  return true;
}

inline bool nativeEventContextMatches(const JsonDocument& document,
                                      const char* activeAgent, uint32_t version) {
  JsonVariantConst agent = document["agentId"];
  if (!agent.isUnbound() &&
      (!agent.is<const char*>() || std::strcmp(agent.as<const char*>(), activeAgent))) return false;
  JsonVariantConst receivedVersion = document["selectionVersion"];
  return receivedVersion.isUnbound() ||
      (receivedVersion.is<uint32_t>() && receivedVersion.as<uint32_t>() > 0 &&
       receivedVersion.as<uint32_t>() == version);
}
