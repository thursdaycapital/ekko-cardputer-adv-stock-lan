#include "OfficialStudioProtocol.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace official_studio;

namespace {

Context context(const char* agent = "ekko-agent", Mode mode = Mode::Scoped) {
  Context value;
  value.machineId = "machine-official-fixture";
  value.accountId = "user-7";
  value.profile = "工作 空间";
  value.agentId = agent;
  value.sessionId = "95f54aaf-c3b0-4ba6-9547-9b1111550000";
  value.mode = mode;
  value.connectionEpoch = 5;
  return value;
}

ModelIdentity model() {
  ModelIdentity value;
  value.provider = "custom:studio-fixture";
  value.model = "fixture-model";
  value.apiMode = "anthropic_messages";
  return value;
}

void assertNoSecretsOrPrivateFields(const JsonDocument& document) {
  for (const char* key : {"apiKey", "api_key", "baseUrl", "base_url", "apiToken", "token",
       "type", "agentId", "selectionVersion", "chatProtocol", "machineId", "accountId", "connectionEpoch"}) {
    assert(document[key].isUnbound());
  }
}

void genericRoutingAndModes() {
  assert(kAgentCount == 16);
  for (const auto& route : kAgentRoutes) {
    const auto ctx = context(route.id);
    JsonDocument document;
    document["apiKey"] = "must-be-cleared";
    const auto result = buildScopedRun(document, ctx, model(), "你好\nquote: \"hello\"", "queue-1");
    if (!route.scoped) {
      assert(result == Result::UnsupportedMode && document.size() == 0);
      continue;
    }
    assert(result == Result::Ok);
    assert(document["source"] == route.source);
    assert(document["profile"] == ctx.profile);
    assert(document["session_id"] == ctx.sessionId);
    assert(document["input"] == "你好\nquote: \"hello\"");
    assert(document["queue_id"] == "queue-1");
    assert(document["provider"] == "custom:studio-fixture");
    assert(document["model"] == "fixture-model");
    assert(document["apiMode"] == "anthropic_messages");
    if (equal(route.id, "hermes")) {
      assert(document["coding_agent_id"].isUnbound() && document["mode"].isUnbound());
    } else {
      assert(document["coding_agent_id"] == route.id && document["mode"] == "scoped");
      if (equal(route.id, "ekko-agent")) assert(document["agent_id"] == "ekko-agent");
      else assert(document["agent_id"].isUnbound());
    }
    assertNoSecretsOrPrivateFields(document);
    std::string json;
    serializeJson(document, json);
    JsonDocument decoded;
    assert(!deserializeJson(decoded, json));
    assert(decoded["input"] == document["input"]);
  }
  for (const char* alias : {"claude", "ekko", "ekko_agent", "Hermes", "", "unknown", "codex "}) {
    JsonDocument document;
    assert(agentRoute(alias) == nullptr);
    assert(buildScopedRun(document, context(alias), model(), "test", "queue") == Result::UnsupportedAgent);
    assert(document.size() == 0);
  }
  assert(agentRoute(nullptr) == nullptr);
}

void modelIdentityIsExplicit() {
  const auto ctx = context("claude-code");
  for (const char* apiMode : {"chat_completions", "codex_responses", "anthropic_messages"}) {
    auto selected = model();
    selected.apiMode = apiMode;
    JsonDocument document;
    assert(buildScopedRun(document, ctx, selected, "hi", "queue") == Result::Ok);
    assert(document["apiMode"] == apiMode);
  }
  for (int field = 0; field < 3; ++field) {
    for (const char* missing : {static_cast<const char*>(nullptr), "", " \n\t"}) {
      auto selected = model();
      if (field == 0) selected.provider = missing;
      if (field == 1) selected.model = missing;
      if (field == 2) selected.apiMode = missing;
      JsonDocument document;
      document["old"] = 1;
      assert(buildScopedRun(document, ctx, selected, "hi", "q") == Result::MissingModelIdentity);
      assert(document.size() == 0);
    }
  }
  for (const char* unsupported : {"codex_app_server", "bedrock_converse", "openai", "Chat_Completions", " chat_completions"}) {
    auto selected = model();
    selected.apiMode = unsupported;
    JsonDocument document;
    assert(buildScopedRun(document, ctx, selected, "hi", "q") == Result::UnsupportedApiMode);
    assert(document.size() == 0);
  }
  for (const char* blocked : {"openai-codex", "copilot", "xai-oauth", "qwen-oauth", "nous", "claude-oauth", "minimax-oauth", "\v CLAUDE-OAUTH \t\f", "\xc2\xa0" "claude-oauth" "\xe3\x80\x80"}) {
    auto selected = model();
    selected.provider = blocked;
    JsonDocument document;
    assert(buildScopedRun(document, ctx, selected, "hi", "q") == Result::UnsupportedProvider);
    assert(document.size() == 0);
    assert(buildScopedRun(document, context("ekko-agent"), selected, "hi", "q") == Result::Ok);
    assert(buildScopedRun(document, context("hermes"), selected, "hi", "q") == Result::Ok);
  }
  auto selected = model();
  selected.provider = "custom:claude-oauth";  // A provider key is not its substring.
  JsonDocument document;
  assert(buildScopedRun(document, ctx, selected, "hi", "q") == Result::Ok);
  for (const char* blank : {static_cast<const char*>(nullptr), "", " \n\t"}) {
    assert(buildScopedRun(document, ctx, model(), blank, "q") == Result::EmptyInput && document.size() == 0);
    assert(buildScopedRun(document, ctx, model(), "hi", blank) == Result::MissingRequestId && document.size() == 0);
  }
}

void contextIsolation() {
  const auto active = context("claude-code");
  assert(validContext(active) && sameConversation(active, active) && sameConnection(active, active));
  for (int field = 0; field < 7; ++field) {
    auto changed = active;
    switch (field) {
      case 0: changed.machineId = "another-machine"; break;
      case 1: changed.accountId = "another-account"; break;
      case 2: changed.profile = "工作-空间"; break;
      case 3: changed.agentId = "codex"; break;
      case 4: changed.sessionId = "other-session"; break;
      case 5: changed.mode = Mode::Global; break;
      case 6: ++changed.connectionEpoch; break;
    }
    assert(!sameConnection(active, changed));
    assert(sameConversation(active, changed) == (field == 6));
  }
  for (int field = 0; field < 7; ++field) {
    auto invalid = active;
    switch (field) {
      case 0: invalid.machineId = nullptr; break;
      case 1: invalid.accountId = ""; break;
      case 2: invalid.profile = "  "; break;
      case 3: invalid.agentId = "claude"; break;
      case 4: invalid.sessionId = nullptr; break;
      case 5: invalid.mode = static_cast<Mode>(99); break;
      case 6: invalid.connectionEpoch = 0; break;
    }
    assert(!validContext(invalid) && !sameConnection(active, invalid));
    JsonDocument document;
    document["old"] = true;
    assert(buildSessionCommand(document, invalid) == Result::InvalidContext && document.size() == 0);
  }
  JsonDocument document;
  document["token"] = "must-be-cleared";
  assert(buildSessionCommand(document, active) == Result::Ok);
  assert(document.size() == 1 && document["session_id"] == active.sessionId);
}

NativeReadiness readiness(const Context& ctx, const char* nativeModel = nullptr, const char* status = "ready", bool installed = true) {
  JsonDocument tools, catalogs;
  tools["tools"][0]["id"] = ctx.agentId;
  tools["tools"][0]["installed"] = installed;
  catalogs["agents"][0]["agentId"] = ctx.agentId;
  catalogs["agents"][0]["status"] = status;
  auto models = catalogs["agents"][0]["models"].to<JsonArray>();
  models.add<JsonObject>()["id"] = "vendor/native-model";
  auto hidden = models.add<JsonObject>();
  hidden["id"] = "hidden-model"; hidden["hidden"] = true;
  // inspectNativeReadiness returns only caller-owned context/model references,
  // never references to the HTTP documents that are destroyed here.
  return inspectNativeReadiness(ctx, tools.as<JsonVariantConst>(), catalogs.as<JsonVariantConst>(), nativeModel);
}

void nativeGlobalRouting() {
  for (const auto& route : kAgentRoutes) {
    auto ctx = context(route.id, Mode::Global);
    if (!route.global) {
      assert(!validContext(ctx));
      continue;
    }
    JsonDocument document;
    assert(buildScopedRun(document, ctx, model(), "hi", "q") == Result::UnsupportedMode);
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx), "hi", "q") == Result::Ok);
    assert(document["source"] == "coding_agent" && document["coding_agent_id"] == route.id);
    assert(document["mode"] == "global");
    assert(document["model"].isUnbound() && document["provider"].isUnbound() && document["apiMode"].isUnbound());
    assertNoSecretsOrPrivateFields(document);
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, "vendor/native-model"), "hi", "q") == Result::Ok);
    assert(document["model"] == "vendor/native-model");
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, "unknown-model"), "hi", "q") == Result::NativeModelUnavailable);
    assert(document.size() == 0);
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, "hidden-model"), "hi", "q") == Result::NativeModelUnavailable);
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, nullptr, "empty"), "hi", "q") == Result::Ok);
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, nullptr, "unsupported"), "hi", "q") == Result::Ok);
    assert(document["model"].isUnbound() && document["provider"].isUnbound() && document["apiMode"].isUnbound());
    for (const char* state : {"empty", "unsupported"}) {
      assert(buildNativeGlobalRun(document, ctx, readiness(ctx, "vendor/native-model", state), "hi", "q") == Result::NativeModelUnavailable);
      assert(document.size() == 0);
    }
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, nullptr, "not_installed"), "hi", "q") == Result::NotInstalled);
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, nullptr, "auth_required"), "hi", "q") == Result::AuthenticationRequired);
    for (const char* state : {"timeout", "error"}) {
      assert(buildNativeGlobalRun(document, ctx, readiness(ctx, nullptr, state), "hi", "q") == Result::RetryableUnavailable);
      assert(document.size() == 0);
    }
    for (const char* state : {"ready ", ""}) {
      assert(buildNativeGlobalRun(document, ctx, readiness(ctx, nullptr, state), "hi", "q") == Result::NativeNotReady);
      assert(document.size() == 0);
    }
    assert(buildNativeGlobalRun(document, ctx, readiness(ctx, nullptr, "ready", false), "hi", "q") == Result::NotInstalled);
    auto old = readiness(ctx);
    ++ctx.connectionEpoch;
    assert(buildNativeGlobalRun(document, ctx, old, "hi", "q") == Result::StaleContext);
  }
  const auto ctx = context("cursor", Mode::Global);
  JsonDocument malformed, tools, catalogs;
  assert(inspectNativeReadiness(ctx, malformed.as<JsonVariantConst>(), malformed.as<JsonVariantConst>()).result == Result::InvalidPayload);
  tools["tools"][0]["id"] = "cursor";
  tools["tools"][0]["installed"] = "true";  // Do not coerce a string into readiness.
  catalogs["agents"][0]["agentId"] = "cursor";
  catalogs["agents"][0]["status"] = "ready";
  catalogs["agents"][0]["models"].to<JsonArray>();
  assert(inspectNativeReadiness(ctx, tools.as<JsonVariantConst>(), catalogs.as<JsonVariantConst>()).result == Result::NotInstalled);
  tools["tools"][0]["installed"] = true;
  catalogs["agents"][0]["agentId"] = "qoder";
  assert(inspectNativeReadiness(ctx, tools.as<JsonVariantConst>(), catalogs.as<JsonVariantConst>()).result == Result::NativeNotReady);
  assert(!deserializeJson(catalogs, R"({"agents":[{"agentId":"cursor\u0000evil","status":"ready","models":[]}]})"));
  assert(inspectNativeReadiness(ctx, tools.as<JsonVariantConst>(), catalogs.as<JsonVariantConst>()).result == Result::NativeNotReady);
}

void eventsAndLongContent() {
  const auto ctx = context("codex");
  struct Fixture { const char* name; const char* json; EventType type; };
  const Fixture cases[] = {
    {"run.started", R"({"event":"run.started","run_id":"run-1","queue_id":"q-1"})", EventType::Started},
    {"message.delta", R"({"event":"message.delta","run_id":"run-1","delta":"你好"})", EventType::Delta},
    {"run.completed", R"({"event":"run.completed","run_id":"run-1","queue_id":"q-1","output":"完整回答"})", EventType::Completed},
    {"run.failed", R"({"event":"run.failed","run_id":"run-1","queue_id":"q-1","error":"Provider unavailable"})", EventType::Failed},
    {"resumed", R"({"isWorking":true,"messages":[{"id":1,"role":"assistant","content":"历史"}],"events":[{"event":"message.delta","data":{"delta":"未完成"}}]})", EventType::Resumed},
  };
  for (const auto& fixture : cases) {
    JsonDocument document;
    assert(!deserializeJson(document, fixture.json));
    document["session_id"] = ctx.sessionId;
    EventView out;
    assert(parseEvent(fixture.name, document.as<JsonVariantConst>(), ctx, ctx, out, "run-1", "q-1") == Result::Ok);
    assert(out.type == fixture.type && equal(out.sessionId, ctx.sessionId));
    if (out.type == EventType::Delta) assert(equal(out.text, "你好"));
    if (out.type == EventType::Completed) assert(equal(out.text, "完整回答"));
    if (out.type == EventType::Failed) assert(out.error == "Provider unavailable");
    if (out.type == EventType::Resumed) {
      assert(out.isWorking && out.messages.size() == 1 && out.events.size() == 1);
      assert(out.messages[0]["content"] == "历史");
    }
    std::string before, after;
    serializeJson(document, before);
    auto old = ctx; ++old.connectionEpoch;
    assert(parseEvent(fixture.name, document.as<JsonVariantConst>(), ctx, old, out) == Result::StaleContext);
    assert(out.sessionId == nullptr && out.text == nullptr && out.messages.isNull());
    serializeJson(document, after);
    assert(before == after);
  }
  const std::string longText(26000, 'x');
  JsonDocument document;
  document["session_id"] = ctx.sessionId;
  document["run_id"] = "run-1";
  document["delta"] = longText;
  EventView out;
  assert(parseEvent("message.delta", document.as<JsonVariantConst>(), ctx, ctx, out, "run-1") == Result::Ok);
  assert(std::strlen(out.text) == longText.size());
  assert(out.textBytes == longText.size());
  assert(out.text == document["delta"].as<const char*>());  // Borrow; do not copy/truncate.
  document.clear();
  document["session_id"] = ctx.sessionId;
  document["output"] = nullptr;
  assert(parseEvent("run.completed", document.as<JsonVariantConst>(), ctx, ctx, out) == Result::Ok && out.text == nullptr);
  assert(out.textBytes == 0);
  assert(!deserializeJson(document, R"({"delta":"a\u0000b","run_id":"run-1"})"));
  document["session_id"] = ctx.sessionId;
  assert(parseEvent("message.delta", document.as<JsonVariantConst>(), ctx, ctx, out, "run-1") == Result::Ok);
  assert(out.textBytes == 3 && out.text[0] == 'a' && out.text[1] == 0 && out.text[2] == 'b');
}

void rejectWrongEvents() {
  const auto ctx = context();
  JsonDocument document;
  EventView out;
  auto parse = [&](const char* name = "message.delta", const char* run = "run-1", const char* queue = "q-1") {
    return parseEvent(name, document.as<JsonVariantConst>(), ctx, ctx, out, run, queue);
  };
  assert(parse() == Result::InvalidPayload);
  document["delta"] = "hello";
  assert(parse() == Result::WrongSession);
  document["session_id"] = "other-session";
  assert(parse() == Result::WrongSession);
  document["session_id"] = ctx.sessionId;
  document["run_id"] = "other-run";
  assert(parse() == Result::WrongRun);
  document["run_id"] = "run-1";
  document["queue_id"] = "other-queue";
  assert(parse() == Result::WrongRequest);
  document["queue_id"] = "q-1";
  assert(parse() == Result::Ok);
  assert(out.requestCorrelated);
  document["event"] = "run.completed";
  assert(parse() == Result::InvalidPayload);
  document.remove("event");
  document["delta"] = 7;
  assert(parse() == Result::InvalidPayload);
  document["delta"] = "hello";
  document["run_id"] = 7;
  assert(parse() == Result::InvalidPayload);
  document.remove("run_id");
  assert(parse() == Result::WrongRun);  // Known run cannot accept an untagged delta.
  document["error"] = "failed before run.started";
  assert(parse("run.failed") == Result::Ok);  // Official pre-start error uses queue_id.
  assert(out.requestCorrelated);
  document.remove("queue_id");
  assert(parse("run.failed") == Result::NeedsReconcile);
  assert(out.error == "failed before run.started" && !out.requestCorrelated);
  assert(parse("mcu.chat.message", nullptr, nullptr) == Result::UnknownEvent);
  document["isWorking"] = "false";
  assert(parse("resumed") == Result::InvalidPayload);
  document["isWorking"] = false;
  document["messages"] = "invalid";
  assert(parse("resumed") == Result::InvalidPayload);
  document["messages"].to<JsonArray>();
  assert(parse("resumed") == Result::Ok && !out.isWorking);
  document["output"] = 42;
  assert(parse("run.completed", nullptr, nullptr) == Result::InvalidPayload);
  document["output"] = "valid";
  document["profile"] = "different-payload-profile"; // Actual scope comes from its authenticated socket snapshot.
  assert(parse("run.completed", nullptr, nullptr) == Result::Ok);
  auto wrongProfile = ctx; wrongProfile.profile = "another-profile";
  assert(parseEvent("run.completed", document.as<JsonVariantConst>(), ctx, wrongProfile, out) == Result::StaleContext);
  assert(!deserializeJson(document, R"({"session_id":"95f54aaf-c3b0-4ba6-9547-9b1111550000\u0000other","delta":"x"})"));
  assert(parse("message.delta", nullptr, nullptr) == Result::WrongSession);
  document["session_id"] = ctx.sessionId;
  assert(!deserializeJson(document, R"({"run_id":"run-1\u0000other","delta":"x"})"));
  document["session_id"] = ctx.sessionId;
  assert(parse("message.delta", nullptr, nullptr) == Result::InvalidPayload);
}

void stockCodingEventsWithoutQueueId() {
  const auto ctx = context("claude-code");
  JsonDocument document;
  EventView out;
  // response-stream.ts: stock Coding Agent run.started does not contain qid.
  assert(!deserializeJson(document, R"({"event":"run.started","run_id":"stock-run","status":"in_progress"})"));
  document["session_id"] = ctx.sessionId;
  assert(parseEvent("run.started", document.as<JsonVariantConst>(), ctx, ctx, out, nullptr, "pending-request") == Result::Ok);
  assert(!out.requestCorrelated && equal(out.runId, "stock-run"));
  // After the owner binds run.started, the run ID correlates subsequent deltas.
  document["event"] = "message.delta";
  document["delta"] = "stock coding response";
  assert(parseEvent("message.delta", document.as<JsonVariantConst>(), ctx, ctx, out, "stock-run", "pending-request") == Result::Ok);
  assert(out.requestCorrelated);
  document["run_id"] = "old-run";
  assert(parseEvent("message.delta", document.as<JsonVariantConst>(), ctx, ctx, out, "stock-run", "pending-request") == Result::WrongRun);
  // run-manager cleanupRun and resume access failures are session failures,
  // not necessarily a particular turn's terminal event. Surface for recovery.
  document.clear();
  document["session_id"] = ctx.sessionId;
  document["event"] = "run.failed";
  document["error"] = "Coding agent session closed";
  assert(parseEvent("run.failed", document.as<JsonVariantConst>(), ctx, ctx, out, "stock-run", "pending-request") == Result::NeedsReconcile);
  assert(out.type == EventType::Failed && out.error == "Coding agent session closed" && !out.requestCorrelated);
  auto old = ctx; ++old.connectionEpoch;
  assert(parseEvent("run.failed", document.as<JsonVariantConst>(), ctx, old, out, "stock-run") == Result::StaleContext);
}

void allocationFailureIsExplicit() {
  struct DenyAllocator : ArduinoJson::Allocator {
    void* allocate(size_t) override { return nullptr; }
    void deallocate(void*) override {}
    void* reallocate(void*, size_t) override { return nullptr; }
  } allocator;
  JsonDocument document(&allocator);
  assert(buildScopedRun(document, context(), model(), "hello", "queue") == Result::OutOfMemory);
  assert(document.size() == 0);
  assert(buildSessionCommand(document, context()) == Result::OutOfMemory);
  assert(document.size() == 0);
}

void codingStoredInputTrimming() {
  const std::string original=" \t\xc2\xa0" "你好  x\n y" "\xe3\x80\x80\xef\xbb\xbf\r\n";
  const auto view=storedCodingInput(original.c_str());
  assert(view.valid && std::string(view.data,view.bytes)=="你好  x\n y");
  const auto empty=storedCodingInput("\xe3\x80\x80\t\r\n");
  assert(empty.valid && empty.bytes==0);
  assert(!storedCodingInput(nullptr).valid);
  assert(!storedCodingInput("\xc0\x80").valid);
  const auto plain=storedCodingInput("hi");assert(plain.valid && plain.bytes==2);
}

}  // namespace

int main() {
  genericRoutingAndModes();
  modelIdentityIsExplicit();
  contextIsolation();
  nativeGlobalRouting();
  eventsAndLongContent();
  rejectWrongEvents();
  stockCodingEventsWithoutQueueId();
  allocationFailureIsExplicit();
  codingStoredInputTrimming();
  std::puts("Official Studio: 16 canonical routes, modes/model identity, native readiness, session isolation and event views passed");
}
