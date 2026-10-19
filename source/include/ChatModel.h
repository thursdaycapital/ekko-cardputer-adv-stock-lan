#pragma once
#include "AgentSelection.h"
#include "ChatInput.h"
#include "ChineseInput.h"
#include <array>
#include <limits>

constexpr size_t kNativeChatContextCount = 16;
constexpr size_t kNativeChatMessageCount = 12;
constexpr size_t kNativeChatMessageBytes = 1024;
constexpr size_t kNativeChatChunkBytes = 512;
constexpr size_t kNativeChatIdBytes = 128;
constexpr size_t kNativeChatProfileBytes = 64;
constexpr size_t kNativeChatBindingBytes = 64;
constexpr size_t kNativeChatSessionBytes = 224;
constexpr size_t kNativeChatRequestBytes = 64;

enum class NativeChatRole : uint8_t { User, Assistant };
enum class NativeChatResult : uint8_t {
  Ok, Duplicate, Truncated, Gap, Stale, InvalidInput, DraftCapacity,
  MissingMessage, Finalized, SessionMismatch
};

inline const char* nativeChatResultName(NativeChatResult result) {
  switch (result) {
    case NativeChatResult::Ok: return "ok";
    case NativeChatResult::Duplicate: return "duplicate";
    case NativeChatResult::Truncated: return "truncated";
    case NativeChatResult::Gap: return "gap";
    case NativeChatResult::Stale: return "stale";
    case NativeChatResult::InvalidInput: return "invalid_input";
    case NativeChatResult::DraftCapacity: return "draft_capacity";
    case NativeChatResult::MissingMessage: return "missing_message";
    case NativeChatResult::Finalized: return "finalized";
    case NativeChatResult::SessionMismatch: return "session_mismatch";
  }
  return "invalid_input";
}

struct NativeChatRoute {
  const char* agent_id;
  const char* profile;
  uint32_t selection_version;
  const char* session_id;
  NativeChatRoute(const char* agent = "", const char* selectedProfile = "",
                  uint32_t version = 0, const char* session = "")
      : agent_id(agent), profile(selectedProfile), selection_version(version), session_id(session) {}
};

struct NativeChatMessage {
  char text[kNativeChatMessageBytes + 1]{};
  char id[kNativeChatIdBytes + 1]{};
  char request_id[kNativeChatRequestBytes + 1]{};
  uint32_t window_offset = 0;
  uint32_t next_offset = 0;
  uint32_t total_bytes = 0;
  uint32_t order = 0;
  uint32_t last_used = 0;
  uint16_t stored_bytes = 0;
  uint8_t context_index = 0;
  NativeChatRole role = NativeChatRole::User;
  bool used = false;
  bool from_history = false;
  bool window_complete = false;
  bool message_final = false;
  bool truncated_before = false;
  bool truncated_after = false;
  bool gap = false;
};

// A fully received DB/history window is a readable snapshot even when its
// persisted finish_reason is absent. This never infers that the run finished.
// Live data remains closed until the final frame and full window arrive.
inline bool nativeChatWindowReadable(const NativeChatMessage& message) {
  return message.used && message.window_complete &&
      (message.message_final || message.from_history);
}

struct NativeChatContext {
  NativeChatInput draft;
  NativeChineseInput input_method;
  char profile[kNativeChatProfileBytes + 1]{};
  // Caller-created machine/account/mode fingerprint, never a credential.
  // Empty is the legacy three-argument activate namespace only.
  char binding[kNativeChatBindingBytes + 1]{};
  char session_id[kNativeChatSessionBytes + 1]{};
  uint32_t last_used = 0;
  uint8_t agent_index = 0;
  bool used = false;
  bool history_has_more = false;
  bool history_has_newer = false;
};

// Native session history is authoritative. This is a small reloadable cache:
// 16 drafts, 12 message windows, no allocation and no automatic model/audio run.
// Do not retain message/context pointers across a mutating method.
class NativeChatModel {
 public:
  NativeChatModel() = default;
  NativeChatModel(const NativeChatModel&) = delete;
  NativeChatModel& operator=(const NativeChatModel&) = delete;

  NativeChatResult activate(const char* agent, const char* profile, uint32_t version,
                            const char* binding = "") {
    if (!binding) { deactivate(); return NativeChatResult::InvalidInput; }
    size_t bindingBytes = 0;
    while (bindingBytes <= kNativeChatBindingBytes && binding[bindingBytes]) ++bindingBytes;
    return activate(agent, profile, version, binding, bindingBytes);
  }

  // Explicit-byte input is for decoded JSON / String values: a const char*
  // alone cannot detect bytes after an embedded NUL. No input is truncated.
  NativeChatResult activate(const char* agent, const char* profile, uint32_t version,
                            const char* binding, size_t bindingBytes) {
    const int agentIndex = nativeAgentIndex(agent);
    if (agentIndex < 0 || !version || !native_chat::boundedIdentifier(profile, kNativeChatProfileBytes) ||
        !validBinding(binding, bindingBytes)) {
      deactivate(); return NativeChatResult::InvalidInput;
    }
    // A caller may pass the current context's binding while reusing an empty
    // LRU slot. Snapshot before clearing that slot, without retaining a pointer.
    char bindingSnapshot[kNativeChatBindingBytes + 1]{};
    if (bindingBytes) std::memcpy(bindingSnapshot, binding, bindingBytes);
    int index = -1;
    for (size_t i = 0; i < contexts_.size(); ++i)
      if (contexts_[i].used && contexts_[i].agent_index == agentIndex &&
          std::strcmp(contexts_[i].profile, profile) == 0 &&
          std::strlen(contexts_[i].binding) == bindingBytes &&
          std::memcmp(contexts_[i].binding, bindingSnapshot, bindingBytes) == 0) { index = static_cast<int>(i); break; }
    if (index < 0) {
      for (size_t i = 0; i < contexts_.size(); ++i)
        if (!contexts_[i].used) { index = static_cast<int>(i); break; }
      if (index < 0) {
        uint32_t oldestAge = 0;
        for (size_t i = 0; i < contexts_.size(); ++i) {
          if (!contexts_[i].draft.empty() || contexts_[i].input_method.composing()) continue;
          const uint32_t age = clock_ - contexts_[i].last_used;
          if (index < 0 || age > oldestAge) { index = static_cast<int>(i); oldestAge = age; }
        }
      }
      if (index < 0) { deactivate(); return NativeChatResult::DraftCapacity; }
      removeMessages(static_cast<uint8_t>(index));
      NativeChatContext& context = contexts_[index];
      context.draft.clear();
      context.input_method.reset();
      copy(context.profile, profile);
      std::memset(context.binding, 0, sizeof(context.binding));
      if (bindingBytes) std::memcpy(context.binding, bindingSnapshot, bindingBytes);
      context.session_id[0] = 0;
      context.agent_index = static_cast<uint8_t>(agentIndex);
      context.used = true; context.history_has_more = context.history_has_newer = false;
    }
    if (active_ != index || version_ != version) {
      resetHistoryRequest(); session_confirmed_ = false; ++revision_;
    }
    active_ = index; version_ = version;
    touch();
    return NativeChatResult::Ok;
  }

  void disconnect() {
    version_ = 0; session_confirmed_ = false; resetHistoryRequest(); ++revision_;
  }
  // Hide before validating a new account/target/configuration. Cached drafts
  // and history remain recoverable only by reactivating the same binding.
  // Unlike disconnect(), this also prevents offline reads of the old context.
  void hideContext() { deactivate(); }
  bool selectionReady() const { return active_ >= 0 && version_ != 0; }
  bool ready() const { return selectionReady() && session_confirmed_; }
  uint32_t version() const { return version_; }
  uint32_t revision() const { return revision_; }
  NativeChatInput* draft() { return active_ < 0 ? nullptr : &contexts_[active_].draft; }
  const NativeChatInput* draft() const { return active_ < 0 ? nullptr : &contexts_[active_].draft; }
  NativeChineseInput* inputMethod() { return active_ < 0 ? nullptr : &contexts_[active_].input_method; }
  const NativeChineseInput* inputMethod() const { return active_ < 0 ? nullptr : &contexts_[active_].input_method; }
  const NativeChatContext* context() const { return active_ < 0 ? nullptr : &contexts_[active_]; }
  const char* agentId() const { return active_ < 0 ? "" : kNativeAgents[contexts_[active_].agent_index].id; }

  // Root creates a unique requestId before sending mcu.chat.history. Late
  // begins/chunks/ends from an older request cannot replace the chosen window.
  NativeChatResult expectHistory(const char* requestId, bool replaceMessages = true) {
    if (!selectionReady()) return NativeChatResult::Stale;
    if (!validRequest(requestId)) return NativeChatResult::InvalidInput;
    if (std::strcmp(history_request_, requestId) != 0) {
      copy(history_request_, requestId); history_receiving_ = false;
      history_replace_ = replaceMessages;
    }
    return NativeChatResult::Ok;
  }

  NativeChatResult beginHistory(const NativeChatRoute& route, const char* requestId) {
    if (!historyExpected(requestId) || !selectionMatches(route)) return NativeChatResult::Stale;
    if (!native_chat::boundedIdentifier(route.session_id, kNativeChatSessionBytes))
      return NativeChatResult::InvalidInput;
    NativeChatContext& current = contexts_[active_];
    if (std::strcmp(current.session_id, route.session_id) != 0 ||
        (!history_receiving_ && history_replace_)) {
      // A clear/new native session changes history, never the unsent draft.
      removeMessages(static_cast<uint8_t>(active_));
      copy(current.session_id, route.session_id);
      current.history_has_more = current.history_has_newer = false; ++revision_;
    }
    if (!session_confirmed_) ++revision_;
    history_receiving_ = session_confirmed_ = true; touch();
    return NativeChatResult::Ok;
  }

  NativeChatResult endHistory(const NativeChatRoute& route, const char* requestId,
                              bool hasMore, bool hasNewer = false) {
    if (!history_receiving_ || !historyExpected(requestId)) return NativeChatResult::Stale;
    const NativeChatResult contextResult = checkRoute(route);
    if (contextResult != NativeChatResult::Ok) return contextResult;
    if (history_replace_) {
      contexts_[active_].history_has_more = hasMore;
      contexts_[active_].history_has_newer = hasNewer;
    }
    resetHistoryRequest(); ++revision_; touch();
    return NativeChatResult::Ok;
  }

  // Map mcu.chat.message.begin. All IDs are copied whole or rejected. A
  // repeated begin is idempotent and never erases already received chunks.
  NativeChatResult beginMessage(const NativeChatRoute& route, const char* requestId,
                                const char* id, NativeChatRole role,
                                uint32_t startOffset, uint32_t totalBytes,
                                bool messageFinal, bool fromHistory = false) {
    if (!validRequest(requestId) || !native_chat::boundedIdentifier(id, kNativeChatIdBytes) ||
        (role != NativeChatRole::User && role != NativeChatRole::Assistant) ||
        (startOffset > totalBytes && (totalBytes || messageFinal)) ||
        (!fromHistory && startOffset != 0)) return NativeChatResult::InvalidInput;
    if (fromHistory && (!history_receiving_ || !historyExpected(requestId))) return NativeChatResult::Stale;
    NativeChatResult contextResult = checkRoute(route, true);
    if (contextResult != NativeChatResult::Ok) return contextResult;
    NativeChatMessage* message = findMutable(id);
    if (message && message->role != role) return NativeChatResult::InvalidInput;
    if (message && std::strcmp(message->request_id, requestId) == 0 &&
        message->window_offset == startOffset && message->from_history == fromHistory)
      return NativeChatResult::Duplicate;
    if (!message) message = allocateMessage();
    const bool newMessage = !message->used;
    const uint32_t order = newMessage ? ++clock_ : message->order;
    copy(message->id, id); copy(message->request_id, requestId);
    message->text[0] = 0; message->stored_bytes = 0;
    message->context_index = static_cast<uint8_t>(active_);
    message->role = role; message->used = true; message->order = order;
    message->window_offset = message->next_offset = startOffset;
    message->total_bytes = totalBytes; message->from_history = fromHistory;
    message->window_complete = false; message->message_final = messageFinal;
    message->truncated_before = startOffset > 0;
    message->truncated_after = totalBytes > startOffset;
    message->gap = false; message->last_used = ++clock_; ++revision_; touch();
    return NativeChatResult::Ok;
  }

  // Map mcu.chat.message. offset is absolute UTF-8 bytes. History final closes
  // a window; live final closes the logical message. Missing/duplicate chunks
  // never concatenate at the wrong position. No implicit begin is permitted.
  NativeChatResult appendChunk(const NativeChatRoute& route, const char* requestId,
                               const char* id, NativeChatRole role, uint32_t offset,
                               const char* text, size_t length, bool final,
                               uint32_t totalBytes = 0) {
    const NativeChatResult contextResult = checkRoute(route);
    if (contextResult != NativeChatResult::Ok) return contextResult;
    if (!validRequest(requestId) || !native_chat::boundedIdentifier(id, kNativeChatIdBytes) ||
        length > kNativeChatChunkBytes || !native_chat::validUtf8(text, length) ||
        length > std::numeric_limits<uint32_t>::max() - offset) return NativeChatResult::InvalidInput;
    NativeChatMessage* message = findMutable(id);
    if (!message) return NativeChatResult::MissingMessage;
    if (message->role != role) return NativeChatResult::InvalidInput;
    if (std::strcmp(message->request_id, requestId) != 0 ||
        (message->from_history && (!history_receiving_ || !historyExpected(requestId))))
      return NativeChatResult::Stale;
    const uint32_t end = offset + static_cast<uint32_t>(length);
    if (offset > message->next_offset) return markGap(*message);
    const bool duplicate = offset < message->next_offset;
    if (duplicate) {
      if (offset < message->window_offset || end > message->next_offset) return NativeChatResult::InvalidInput;
      const uint32_t storedEnd = message->window_offset + message->stored_bytes;
      if (offset < storedEnd) {
        const size_t overlap = std::min<size_t>(length, storedEnd - offset);
        if (overlap && std::memcmp(message->text + offset - message->window_offset, text, overlap))
          return NativeChatResult::InvalidInput;
      }
      if (!final || end != message->next_offset || message->window_complete) return NativeChatResult::Duplicate;
    } else if (message->window_complete) {
      return !length && final ? NativeChatResult::Duplicate : NativeChatResult::Finalized;
    }
    if (message->from_history && end > message->total_bytes) return NativeChatResult::InvalidInput;
    if (final && !message->from_history && totalBytes && totalBytes != end) return markGap(*message);
    if (final && !message->from_history && message->message_final && message->total_bytes != end)
      return markGap(*message);
    if (message->from_history && totalBytes && totalBytes != message->total_bytes)
      return NativeChatResult::InvalidInput;

    bool truncated = false;
    if (!duplicate) {
      const uint32_t storedEnd = message->window_offset + message->stored_bytes;
      const size_t copyBytes = offset == storedEnd ?
          native_chat::prefixBytes(text, length, kNativeChatMessageBytes - message->stored_bytes) : 0;
      if (copyBytes) std::memmove(message->text + message->stored_bytes, text, copyBytes);
      message->stored_bytes += static_cast<uint16_t>(copyBytes);
      message->text[message->stored_bytes] = 0;
      message->next_offset = end;
      if (!message->from_history && end > message->total_bytes) message->total_bytes = end;
      truncated = copyBytes != length;
    }
    if (final) {
      message->window_complete = true;
      if (!message->from_history) message->message_final = true;
    }
    message->truncated_after = message->window_offset + message->stored_bytes < message->total_bytes;
    message->gap = false; message->last_used = ++clock_; ++revision_; touch();
    return truncated ? NativeChatResult::Truncated : NativeChatResult::Ok;
  }

  size_t messageCount() const {
    if (active_ < 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < messages_.size(); ++i)
      if (messages_[i].used && messages_[i].context_index == active_) ++count;
    return count;
  }
  const NativeChatMessage* findMessage(const char* id) const {
    if (active_ < 0 || !id) return nullptr;
    for (size_t i = 0; i < messages_.size(); ++i)
      if (messages_[i].used && messages_[i].context_index == active_ &&
          std::strcmp(messages_[i].id, id) == 0) return &messages_[i];
    return nullptr;
  }
  const NativeChatMessage* messageAt(size_t index) const {
    std::array<size_t, kNativeChatMessageCount> ordered{};
    size_t count = 0;
    for (size_t i = 0; i < messages_.size(); ++i) {
      if (!messages_[i].used || messages_[i].context_index != active_) continue;
      size_t position = count;
      while (position && uint32_t(clock_ - messages_[ordered[position - 1]].order) <
          uint32_t(clock_ - messages_[i].order)) {
        ordered[position] = ordered[position - 1]; --position;
      }
      ordered[position] = i; ++count;
    }
    return index < count ? &messages_[ordered[index]] : nullptr;
  }
  void clearMessages() {
    if (active_ >= 0) removeMessages(static_cast<uint8_t>(active_));
  }

 private:
  static bool validBinding(const char* binding, size_t length) {
    if (!binding || length > kNativeChatBindingBytes || !native_chat::validUtf8(binding, length)) return false;
    for (size_t i = 0; i < length; ++i)
      if (static_cast<uint8_t>(binding[i]) < 0x20 || binding[i] == 0x7f) return false;
    return true;
  }
  template <size_t Capacity> static void copy(char (&destination)[Capacity], const char* source) {
    const size_t length = std::strlen(source); // Caller validated the matching bound.
    std::memmove(destination, source, length + 1);
  }
  static bool validRequest(const char* request) {
    if (!native_chat::boundedIdentifier(request, kNativeChatRequestBytes)) return false;
    for (const char* c = request; *c; ++c) if (static_cast<uint8_t>(*c) > 0x7E) return false;
    return true;
  }
  bool selectionMatches(const NativeChatRoute& route) const {
    return selectionReady() && route.agent_id && route.profile && route.selection_version == version_ &&
        std::strcmp(route.agent_id, agentId()) == 0 &&
        std::strcmp(route.profile, contexts_[active_].profile) == 0;
  }
  NativeChatResult checkRoute(const NativeChatRoute& route, bool allowFirstSession = false) {
    if (!selectionMatches(route)) return NativeChatResult::Stale;
    if (!native_chat::boundedIdentifier(route.session_id, kNativeChatSessionBytes))
      return NativeChatResult::InvalidInput;
    NativeChatContext& current = contexts_[active_];
    if (!*current.session_id && allowFirstSession) copy(current.session_id, route.session_id);
    if (std::strcmp(current.session_id, route.session_id) != 0) return NativeChatResult::SessionMismatch;
    if (allowFirstSession) session_confirmed_ = true;
    return session_confirmed_ ? NativeChatResult::Ok : NativeChatResult::Stale;
  }
  bool historyExpected(const char* requestId) const {
    return requestId && *history_request_ && std::strcmp(history_request_, requestId) == 0;
  }
  void resetHistoryRequest() { history_request_[0] = 0; history_receiving_ = history_replace_ = false; }
  void deactivate() { active_ = -1; version_ = 0; session_confirmed_ = false; resetHistoryRequest(); ++revision_; }
  void touch() { if (active_ >= 0) contexts_[active_].last_used = ++clock_; }
  void removeMessages(uint8_t contextIndex) {
    for (size_t i = 0; i < messages_.size(); ++i)
      if (messages_[i].used && messages_[i].context_index == contextIndex) messages_[i].used = false;
    ++revision_;
  }
  NativeChatMessage* findMutable(const char* id) {
    return const_cast<NativeChatMessage*>(findMessage(id));
  }
  NativeChatMessage* allocateMessage() {
    for (size_t i = 0; i < messages_.size(); ++i) if (!messages_[i].used) return &messages_[i];
    size_t oldest = 0;
    for (size_t i = 1; i < messages_.size(); ++i)
      if (uint32_t(clock_ - messages_[i].last_used) > uint32_t(clock_ - messages_[oldest].last_used)) oldest = i;
    messages_[oldest].used = false;
    return &messages_[oldest];
  }
  NativeChatResult markGap(NativeChatMessage& message) {
    if (!message.gap) { message.gap = true; ++revision_; }
    return NativeChatResult::Gap;
  }

  std::array<NativeChatContext, kNativeChatContextCount> contexts_{};
  std::array<NativeChatMessage, kNativeChatMessageCount> messages_{};
  char history_request_[kNativeChatRequestBytes + 1]{};
  int active_ = -1;
  uint32_t version_ = 0, clock_ = 0, revision_ = 0;
  bool history_receiving_ = false;
  bool history_replace_ = false;
  bool session_confirmed_ = false;
};

static_assert(sizeof(NativeChatModel) <= 36 * 1024, "Chat and input methods exceed no-PSRAM budget");
