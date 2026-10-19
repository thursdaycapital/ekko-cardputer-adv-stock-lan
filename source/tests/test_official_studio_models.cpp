#include "OfficialStudioModels.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace models = official_studio_models;

namespace {

JsonDocument fixture() {
  JsonDocument response;
  assert(!deserializeJson(response, R"({
    "default":"other-profile-model", "default_provider":"other-profile-provider",
    "groups":[{"provider":"wrong-merged-provider","api_mode":"chat_completions","models":["wrong-model"]}],
    "allProviders":[{"provider":"catalog-only","api_mode":"chat_completions","models":["catalog-model"]}],
    "profiles":[
      {"profile":"default","default":"vendor/Default-Exact","default_provider":"custom:Provider-Exact",
       "groups":[
         {"provider":"first-provider","api_mode":"chat_completions","models":["first-model"]},
         {"provider":"custom:Provider-Exact","api_mode":"anthropic_messages",
          "models":["vendor/First-Exact","vendor/Default-Exact"],
          "available_models":["hidden-by-server"],
          "model_meta":{"vendor/Default-Exact":{"alias":"display-only-alias"}}}
       ]},
      {"profile":"工作 空间","default":"中文模型/精确","default_provider":"custom:中文服务商",
       "groups":[{"provider":"custom:中文服务商","api_mode":"codex_responses","models":["中文模型/精确"]}]}
    ]
  })"));
  return response;
}

void assertEmpty(const models::Selection& value, models::Result expected) {
  assert(value.result == expected && value.origin == models::Origin::None);
  for (char c : value.provider) assert(c == 0);
  for (char c : value.model) assert(c == 0);
  for (char c : value.apiMode) assert(c == 0);
  const auto identity = value.identity();
  assert(identity.provider == nullptr && identity.model == nullptr && identity.apiMode == nullptr);
}

void profileScopeAndOwnership() {
  auto response = fixture();
  auto selected = models::selectProfileDefault(response, "default", "codex");
  assert(selected.result == models::Result::Ok && selected.origin == models::Origin::ProfileDefault);
  assert(std::string(selected.provider.data()) == "custom:Provider-Exact");
  assert(std::string(selected.model.data()) == "vendor/Default-Exact");
  assert(std::string(selected.apiMode.data()) == "anthropic_messages");
  auto chinese = models::selectProfileDefault(response, "工作 空间", "ekko-agent");
  assert(chinese.result == models::Result::Ok);
  assert(std::string(chinese.provider.data()) == "custom:中文服务商");
  assert(std::string(chinese.model.data()) == "中文模型/精确");
  assert(std::string(chinese.apiMode.data()) == "codex_responses");
  assertEmpty(models::selectProfileDefault(response, "not-authorized-profile", "hermes"), models::Result::ProfileNotAvailable);
  assertEmpty(models::selectProfileDefault(response, nullptr, "hermes"), models::Result::InvalidResponse);
  assertEmpty(models::selectProfileDefault(response, "　", "hermes"), models::Result::InvalidResponse);
  assertEmpty(models::selectProfileDefault(response, "default", "claude"), models::Result::UnsupportedAgent);
  assertEmpty(models::selectProfileDefault(response, "default", "cursor"), models::Result::UnsupportedMode);
  assertEmpty(models::selectProfileDefault(response, "default", "codex", official_studio::Mode::Global), models::Result::UnsupportedMode);
  auto copy = selected;
  response.clear();
  selected = {};
  const auto identity = copy.identity();
  assert(std::string(identity.provider) == "custom:Provider-Exact");
  assert(std::string(identity.model) == "vendor/Default-Exact");
  assert(std::string(identity.apiMode) == "anthropic_messages");
  assert(identity.provider == copy.provider.data());
}

void officialFallbackAndVisibility() {
  auto response = fixture();
  auto profile = response["profiles"][0].as<JsonObject>();
  profile["default"] = "missing-default";
  auto selected = models::selectProfileDefault(response, "default", "codex");
  assert(selected.result == models::Result::Ok && selected.origin == models::Origin::ProviderFallback);
  assert(std::string(selected.provider.data()) == "custom:Provider-Exact");
  assert(std::string(selected.model.data()) == "vendor/First-Exact");
  profile["default_provider"] = "missing-provider";
  selected = models::selectProfileDefault(response, "default", "codex");
  assert(selected.result == models::Result::Ok && selected.origin == models::Origin::FirstAvailable);
  assert(std::string(selected.provider.data()) == "first-provider" && std::string(selected.model.data()) == "first-model");
  profile["default"] = "";
  profile["default_provider"] = "";
  assert(models::selectProfileDefault(response, "default", "codex").result == models::Result::Ok);
  profile["groups"].to<JsonArray>();
  assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::NeedsConfiguration);
  profile["default"] = "configured-but-unavailable";
  assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::NotAvailable);

  response = fixture(); profile = response["profiles"][0].as<JsonObject>();
  auto group = profile["groups"][1];
  group["model_meta"]["vendor/Default-Exact"]["disabled"] = true;
  selected = models::selectProfileDefault(response, "default", "claude-code");
  assert(selected.result == models::Result::Ok && std::string(selected.model.data()) == "vendor/First-Exact");
  group["model_meta"]["vendor/First-Exact"]["hidden"] = true;
  // The configured provider still exists. Match ChatPanel: do not silently
  // select another provider when every model in this provider is disabled.
  assertEmpty(models::selectProfileDefault(response, "default", "claude-code"), models::Result::NotAvailable);
  group["model_meta"]["vendor/First-Exact"]["hidden"] = false;
  profile["default"] = "hidden-by-server";
  selected = models::selectProfileDefault(response, "default", "codex");
  assert(selected.result == models::Result::Ok && std::string(selected.model.data()) != "hidden-by-server");
}

void explicitApiModesAndProviderPolicy() {
  for (const char* mode : {"chat_completions", "codex_responses", "anthropic_messages"}) {
    auto response = fixture();
    response["profiles"][0]["groups"][1]["api_mode"] = mode;
    auto selected = models::selectProfileDefault(response, "default", "codex");
    assert(selected.result == models::Result::Ok && std::string(selected.apiMode.data()) == mode);
  }
  for (const char* mode : {"bedrock_converse", "codex_app_server", "Chat_Completions", " chat_completions"}) {
    auto response = fixture(); response["profiles"][0]["groups"][1]["api_mode"] = mode;
    assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::UnsupportedApiMode);
  }
  for (const char* missing : {static_cast<const char*>(nullptr), "", "　"}) {
    auto response = fixture(); response["profiles"][0]["groups"][1]["api_mode"] = missing;
    assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::NeedsConfiguration);
  }
  auto response = fixture(); response["profiles"][0]["groups"][1].remove("api_mode");
  assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::NeedsConfiguration);
  for (const char* provider : {"openai-codex", "copilot", "xai-oauth", "qwen-oauth", "nous", "claude-oauth", "minimax-oauth", "　CLAUDE-OAUTH　", "moa", " MOA "}) {
    auto policy = fixture();
    policy["profiles"][0]["groups"][1]["provider"] = provider;
    policy["profiles"][0]["default_provider"] = provider;
    auto selected = models::selectProfileDefault(policy, "default", "codex");
    assert(selected.result == models::Result::Ok && std::string(selected.provider.data()) == "first-provider");
    if (std::string(provider) != "moa" && std::string(provider) != " MOA ") {
      selected = models::selectProfileDefault(policy, "default", "ekko-agent");
      assert(selected.result == models::Result::Ok && std::string(selected.provider.data()) == provider);
    }
    selected = models::selectProfileDefault(policy, "default", "hermes");
    assert(selected.result == models::Result::Ok && std::string(selected.provider.data()) == provider);
  }
}

void invalidResponsesAndJsonNul() {
  for (const char* invalid : {R"(null)", R"([])", R"({"groups":[{"provider":"p","models":[{"id":"m","label":"m"}]}],"default":"m"})", R"({"profiles":{}})"}) {
    JsonDocument response; assert(!deserializeJson(response, invalid));
    assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::InvalidResponse);
  }
  for (int field = 0; field < 9; ++field) {
    auto response = fixture(); auto profile = response["profiles"][0]; auto group = profile["groups"][1];
    switch (field) {
      case 0: profile["default"] = 42; break;
      case 1: profile["default_provider"] = false; break;
      case 2: profile["groups"] = "not-array"; break;
      case 3: group["provider"] = nullptr; break;
      case 4: group["models"][0] = JsonObject(); break;
      case 5: group["api_mode"] = 42; break;
      case 6: group["model_meta"] = nullptr; break;
      case 7: group["model_meta"]["vendor/Default-Exact"]["disabled"] = "false"; break;
      case 8: group["model_meta"]["vendor/Default-Exact"]["hidden"] = 0; break;
    }
    assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::InvalidResponse);
  }
  auto response = fixture(); response["profiles"].as<JsonArray>().add(response["profiles"][0]);
  assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::InvalidResponse);
  response = fixture(); response["profiles"][0]["groups"].as<JsonArray>().add(response["profiles"][0]["groups"][1]);
  assertEmpty(models::selectProfileDefault(response, "default", "codex"), models::Result::InvalidResponse);
  for (const char* json : {
    R"({"profiles":[{"profile":"default\u0000evil","default":"m","default_provider":"p","groups":[]}]})",
    R"({"profiles":[{"profile":"default","default":"m\u0000evil","default_provider":"p","groups":[]}]})",
    R"({"profiles":[{"profile":"default","default":"m","default_provider":"p\u0000evil","groups":[]}]})",
    R"({"profiles":[{"profile":"default","default":"m","default_provider":"p","groups":[{"provider":"p\u0000evil","api_mode":"chat_completions","models":["m"]}]}]})",
    R"({"profiles":[{"profile":"default","default":"m","default_provider":"p","groups":[{"provider":"p","api_mode":"chat_completions\u0000evil","models":["m"]}]}]})",
    R"({"profiles":[{"profile":"default","default":"m","default_provider":"p","groups":[{"provider":"p","api_mode":"chat_completions","models":["m\u0000evil"]}]}]})",
    R"({"profiles":[{"profile":"default","default":"m","default_provider":"p","groups":[{"provider":"p","api_mode":"chat_completions","models":["m"],"model_meta":{"m\u0000evil":{"disabled":false}}}]}]})"
  }) {
    JsonDocument nul; assert(!deserializeJson(nul, json));
    assertEmpty(models::selectProfileDefault(nul, "default", "codex"), models::Result::InvalidResponse);
  }
}

void exactBoundsAndNoTruncation() {
  for (const size_t extra : {size_t{0}, size_t{1}}) {
    auto response = fixture(); auto profile = response["profiles"][0].as<JsonObject>();
    const std::string provider(models::kProviderMaxBytes + extra, 'p');
    profile["groups"][1]["provider"] = provider; profile["default_provider"] = provider;
    auto selected = models::selectProfileDefault(response, "default", "codex");
    if (extra) assertEmpty(selected, models::Result::IdentityTooLong);
    else assert(selected.result == models::Result::Ok && std::string(selected.provider.data()) == provider);
    response = fixture(); profile = response["profiles"][0].as<JsonObject>();
    const std::string model(models::kModelMaxBytes + extra, 'm');
    profile["groups"][1]["models"][0] = model; profile["default"] = model;
    selected = models::selectProfileDefault(response, "default", "codex");
    if (extra) assertEmpty(selected, models::Result::IdentityTooLong);
    else assert(selected.result == models::Result::Ok && std::string(selected.model.data()) == model);
  }
}

void filteredInputNeverCopiesCredentials() {
  JsonDocument source = fixture();
  source["api_key"] = "secret-top-level"; source["key"] = "secret-top-key";
  source["profiles"][0]["key"] = "secret-profile-key";
  auto group = source["profiles"][0]["groups"][1];
  group["api_key"] = "secret-group-api-key"; group["key"] = "secret-group-key";
  group["base_url"] = "https://secret-url.invalid/";
  group["model_meta"]["vendor/Default-Exact"]["api_key"] = "secret-meta-key";
  source["allProviders"][0]["api_key"] = "secret-catalog-key";
  JsonDocument filter; assert(models::buildAvailableModelsFilter(filter));
  std::string full; serializeJson(source, full);
  JsonDocument filtered;
  assert(!deserializeJson(filtered, full, ArduinoJson::DeserializationOption::Filter(filter)));
  std::string compact; serializeJson(filtered, compact);
  assert(compact.find("secret-") == std::string::npos);
  assert(filtered["api_key"].isUnbound() && filtered["groups"].isUnbound() && filtered["allProviders"].isUnbound());
  assert(filtered["profiles"][0]["groups"][1]["api_key"].isUnbound());
  auto selected = models::selectProfileDefault(filtered, "default", "codex");
  assert(selected.result == models::Result::Ok);
  group["model_meta"]["vendor/Default-Exact"]["disabled"] = true;
  group["model_meta"]["vendor/First-Exact"]["hidden"] = true;
  full.clear();serializeJson(source, full);
  assert(!deserializeJson(filtered, full, ArduinoJson::DeserializationOption::Filter(filter)));
  assertEmpty(models::selectProfileDefault(filtered, "default", "codex"), models::Result::NotAvailable);
  // A literal model ID can contain "key"; do not rewrite opaque identities.
  source["profiles"][1]["groups"][0]["models"][0] = "model/api_key-and-key-are-literal-id";
  source["profiles"][1]["default"] = "model/api_key-and-key-are-literal-id";
  full.clear();serializeJson(source, full);
  assert(!deserializeJson(filtered, full, ArduinoJson::DeserializationOption::Filter(filter)));
  selected = models::selectProfileDefault(filtered, "工作 空间", "ekko-agent");
  assert(selected.result == models::Result::Ok && std::string(selected.model.data()) == "model/api_key-and-key-are-literal-id");
}

void allocationFailureIsExplicit() {
  struct DenyAllocator : ArduinoJson::Allocator {
    void* allocate(size_t) override { return nullptr; }
    void deallocate(void*) override {}
    void* reallocate(void*, size_t) override { return nullptr; }
  } allocator;
  JsonDocument denied(&allocator);
  assert(!models::buildAvailableModelsFilter(denied));
  denied["profiles"][0]["profile"] = "default";
  assertEmpty(models::selectProfileDefault(denied, "default", "codex"), models::Result::OutOfMemory);
}

}  // namespace

int main() {
  profileScopeAndOwnership();
  officialFallbackAndVisibility();
  explicitApiModesAndProviderPolicy();
  invalidResponsesAndJsonNul();
  exactBoundsAndNoTruncation();
  filteredInputNeverCopiesCredentials();
  allocationFailureIsExplicit();
  std::puts("Official models: exact Profile defaults/fallback, visibility, three modes, owned bounds/NUL rejection and credential-free filter passed");
}
