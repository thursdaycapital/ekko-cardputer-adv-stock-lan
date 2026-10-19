#pragma once

#include "OfficialStudioProtocol.h"
#include <array>
#include <cstddef>
#include <cstring>

// Official Studio 0.7.36 / 1242ac6:
// packages/client/src/api/hermes/system.ts: AvailableModelsResponse.
// packages/client/src/components/hermes/chat/ChatPanel.vue:
//   getDefaultModelForProfile / getSelectableModelGroupsForProfile.
// packages/server/src/modules/hermes/controllers/models.ts: getAvailable.
// This is the device's fresh Profile-default selection, not the Web UI's
// optional localStorage selection. No config.yaml, provider key or URL is read.
namespace official_studio_models {

constexpr size_t kProviderMaxBytes = 256;
constexpr size_t kModelMaxBytes = 512;
constexpr size_t kApiModeMaxBytes = 23;

enum class Result : uint8_t {
  Ok, NeedsConfiguration, InvalidResponse, ProfileNotAvailable, NotAvailable,
  UnsupportedAgent, UnsupportedMode, UnsupportedApiMode, IdentityTooLong,
  OutOfMemory
};

enum class Origin : uint8_t { None, ProfileDefault, ProviderFallback, FirstAvailable };

// Owned identity: independent of the HTTP JsonDocument's lifetime; no pointer
// into it and no credential or base URL fields. Bounds count UTF-8 bytes.
struct Selection {
  Result result = Result::InvalidResponse;
  Origin origin = Origin::None;
  std::array<char, kProviderMaxBytes + 1> provider{};
  std::array<char, kModelMaxBytes + 1> model{};
  std::array<char, kApiModeMaxBytes + 1> apiMode{};

  official_studio::ModelIdentity identity() const {
    if (result != Result::Ok) return {};
    official_studio::ModelIdentity out;
    out.provider = provider.data();
    out.model = model.data();
    out.apiMode = apiMode.data();
    return out;  // Borrowed from this Selection; do not outlive or move it.
  }
};

inline bool plainString(JsonVariantConst value) {
  if (!value.is<const char*>()) return false;
  const JsonString text = value.as<JsonString>();
  return text.size() == std::strlen(text.c_str());  // Reject JSON embedded NUL.
}

inline bool blank(const char* value) {
  return !value || !*official_studio::skipJsWhitespace(value);
}

inline bool optionalFlag(JsonVariantConst object, const char* name) {
  const JsonVariantConst value = object[name];
  return value.isUnbound() || value.is<bool>();
}

inline bool validMetadata(JsonVariantConst metadata) {
  if (metadata.isUnbound()) return true;
  if (!metadata.is<JsonObjectConst>()) return false;
  for (JsonPairConst member : metadata.as<JsonObjectConst>()) {
    const JsonString key = member.key();
    if (key.size() != std::strlen(key.c_str()) || !member.value().is<JsonObjectConst>() ||
        !optionalFlag(member.value(), "disabled") || !optionalFlag(member.value(), "hidden")) return false;
  }
  return true;
}

inline bool validGroup(JsonVariantConst group) {
  if (!group.is<JsonObjectConst>() || !plainString(group["provider"]) ||
      blank(group["provider"].as<const char*>()) || !group["models"].is<JsonArrayConst>() ||
      !validMetadata(group["model_meta"])) return false;
  const auto apiMode = group["api_mode"];
  if (!apiMode.isUnbound() && !apiMode.isNull() && !plainString(apiMode)) return false;
  for (JsonVariantConst model : group["models"].as<JsonArrayConst>()) {
    if (!plainString(model) || blank(model.as<const char*>())) return false;
  }
  return true;
}

inline bool selectable(JsonVariantConst group, const char* model) {
  const auto meta = group["model_meta"][model];
  return !meta["disabled"].as<bool>() && !meta["hidden"].as<bool>();
}

inline bool providerAllowed(const official_studio::AgentRoute& route, const char* provider) {
  // Official ChatPanel reserves the virtual MoA provider for Hermes. The
  // protocol helper applies the official external-Agent OAuth restriction.
  if (official_studio::equal(route.id, "hermes")) return true;
  const char* normalized = official_studio::skipJsWhitespace(provider);
  const char* moa = "moa";
  while (normalized && *normalized && *moa) {
    char c = *normalized;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    if (c != *moa) break;
    ++normalized;
    ++moa;
  }
  if (!*moa && normalized && !*official_studio::skipJsWhitespace(normalized)) return false;
  return official_studio::scopedProviderAllowed(route, provider);
}

inline const char* firstSelectable(JsonVariantConst group) {
  for (JsonVariantConst value : group["models"].as<JsonArrayConst>()) {
    const char* model = value.as<const char*>();
    if (selectable(group, model)) return model;
  }
  return nullptr;
}

template<size_t N>
inline bool copyIdentity(std::array<char, N>& target, const char* source) {
  const size_t bytes = std::strlen(source);
  if (bytes >= N) return false;
  std::memcpy(target.data(), source, bytes + 1);
  return true;
}

inline Selection failure(Result result) {
  Selection out;
  out.result = result;
  return out;
}

// Use the exact profile in profiles[]. Never use the top-level merged groups,
// allProviders, available_models, display aliases or a different Profile.
// groups.models is the server's already visibility-filtered list; disabled
// models are removed here as in ChatPanel (hidden is also rejected if present).
// Native/global calls must instead use OfficialStudioProtocol readiness APIs.
inline Selection selectProfileDefault(const JsonDocument& response, const char* profile,
                                      const char* agentId, official_studio::Mode mode = official_studio::Mode::Scoped) {
  if (response.overflowed()) return failure(Result::OutOfMemory);
  const auto* route = official_studio::agentRoute(agentId);
  if (!route) return failure(Result::UnsupportedAgent);
  if (mode != official_studio::Mode::Scoped || !route->scoped) return failure(Result::UnsupportedMode);
  if (blank(profile) || !response.is<JsonObjectConst>() || !response["profiles"].is<JsonArrayConst>()) {
    return failure(Result::InvalidResponse);
  }
  JsonObjectConst matched;
  for (JsonVariantConst entry : response["profiles"].as<JsonArrayConst>()) {
    if (!entry.is<JsonObjectConst>() || !plainString(entry["profile"])) return failure(Result::InvalidResponse);
    if (!official_studio::jsonStringEquals(entry["profile"], profile)) continue;
    if (!matched.isNull()) return failure(Result::InvalidResponse);  // Ambiguous profile.
    matched = entry.as<JsonObjectConst>();
  }
  if (matched.isNull()) return failure(Result::ProfileNotAvailable);
  if (!plainString(matched["default"]) || !plainString(matched["default_provider"]) ||
      !matched["groups"].is<JsonArrayConst>()) return failure(Result::InvalidResponse);
  const char* defaultProvider = matched["default_provider"].as<const char*>();
  const char* defaultModel = matched["default"].as<const char*>();
  JsonVariantConst preferredGroup, firstGroup;
  for (JsonVariantConst group : matched["groups"].as<JsonArrayConst>()) {
    if (!validGroup(group)) return failure(Result::InvalidResponse);
    const char* provider = group["provider"].as<const char*>();
    if (!providerAllowed(*route, provider)) continue;
    if (official_studio::equal(provider, defaultProvider)) {
      if (!preferredGroup.isNull()) return failure(Result::InvalidResponse);
      preferredGroup = group;
    }
    if (firstGroup.isNull() && firstSelectable(group)) firstGroup = group;
  }
  // Match ChatPanel: an existing configured provider remains selected even
  // when it has zero enabled models; do not silently cross to another provider.
  const JsonVariantConst chosen = preferredGroup.isNull() ? firstGroup : preferredGroup;
  if (chosen.isNull()) {
    return failure(blank(defaultProvider) && blank(defaultModel) ? Result::NeedsConfiguration : Result::NotAvailable);
  }
  const char* selectedModel = nullptr;
  for (JsonVariantConst value : chosen["models"].as<JsonArrayConst>()) {
    if (official_studio::jsonStringEquals(value, defaultModel) && selectable(chosen, defaultModel)) {
      selectedModel = value.as<const char*>();
      break;
    }
  }
  const bool selectedDefault = selectedModel != nullptr;
  if (!selectedModel) selectedModel = firstSelectable(chosen);
  if (!selectedModel) return failure(Result::NotAvailable);
  if (chosen["api_mode"].isUnbound() || chosen["api_mode"].isNull() || blank(chosen["api_mode"].as<const char*>())) {
    return failure(Result::NeedsConfiguration);  // Do not guess from provider/URL.
  }
  const char* selectedApiMode = chosen["api_mode"].as<const char*>();
  if (!official_studio::validApiMode(selectedApiMode)) return failure(Result::UnsupportedApiMode);
  Selection out;
  if (!copyIdentity(out.provider, chosen["provider"].as<const char*>()) ||
      !copyIdentity(out.model, selectedModel) || !copyIdentity(out.apiMode, selectedApiMode)) {
    return failure(Result::IdentityTooLong);  // No truncated partial identity.
  }
  out.result = Result::Ok;
  out.origin = !preferredGroup.isNull() && selectedDefault ? Origin::ProfileDefault :
      !preferredGroup.isNull() ? Origin::ProviderFallback : Origin::FirstAvailable;
  return out;
}

// ArduinoJson 7.4.3 supports the "*" object-key filter. Keep only the official
// selection fields, not entire groups/config documents (which contain api_key).
// Fetch /api/hermes/available-models?profile=<URL-encoded exact profile> using
// normal Studio user auth. A successful HTTP status and deserializeJson result
// must be checked by the caller before invoking selectProfileDefault.
inline bool buildAvailableModelsFilter(JsonDocument& filter) {
  filter.clear();
  auto profile = filter["profiles"][0].to<JsonObject>();
  profile["profile"] = true;
  profile["default"] = true;
  profile["default_provider"] = true;
  auto group = profile["groups"][0].to<JsonObject>();
  group["provider"] = true;
  group["api_mode"] = true;
  group["models"][0] = true;
  group["model_meta"]["*"]["disabled"] = true;
  group["model_meta"]["*"]["hidden"] = true;
  if (!filter.overflowed()) return true;
  filter.clear();
  return false;
}

}  // namespace official_studio_models
