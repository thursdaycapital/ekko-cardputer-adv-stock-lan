#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include "ChatUtf8.h"

// Official Studio 0.7.36 / 1242ac6, LAN HTTP + Socket.IO /chat-run only.
// Sources (relative to the Studio repository):
//   config/agents.json: canonical IDs and supported configuration modes.
//   packages/client/src/api/studio/chat.ts: StartRunRequest / RunEvent.
//   packages/client/src/components/hermes/chat/ChatPanel.vue: new-chat sources.
//   packages/server/src/modules/studio/contracts/runs/model-execution-identity.ts
//   packages/server/src/modules/studio/contracts/provider-compat.ts
//   packages/server/src/modules/studio/sockets/chat-run.ts: run/resume/resumed.
// No MCU extension, cloud carrier, credentials or server modification is used.
namespace official_studio {

enum class Mode : uint8_t { Scoped, Global };

struct AgentRoute {
  const char* id;
  const char* source;
  bool scoped;
  bool global;
};

constexpr AgentRoute kAgentRoutes[] = {
  {"ekko-agent", "builtin_agent", true, false},
  {"hermes", "cli", true, false},
  {"claude-code", "coding_agent", true, true},
  {"codex", "coding_agent", true, true},
  {"pi", "coding_agent", true, true},
  {"grok", "coding_agent", true, true},
  {"opencode", "coding_agent", true, true},
  {"dsh", "coding_agent", true, true},
  {"cursor", "coding_agent", false, true},
  {"antigravity", "coding_agent", true, true},
  {"qwen", "coding_agent", true, true},
  {"kimi", "coding_agent", true, true},
  {"codebuddy", "coding_agent", true, true},
  {"qoder", "coding_agent", false, true},
  {"copilot", "coding_agent", true, true},
  {"zcode", "coding_agent", true, true},
};
constexpr size_t kAgentCount = sizeof(kAgentRoutes) / sizeof(kAgentRoutes[0]);

inline bool equal(const char* left, const char* right) {
  return left && right && std::strcmp(left, right) == 0;
}

inline bool nonBlank(const char* value) {
  if (!value) return false;
  for (; *value; ++value) {
    const unsigned char c = static_cast<unsigned char>(*value);
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '\v') return true;
  }
  return false;
}

inline bool jsonStringEquals(JsonVariantConst value, const char* expected) {
  if (!expected || !value.is<const char*>()) return false;
  const JsonString actual = value.as<JsonString>();
  const size_t expectedBytes = std::strlen(expected);
  return actual.size() == expectedBytes && std::memcmp(actual.c_str(), expected, expectedBytes) == 0;
}

inline bool validOptionalId(JsonVariantConst value) {
  if (value.isUnbound() || value.isNull()) return true;
  if (!value.is<const char*>()) return false;
  const JsonString string = value.as<JsonString>();
  return string.size() == std::strlen(string.c_str());
}

inline const AgentRoute* agentRoute(const char* canonicalId) {
  for (const auto& route : kAgentRoutes) if (equal(route.id, canonicalId)) return &route;
  return nullptr;  // An unknown ID must never route to another Agent.
}

inline bool supportsMode(const AgentRoute& route, Mode mode) {
  return mode == Mode::Scoped ? route.scoped : mode == Mode::Global && route.global;
}

// These strings are owned by the caller. Capture a snapshot when opening the
// authenticated socket, and keep its IMMUTABLE string storage alive until all
// its events drain (a shallow Context copy does not copy string buffers).
// machine/account/agent/mode/epoch are LOCAL isolation data, never wire fields.
// Session IDs are opaque persisted IDs, not the catalog's display/session alias.
struct Context {
  const char* machineId = nullptr;
  const char* accountId = nullptr;
  const char* profile = nullptr;
  const char* agentId = nullptr;
  const char* sessionId = nullptr;
  Mode mode = Mode::Scoped;
  uint32_t connectionEpoch = 0;
};

inline bool validContext(const Context& context) {
  const auto* route = agentRoute(context.agentId);
  return nonBlank(context.machineId) && nonBlank(context.accountId) && nonBlank(context.profile) &&
      nonBlank(context.sessionId) && context.connectionEpoch != 0 && route && supportsMode(*route, context.mode);
}

inline bool sameConversation(const Context& left, const Context& right) {
  return validContext(left) && validContext(right) && left.mode == right.mode &&
      equal(left.machineId, right.machineId) && equal(left.accountId, right.accountId) &&
      equal(left.profile, right.profile) && equal(left.agentId, right.agentId) && equal(left.sessionId, right.sessionId);
}

inline bool sameConnection(const Context& left, const Context& right) {
  return sameConversation(left, right) && left.connectionEpoch == right.connectionEpoch;
}

struct ModelIdentity {
  const char* provider = nullptr;
  const char* model = nullptr;
  const char* apiMode = nullptr;
};

inline bool validApiMode(const char* value) {
  return equal(value, "chat_completions") || equal(value, "codex_responses") || equal(value, "anthropic_messages");
}

inline bool validModelIdentity(const ModelIdentity& model) {
  return nonBlank(model.provider) && nonBlank(model.model) && validApiMode(model.apiMode);
}

inline size_t jsWhitespaceBytes(const char* value) {
  if (!value) return 0;
  const auto* bytes = reinterpret_cast<const unsigned char*>(value);
  if (bytes[0] == 0x20 || (bytes[0] >= 0x09 && bytes[0] <= 0x0d)) return 1;
  if (bytes[0] == 0xc2 && bytes[1] == 0xa0) return 2;
  if (bytes[0] == 0xe1 && bytes[1] == 0x9a && bytes[2] == 0x80) return 3;
  if (bytes[0] == 0xe2 && bytes[1] == 0x80 &&
      ((bytes[2] >= 0x80 && bytes[2] <= 0x8a) || bytes[2] == 0xa8 || bytes[2] == 0xa9 || bytes[2] == 0xaf)) return 3;
  if (bytes[0] == 0xe2 && bytes[1] == 0x81 && bytes[2] == 0x9f) return 3;
  if (bytes[0] == 0xe3 && bytes[1] == 0x80 && bytes[2] == 0x80) return 3;
  if (bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) return 3;
  return 0;
}

inline const char* skipJsWhitespace(const char* value) {
  while (const size_t bytes = jsWhitespaceBytes(value)) value += bytes;
  return value;
}

struct InputView { const char* data = nullptr; size_t bytes = 0; bool valid = false; };

// The stock coding runtime stores String(input).trim(), whereas the device
// draft keeps its exact original contents. Do not collapse internal whitespace.
inline InputView storedCodingInput(const char* input) {
  InputView out;
  if(!input) return out;
  const size_t bytes=std::strlen(input);
  if(!native_chat::validUtf8(input,bytes)) return out;
  out.data=skipJsWhitespace(input);const char* end=out.data;
  for(const char* cursor=out.data;*cursor;) {
    const size_t whitespace=jsWhitespaceBytes(cursor);
    if(whitespace) cursor+=whitespace;
    else {cursor+=native_chat::codepointBytes(static_cast<uint8_t>(*cursor));end=cursor;}
  }
  out.bytes=static_cast<size_t>(end-out.data);out.valid=true;return out;
}

inline bool scopedProviderAllowed(const AgentRoute& route, const char* provider) {
  if (!equal(route.source, "coding_agent")) return true;
  // Exact official subscription/OAuth restriction; no invented provider allowlist.
  constexpr const char* blocked[] = {"openai-codex", "copilot", "xai-oauth", "qwen-oauth", "nous", "claude-oauth", "minimax-oauth"};
  for (const auto* value : blocked) {
    // Provider identity itself remains unchanged on the wire. The official
    // compatibility check compares a trimmed lower-case provider name.
    const char* candidate = skipJsWhitespace(provider);
    size_t index = 0;
    while (candidate && candidate[index] && value[index]) {
      char c = candidate[index];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
      if (c != value[index]) break;
      ++index;
    }
    if (value[index] == '\0' && candidate) {
      candidate = skipJsWhitespace(candidate + index);
      if (!*candidate) return false;
    }
  }
  return true;
}

enum class Result : uint8_t {
  Ok, InvalidContext, UnsupportedAgent, UnsupportedMode, MissingModelIdentity,
  UnsupportedApiMode, UnsupportedProvider, EmptyInput, MissingRequestId,
  NotInstalled, NativeNotReady, NativeModelUnavailable, AuthenticationRequired,
  RetryableUnavailable, InvalidPayload,
  StaleContext, WrongSession, WrongRun, WrongRequest, UnknownEvent, NeedsReconcile, OutOfMemory
};

inline Result validateRun(const Context& context, const char* input, const char* queueId) {
  const auto* route = agentRoute(context.agentId);
  if (!route) return Result::UnsupportedAgent;
  if (!supportsMode(*route, context.mode)) return Result::UnsupportedMode;
  if (!validContext(context)) return Result::InvalidContext;
  if (!nonBlank(input)) return Result::EmptyInput;
  if (!nonBlank(queueId)) return Result::MissingRequestId;
  return Result::Ok;
}

inline void writeRunBase(JsonDocument& document, const Context& context, const char* input, const char* queueId) {
  const auto& route = *agentRoute(context.agentId);
  document["source"] = route.source;
  document["input"] = input;
  document["session_id"] = context.sessionId;
  document["profile"] = context.profile;
  document["queue_id"] = queueId;
  if (!equal(route.id, "hermes")) {
    document["coding_agent_id"] = route.id;
    document["mode"] = context.mode == Mode::Scoped ? "scoped" : "global";
    if (equal(route.id, "ekko-agent")) document["agent_id"] = route.id;
  }
}

inline Result finishPayload(JsonDocument& document) {
  if (!document.overflowed()) return Result::Ok;
  document.clear();
  return Result::OutOfMemory;
}

// All generic/scoped runs carry an explicit execution identity. The official
// server deliberately does not fill missing fields from Profile defaults.
inline Result buildScopedRun(JsonDocument& document, const Context& context, const ModelIdentity& model,
                             const char* input, const char* queueId) {
  document.clear();
  const Result validation = validateRun(context, input, queueId);
  if (validation != Result::Ok) return validation;
  if (context.mode != Mode::Scoped) return Result::UnsupportedMode;
  if (!nonBlank(model.provider) || !nonBlank(model.model) || !nonBlank(model.apiMode)) return Result::MissingModelIdentity;
  if (!validApiMode(model.apiMode)) return Result::UnsupportedApiMode;
  if (!scopedProviderAllowed(*agentRoute(context.agentId), model.provider)) return Result::UnsupportedProvider;
  writeRunBase(document, context, input, queueId);
  document["provider"] = model.provider;
  document["model"] = model.model;
  document["apiMode"] = model.apiMode;
  return finishPayload(document);
}

// Readiness is tied to the authenticated connection that fetched it. The input
// JSON is the official /api/coding-agents and /api/coding-agents/models response.
// An empty selected model means the native CLI default (the official UI uses it).
// Discovery "unsupported" does not prohibit that default; it is different from
// auth_required/not_installed. timeout/error are explicit retryable failures.
struct NativeReadiness {
  Context context;
  Result result = Result::NativeNotReady;
  const char* model = nullptr;  // Borrowed from the caller, never resolvedModel.
};

inline NativeReadiness inspectNativeReadiness(const Context& context, JsonVariantConst installed,
                                            JsonVariantConst catalogs, const char* selectedModel = nullptr) {
  NativeReadiness out;
  out.context = context;
  out.model = selectedModel;
  if (!validContext(context)) { out.result = Result::InvalidContext; return out; }
  if (context.mode != Mode::Global) { out.result = Result::UnsupportedMode; return out; }
  if (!installed["tools"].is<JsonArrayConst>() || !catalogs["agents"].is<JsonArrayConst>()) {
    out.result = Result::InvalidPayload; return out;
  }
  bool foundInstalled = false;
  for (JsonObjectConst tool : installed["tools"].as<JsonArrayConst>()) {
    if (jsonStringEquals(tool["id"], context.agentId)) {
      foundInstalled = tool["installed"].is<bool>() && tool["installed"].as<bool>();
      break;
    }
  }
  if (!foundInstalled) { out.result = Result::NotInstalled; return out; }
  for (JsonObjectConst catalog : catalogs["agents"].as<JsonArrayConst>()) {
    if (!jsonStringEquals(catalog["agentId"], context.agentId)) continue;
    const JsonVariantConst status = catalog["status"];
    if (jsonStringEquals(status, "not_installed")) { out.result = Result::NotInstalled; return out; }
    if (jsonStringEquals(status, "auth_required")) { out.result = Result::AuthenticationRequired; return out; }
    if (jsonStringEquals(status, "timeout") || jsonStringEquals(status, "error")) {
      out.result = Result::RetryableUnavailable; return out;
    }
    const bool ready = jsonStringEquals(status, "ready");
    if (!ready && !jsonStringEquals(status, "empty") && !jsonStringEquals(status, "unsupported")) return out;
    if (!catalog["models"].is<JsonArrayConst>()) { out.result = Result::InvalidPayload; return out; }
    if (!nonBlank(selectedModel)) { out.result = Result::Ok; return out; }
    if (!ready) { out.result = Result::NativeModelUnavailable; return out; }
    for (JsonObjectConst model : catalog["models"].as<JsonArrayConst>()) {
      if (jsonStringEquals(model["id"], selectedModel) && !model["hidden"].as<bool>()) {
        out.result = Result::Ok; return out;
      }
    }
    out.result = Result::NativeModelUnavailable;
    return out;
  }
  return out;
}

inline Result buildNativeGlobalRun(JsonDocument& document, const Context& context, const NativeReadiness& readiness,
                                   const char* input, const char* queueId) {
  document.clear();
  const Result validation = validateRun(context, input, queueId);
  if (validation != Result::Ok) return validation;
  if (context.mode != Mode::Global) return Result::UnsupportedMode;
  if (!sameConnection(context, readiness.context)) return Result::StaleContext;
  if (readiness.result != Result::Ok) return readiness.result;
  writeRunBase(document, context, input, queueId);
  if (nonBlank(readiness.model)) document["model"] = readiness.model;
  // Socket.IO /chat-run only: REST /api/studio/chat-run/runs also demands the
  // scoped identity for coding runs and is NOT this native-global API's carrier.
  // Native global mode owns its provider/auth/config; do not invent a provider,
  // apiMode, API key or proxy URL merely to satisfy the scoped-mode contract.
  return finishPayload(document);
}

inline Result buildSessionCommand(JsonDocument& document, const Context& context) {
  document.clear();
  if (!validContext(context)) return Result::InvalidContext;
  document["session_id"] = context.sessionId;
  return finishPayload(document);  // Official resume and abort use this payload.
}

enum class EventType : uint8_t { Started, Delta, Completed, Failed, Resumed };

// Borrowed views: the source JsonDocument AND Context strings must outlive use.
// No delta/output/history is truncated here. The transport must bound memory,
// stream large JSON, and report unsupported oversized frames explicitly.
struct EventView {
  EventType type = EventType::Failed;
  const char* sessionId = nullptr;
  const char* runId = nullptr;
  const char* queueId = nullptr;
  const char* text = nullptr;
  size_t textBytes = 0;  // Use the length, not strlen(): JSON text can contain NUL.
  JsonVariantConst error;
  JsonArrayConst messages;
  JsonArrayConst events;
  bool isWorking = false;
  // Official coding events may omit queue_id. Ok validates socket/session
  // scope, not an otherwise absent per-request correlation ID. The caller must
  // own one pending send, bind run.started's run_id, and reconcile ambiguous
  // session failures; never automatically replay an uncertain write.
  bool requestCorrelated = false;
};

inline Result parseEvent(const char* eventName, JsonVariantConst payload, const Context& active,
                         const Context& source, EventView& out, const char* expectedRunId = nullptr,
                         const char* expectedQueueId = nullptr) {
  out = EventView{};
  if (!sameConnection(active, source)) return Result::StaleContext;
  if (!payload.is<JsonObjectConst>()) return Result::InvalidPayload;
  const char* taggedName = payload["event"].as<const char*>();
  if (!payload["event"].isUnbound() && (!taggedName || !jsonStringEquals(payload["event"], eventName))) return Result::InvalidPayload;
  EventType type;
  if (equal(eventName, "run.started")) type = EventType::Started;
  else if (equal(eventName, "message.delta")) type = EventType::Delta;
  else if (equal(eventName, "run.completed")) type = EventType::Completed;
  else if (equal(eventName, "run.failed")) type = EventType::Failed;
  else if (equal(eventName, "resumed")) type = EventType::Resumed;
  else return Result::UnknownEvent;
  const char* sessionId = payload["session_id"].as<const char*>();
  if (!jsonStringEquals(payload["session_id"], active.sessionId)) return Result::WrongSession;
  const char* runId = payload["run_id"].as<const char*>();
  const char* queueId = payload["queue_id"].as<const char*>();
  if (!validOptionalId(payload["run_id"]) || !validOptionalId(payload["queue_id"])) return Result::InvalidPayload;
  if (nonBlank(expectedRunId) && nonBlank(runId) && !equal(expectedRunId, runId)) return Result::WrongRun;
  if (nonBlank(expectedQueueId) && nonBlank(queueId) && !equal(expectedQueueId, queueId)) return Result::WrongRequest;
  const bool correlated = (nonBlank(expectedRunId) && nonBlank(runId) && equal(expectedRunId, runId)) ||
      (nonBlank(expectedQueueId) && nonBlank(queueId) && equal(expectedQueueId, queueId));
  const bool sessionFailure = type == EventType::Failed && !nonBlank(runId) && !correlated;
  if (nonBlank(expectedRunId) && !nonBlank(runId) && type != EventType::Resumed &&
      !sessionFailure && !(type == EventType::Failed && correlated)) return Result::WrongRun;
  if (type == EventType::Delta && !payload["delta"].is<const char*>()) return Result::InvalidPayload;
  if (type == EventType::Completed && !payload["output"].isUnbound() &&
      !payload["output"].isNull() && !payload["output"].is<const char*>()) return Result::InvalidPayload;
  if (type == EventType::Resumed) {
    if (!payload["isWorking"].is<bool>()) return Result::InvalidPayload;
    if ((!payload["messages"].isUnbound() && !payload["messages"].is<JsonArrayConst>()) ||
        (!payload["events"].isUnbound() && !payload["events"].is<JsonArrayConst>())) return Result::InvalidPayload;
  }
  out.type = type;
  out.sessionId = sessionId;
  out.runId = runId;
  out.queueId = queueId;
  out.text = type == EventType::Delta ? payload["delta"].as<const char*>() :
      type == EventType::Completed ? payload["output"].as<const char*>() : nullptr;
  out.textBytes = out.text ? payload[type == EventType::Delta ? "delta" : "output"].as<JsonString>().size() : 0;
  out.error = payload["error"];
  out.messages = payload["messages"].as<JsonArrayConst>();
  out.events = payload["events"].as<JsonArrayConst>();
  out.isWorking = payload["isWorking"].as<bool>();
  out.requestCorrelated = correlated;
  return sessionFailure ? Result::NeedsReconcile : Result::Ok;
}

}  // namespace official_studio
