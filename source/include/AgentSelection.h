#pragma once
#include <stddef.h>
#include <stdint.h>

struct NativeAgent { const char* id; const char* name; const char* mark; };
// Canonical config/agents.json IDs. Profiles are a separate namespace.
constexpr NativeAgent kNativeAgents[] = {
  {"ekko-agent", "Ekko", "EK"}, {"hermes", "Hermes", "HE"},
  {"claude-code", "Claude", "CL"}, {"codex", "Codex", "CX"},
  {"pi", "Pi", "PI"}, {"grok", "Grok", "GR"},
  {"opencode", "OpenCode", "OC"}, {"dsh", "DeepSeek Harness", "DS"},
  {"cursor", "Cursor", "CU"}, {"antigravity", "Antigravity", "AG"},
  {"qwen", "Qwen Code", "QW"}, {"kimi", "Kimi Code", "KI"},
  {"codebuddy", "CodeBuddy", "CB"}, {"qoder", "Qoder", "QD"},
  {"copilot", "GitHub Copilot", "GH"}, {"zcode", "ZCode", "ZC"}
};
constexpr size_t kNativeAgentCount = sizeof(kNativeAgents) / sizeof(kNativeAgents[0]);
inline int nativeAgentIndex(const char* id) {
  if (!id) return -1;
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    const char* a = id; const char* b = kNativeAgents[i].id;
    while (*a && *a == *b) { ++a; ++b; }
    if (*a == *b) return static_cast<int>(i);
  }
  return -1;
}

struct NativeAgentSelection {
  size_t active = 0, browse = 0, requested = 0;
  uint32_t version = 0;
  bool pending = false, confirmed = false;
  void disconnect() { pending = confirmed = false; version = 0; }
  void move(int delta) {
    const int count = static_cast<int>(kNativeAgentCount);
    browse = static_cast<size_t>((static_cast<int>(browse) + delta % count + count) % count);
  }
  bool request(size_t index) {
    if (index >= kNativeAgentCount || pending) return false;
    requested = index; pending = true; return true;
  }
  bool acknowledge(size_t index, uint32_t nextVersion) {
    if (!pending || index != requested || nextVersion == 0 ||
        (confirmed && nextVersion < version)) return false;
    active = index; version = nextVersion; confirmed = true; pending = false;
    return true;
  }
  bool ready() const { return confirmed && !pending; }
};

inline bool nativeInteractionVisible(bool interaction,
                                     const NativeAgentSelection& selection,
                                     bool hasSelectionError) {
  return interaction && selection.ready() &&
      selection.browse == selection.active && !hasSelectionError;
}
