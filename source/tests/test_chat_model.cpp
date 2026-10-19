#include "ChatModel.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>

static size_t allocations = 0;
void* operator new(std::size_t length) {
  ++allocations;
  if (void* result = std::malloc(length ? length : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](std::size_t length) { return ::operator new(length); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }

static void streamAndRoutes() {
  NativeChatModel model;
  assert(model.activate("codex", "default", 1) == NativeChatResult::Ok);
  assert(model.selectionReady() && !model.ready());
  const NativeChatRoute route("codex", "default", 1, "native-session");
  assert(model.draft()->insert("未发送 草稿") == NativeChatEditResult::Ok);
  assert(model.beginMessage(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                            0, 0, false) == NativeChatResult::Ok);
  assert(model.ready());
  assert(model.beginMessage(route, "user-turn", "user-echo", NativeChatRole::User,
                            0, 6, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "user-turn", "user-echo", NativeChatRole::User,
                           0, "abc", 3, false) == NativeChatResult::Ok);
  assert(model.findMessage("user-echo")->total_bytes == 6);
  assert(model.appendChunk(route, "user-turn", "user-echo", NativeChatRole::User,
                           3, "", 0, true) == NativeChatResult::Gap);
  assert(model.appendChunk(route, "user-turn", "user-echo", NativeChatRole::User,
                           3, "def", 3, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           0, "你", 3, false) == NativeChatResult::Ok);
  const uint32_t revision = model.revision();
  assert(model.beginMessage(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                            0, 0, false) == NativeChatResult::Duplicate);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           0, "你", 3, false) == NativeChatResult::Duplicate);
  assert(model.revision() == revision);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           0, "错", 3, false) == NativeChatResult::InvalidInput);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           6, "!", 1, false) == NativeChatResult::Gap);
  assert(model.findMessage("assistant-1")->gap);
  assert(std::strcmp(model.findMessage("assistant-1")->text, "你") == 0);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           3, "好", 3, false) == NativeChatResult::Ok);
  assert(!model.findMessage("assistant-1")->gap);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           6, "😀", 4, true, 10) == NativeChatResult::Ok);
  const NativeChatMessage* message = model.messageAt(0);
  assert(message && std::strcmp(message->text, "你好😀") == 0);
  assert(message->message_final && message->window_complete && message->total_bytes == 10);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           6, "😀", 4, true, 10) == NativeChatResult::Duplicate);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           10, "!", 1, false) == NativeChatResult::Finalized);
  assert(model.appendChunk(route, "wrong-turn", "assistant-1", NativeChatRole::Assistant,
                           10, "", 0, true) == NativeChatResult::Stale);
  assert(model.appendChunk(route, "turn-1", "missing", NativeChatRole::Assistant,
                           0, "x", 1, false) == NativeChatResult::MissingMessage);
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::User,
                           0, "x", 1, false) == NativeChatResult::InvalidInput);
  NativeChatRoute stale = route;
  stale.agent_id = "hermes";
  assert(model.appendChunk(stale, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           0, "x", 1, false) == NativeChatResult::Stale);
  stale = route; stale.profile = "other";
  assert(model.beginMessage(stale, "turn-2", "other", NativeChatRole::User, 0, 0, false) == NativeChatResult::Stale);
  stale = route; stale.selection_version = 2;
  assert(model.beginMessage(stale, "turn-2", "other", NativeChatRole::User, 0, 0, false) == NativeChatResult::Stale);
  stale = route; stale.session_id = "other-session";
  assert(model.beginMessage(stale, "turn-2", "other", NativeChatRole::User, 0, 0, false) == NativeChatResult::SessionMismatch);
  model.disconnect();
  assert(!model.ready() && model.draft()->hasContent());
  assert(model.appendChunk(route, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           0, "x", 1, false) == NativeChatResult::Stale);
  assert(model.activate("codex", "default", 1) == NativeChatResult::Ok); // New server may reset version.
  assert(model.selectionReady() && !model.ready());
  assert(model.messageCount() == 2 && std::strcmp(model.draft()->data(), "未发送 草稿") == 0);
  assert(model.activate("codex", "other-profile", 2) == NativeChatResult::Ok && model.messageCount() == 0);
  assert(model.draft()->empty());
  assert(model.activate("codex", "default", 3) == NativeChatResult::Ok && model.messageCount() == 2);
  assert(!model.ready());
  assert(std::strcmp(model.draft()->data(), "未发送 草稿") == 0);
  const NativeChatRoute newRoute("codex", "default", 3, "native-session");
  assert(model.appendChunk(newRoute, "turn-1", "assistant-1", NativeChatRole::Assistant,
                           10, "", 0, true) == NativeChatResult::Stale);
  assert(model.expectHistory("reconnect", false) == NativeChatResult::Ok);
  assert(model.beginHistory(newRoute, "reconnect") == NativeChatResult::Ok && model.ready());
}

static void longMessageWindows() {
  NativeChatModel model;
  assert(model.activate("kimi", "default", 8) == NativeChatResult::Ok);
  const NativeChatRoute route("kimi", "default", 8, "native-kimi");
  assert(model.beginMessage(route, "turn", "long-message", NativeChatRole::Assistant, 0, 0, false) == NativeChatResult::Ok);
  const std::string first(512, 'a'), second(511, 'b');
  assert(model.appendChunk(route, "turn", "long-message", NativeChatRole::Assistant, 0, first.c_str(), first.size(), false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "turn", "long-message", NativeChatRole::Assistant, 512, second.c_str(), second.size(), false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "turn", "long-message", NativeChatRole::Assistant, 1023, "中", 3, false) == NativeChatResult::Truncated);
  assert(model.appendChunk(route, "turn", "long-message", NativeChatRole::Assistant, 1026, "tail", 4, false) == NativeChatResult::Truncated);
  assert(model.appendChunk(route, "turn", "long-message", NativeChatRole::Assistant, 1030, "", 0, true, 1030) == NativeChatResult::Ok);
  const NativeChatMessage* message = model.findMessage("long-message");
  assert(message->stored_bytes == 1023 && message->next_offset == 1030 && message->total_bytes == 1030);
  assert(message->truncated_after && !message->truncated_before && native_chat::validUtf8(message->text, message->stored_bytes));
  assert(model.draft()->insert("保留") == NativeChatEditResult::Ok);
  assert(model.expectHistory("window-1", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "window-1") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "window-1", "long-message", NativeChatRole::Assistant, 1023, 1030, true, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "window-1", "long-message", NativeChatRole::Assistant, 1023, "中tail", 7, true) == NativeChatResult::Ok);
  message = model.findMessage("long-message");
  assert(std::strcmp(message->text, "中tail") == 0 && message->window_offset == 1023);
  assert(message->truncated_before && !message->truncated_after && message->message_final && message->window_complete);
  assert(model.endHistory(route, "window-1", false) == NativeChatResult::Ok);
  assert(std::strcmp(model.draft()->data(), "保留") == 0);
  assert(model.appendChunk(route, "window-1", "long-message", NativeChatRole::Assistant, 1023, "中tail", 7, true) == NativeChatResult::Stale);
}

static void historyTransactions() {
  NativeChatModel model;
  assert(model.activate("hermes", "default", 4) == NativeChatResult::Ok);
  NativeChatRoute route("hermes", "default", 4, "session-a");
  model.draft()->insert("draft");
  assert(model.beginHistory(route, "unsolicited") == NativeChatResult::Stale);
  assert(model.expectHistory("history-old") == NativeChatResult::Ok);
  assert(model.expectHistory("history-new") == NativeChatResult::Ok);
  assert(model.beginHistory(route, "history-old") == NativeChatResult::Stale);
  assert(model.beginHistory(route, "history-new") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "history-new", "user-1", NativeChatRole::User, 0, 3, true, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "history-new", "user-1", NativeChatRole::User, 0, "one", 3, true) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "history-new") == NativeChatResult::Ok); // Duplicate begin does not erase.
  assert(model.messageCount() == 1);
  assert(model.beginMessage(route, "history-new", "assistant-1", NativeChatRole::Assistant, 0, 3, true, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "history-new", "assistant-1", NativeChatRole::Assistant, 0, "two", 3, true) == NativeChatResult::Ok);
  assert(model.messageCount() == 2 && std::strcmp(model.messageAt(0)->id, "user-1") == 0);
  assert(model.endHistory(route, "history-old", false) == NativeChatResult::Stale);
  assert(model.endHistory(route, "history-new", true, true) == NativeChatResult::Ok);
  assert(model.context()->history_has_more && model.context()->history_has_newer);
  assert(model.expectHistory("window-only", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "window-only") == NativeChatResult::Ok);
  assert(model.endHistory(route, "window-only", false, false) == NativeChatResult::Ok);
  assert(model.context()->history_has_more && model.context()->history_has_newer);
  assert(model.expectHistory("refresh") == NativeChatResult::Ok);
  assert(model.beginHistory(route, "refresh") == NativeChatResult::Ok && model.messageCount() == 0);
  assert(!model.context()->history_has_more && !model.context()->history_has_newer);
  assert(std::strcmp(model.draft()->data(), "draft") == 0);
  assert(model.beginMessage(route, "refresh", "new", NativeChatRole::User, 0, 1, true, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "refresh", "new", NativeChatRole::User, 0, "x", 1, true) == NativeChatResult::Ok);
  assert(model.endHistory(route, "refresh", false, true) == NativeChatResult::Ok);
  assert(!model.context()->history_has_more && model.context()->history_has_newer);
  route.session_id = "session-b";
  assert(model.expectHistory("clear-session", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "clear-session") == NativeChatResult::Ok && model.messageCount() == 0);
  assert(!model.context()->history_has_more && !model.context()->history_has_newer);
  assert(std::strcmp(model.draft()->data(), "draft") == 0);
  assert(model.endHistory(route, "clear-session", false) == NativeChatResult::Ok);
  assert(!model.context()->history_has_more && !model.context()->history_has_newer);
}

static void limitsAndEviction() {
  NativeChatModel model;
  assert(model.activate("codex", "default", 1) == NativeChatResult::Ok);
  const NativeChatRoute route("codex", "default", 1, "native");
  const std::string maximumId(128, 'i'), invalidId(129, 'i');
  assert(model.beginMessage(route, "request", maximumId.c_str(), NativeChatRole::User, 0, 0, false) == NativeChatResult::Ok);
  assert(std::strcmp(model.messageAt(0)->id, maximumId.c_str()) == 0);
  assert(model.beginMessage(route, "request", invalidId.c_str(), NativeChatRole::User, 0, 0, false) == NativeChatResult::InvalidInput);
  assert(model.beginMessage(route, "非ASCII", "bad", NativeChatRole::User, 0, 0, false) == NativeChatResult::InvalidInput);
  const std::string oversize(513, 'x');
  assert(model.appendChunk(route, "request", maximumId.c_str(), NativeChatRole::User, 0, oversize.c_str(), oversize.size(), false) == NativeChatResult::InvalidInput);
  assert(model.appendChunk(route, "request", maximumId.c_str(), NativeChatRole::User, 0, "\xED\xA0\x80", 3, false) == NativeChatResult::InvalidInput);
  model.clearMessages();
  for (int i = 0; i < 13; ++i) {
    const std::string id = std::to_string(i);
    assert(model.beginMessage(route, "request", id.c_str(), NativeChatRole::User, 0, 0, false) == NativeChatResult::Ok);
    assert(model.appendChunk(route, "request", id.c_str(), NativeChatRole::User, 0, "x", 1, true, 1) == NativeChatResult::Ok);
  }
  assert(model.messageCount() == 12 && !model.findMessage("0"));
  assert(std::strcmp(model.messageAt(0)->id, "1") == 0 && std::strcmp(model.messageAt(11)->id, "12") == 0);
  assert(!model.messageAt(12));
  assert(model.expectHistory("reload-one", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "reload-one") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "reload-one", "1", NativeChatRole::User, 0, 1, true, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "reload-one", "1", NativeChatRole::User, 0, "x", 1, true) == NativeChatResult::Ok);
  assert(model.endHistory(route, "reload-one", false) == NativeChatResult::Ok);
  assert(model.beginMessage(route, "next", "13", NativeChatRole::User, 0, 0, false) == NativeChatResult::Ok);
  assert(model.findMessage("1") && !model.findMessage("2"));
  assert(model.activate("unknown", "default", 1) == NativeChatResult::InvalidInput && !model.ready());
  const std::string profile(64, 'p'), tooLongProfile(65, 'p');
  assert(model.activate("codex", profile.c_str(), 2) == NativeChatResult::Ok);
  assert(model.activate("codex", tooLongProfile.c_str(), 2) == NativeChatResult::InvalidInput);
  assert(model.activate("codex", "default", 0) == NativeChatResult::InvalidInput);
  assert(model.activate("codex", "default", 1) == NativeChatResult::Ok);
  const std::string longSession(225, 's');
  NativeChatRoute invalidRoute = route; invalidRoute.session_id = longSession.c_str();
  assert(model.beginMessage(invalidRoute, "request", "bad", NativeChatRole::User, 0, 0, false) == NativeChatResult::InvalidInput);
}

static void draftsNeverEvicted() {
  NativeChatModel model;
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    assert(model.activate(kNativeAgents[i].id, "default", static_cast<uint32_t>(i + 1)) == NativeChatResult::Ok);
    assert(model.draft()->insert(kNativeAgents[i].id) == NativeChatEditResult::Ok);
  }
  assert(model.activate("codex", "seventeenth", 20) == NativeChatResult::DraftCapacity);
  assert(!model.ready() && !model.draft() && model.messageCount() == 0);
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    assert(model.activate(kNativeAgents[i].id, "default", 21) == NativeChatResult::Ok);
    assert(std::strcmp(model.draft()->data(), kNativeAgents[i].id) == 0);
  }
  assert(model.activate("hermes", "default", 22) == NativeChatResult::Ok);
  model.draft()->clear();
  assert(model.activate("codex", "seventeenth", 23) == NativeChatResult::Ok);
  assert(model.draft()->empty());
  assert(model.activate("codex", "default", 24) == NativeChatResult::Ok);
  assert(std::strcmp(model.draft()->data(), "codex") == 0);
}

static void allocationFree() {
  const size_t before = allocations;
  NativeChatModel model;
  const NativeChatRoute route("ekko-agent", "default", 1, "session");
  assert(model.activate("ekko-agent", "default", 1, "machine/account/scoped") == NativeChatResult::Ok);
  model.draft()->insert("hello world"); model.draft()->moveLeft(); model.draft()->backspace();
  assert(model.beginMessage(route, "request", "message", NativeChatRole::Assistant, 0, 0, false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "request", "message", NativeChatRole::Assistant, 0, "answer", 6, true, 6) == NativeChatResult::Ok);
  assert(model.expectHistory("history", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "history") == NativeChatResult::Ok);
  assert(model.endHistory(route, "history", false) == NativeChatResult::Ok);
  assert(model.messageAt(0) && model.findMessage("message"));
  model.hideContext();
  assert(!model.context() && !model.draft() && !model.messageAt(0));
  assert(model.activate("ekko-agent", "default", 2, "machine/account/scoped") == NativeChatResult::Ok);
  assert(model.messageAt(0) && model.draft()->hasContent());
  assert(allocations == before);
}

static void unfinishedHistorySnapshots() {
  NativeChatModel model;
  assert(model.activate("hermes", "default", 4) == NativeChatResult::Ok);
  const NativeChatRoute route("hermes", "default", 4, "native-hermes");
  const std::string body = std::string(1023, 'x') + "中" + std::string(1000, 'z');
  // Actual persisted assistant shape: long text, finish_reason=null maps to
  // messageFinal=false. The selected DB window still has a transport final.
  assert(model.expectHistory("snapshot") == NativeChatResult::Ok);
  assert(model.beginHistory(route, "snapshot") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "snapshot", "hermes-null-finish", NativeChatRole::Assistant,
                            0, static_cast<uint32_t>(body.size()), false, true) == NativeChatResult::Ok);
  assert(!nativeChatWindowReadable(*model.findMessage("hermes-null-finish")));
  assert(model.appendChunk(route, "snapshot", "hermes-null-finish", NativeChatRole::Assistant,
                           0, body.data(), 512, false) == NativeChatResult::Ok);
  assert(!nativeChatWindowReadable(*model.findMessage("hermes-null-finish")));
  assert(model.appendChunk(route, "snapshot", "hermes-null-finish", NativeChatRole::Assistant,
                           512, body.data() + 512, 511, true) == NativeChatResult::Ok);
  const NativeChatMessage* first = model.findMessage("hermes-null-finish");
  assert(!first->message_final && first->stored_bytes == 1023 && first->truncated_after);
  assert(nativeChatWindowReadable(*first));
  assert(model.endHistory(route, "snapshot", false) == NativeChatResult::Ok);

  assert(model.expectHistory("next-window", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "next-window") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "next-window", "hermes-null-finish", NativeChatRole::Assistant,
                            1023, static_cast<uint32_t>(body.size()), false, true) == NativeChatResult::Ok);
  assert(!nativeChatWindowReadable(*model.findMessage("hermes-null-finish")));
  assert(model.appendChunk(route, "next-window", "hermes-null-finish", NativeChatRole::Assistant,
                           1023, body.data() + 1023, 512, false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "next-window", "hermes-null-finish", NativeChatRole::Assistant,
                           1535, body.data() + 1535, body.size() - 1535, true) == NativeChatResult::Ok);
  const NativeChatMessage* tail = model.findMessage("hermes-null-finish");
  assert(std::strcmp(tail->text, body.c_str() + 1023) == 0);
  assert(tail->truncated_before && !tail->truncated_after && !tail->message_final);
  assert(nativeChatWindowReadable(*tail));
  assert(model.endHistory(route, "next-window", false) == NativeChatResult::Ok);

  assert(model.beginMessage(route, "live", "unfinished-live", NativeChatRole::Assistant,
                            0, 0, false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "live", "unfinished-live", NativeChatRole::Assistant,
                           0, "thinking", 8, false) == NativeChatResult::Ok);
  assert(!nativeChatWindowReadable(*model.findMessage("unfinished-live")));
  assert(model.appendChunk(route, "live", "unfinished-live", NativeChatRole::Assistant,
                           8, "", 0, true, 8) == NativeChatResult::Ok);
  assert(nativeChatWindowReadable(*model.findMessage("unfinished-live")));
  // A user echo can declare logical final in begin, before its text is read.
  assert(model.beginMessage(route, "user", "user-final-begin", NativeChatRole::User,
                            0, 3, true) == NativeChatResult::Ok);
  assert(!nativeChatWindowReadable(*model.findMessage("user-final-begin")));
  assert(model.appendChunk(route, "user", "user-final-begin", NativeChatRole::User,
                           0, "abc", 3, true) == NativeChatResult::Ok);
  assert(nativeChatWindowReadable(*model.findMessage("user-final-begin")));
}

static void bindingIsolationAndHiddenViews() {
  NativeChatModel model;
  const char* bindings[] = {
    "machine-a/account-a/scoped", "machine-b/account-a/scoped",
    "machine-a/account-b/scoped", "machine-a/account-a/global"
  };
  const NativeChatRoute route("codex", "default", 1, "same-session");
  for (size_t i = 0; i < 4; ++i) {
    assert(model.activate("codex", "default", 1, bindings[i]) == NativeChatResult::Ok);
    assert(std::strcmp(model.context()->binding, bindings[i]) == 0);
    assert(model.messageCount() == 0 && model.draft()->empty());
    const std::string draft = "draft-" + std::to_string(i);
    const std::string text = "account-history-" + std::to_string(i);
    assert(model.draft()->insert(draft.c_str()) == NativeChatEditResult::Ok);
    assert(model.beginMessage(route, "same-request", "same-id", NativeChatRole::Assistant,
                              0, 0, false) == NativeChatResult::Ok);
    assert(model.appendChunk(route, "same-request", "same-id", NativeChatRole::Assistant,
                             0, text.data(), text.size(), true) == NativeChatResult::Ok);
  }
  for (size_t i = 0; i < 4; ++i) {
    assert(model.activate("codex", "default", 1, bindings[i]) == NativeChatResult::Ok);
    assert(model.messageCount() == 1 && !model.ready());
    assert(std::string(model.draft()->data()) == "draft-" + std::to_string(i));
    assert(std::string(model.findMessage("same-id")->text) == "account-history-" + std::to_string(i));
  }
  // Hide immediately, before a new account's configuration request can fail.
  assert(model.expectHistory("pending-old-account", false) == NativeChatResult::Ok);
  const uint32_t revision = model.revision();
  model.hideContext();
  assert(model.revision() > revision && !model.ready() && !model.selectionReady() && model.version() == 0);
  assert(!model.context() && !model.draft() && !model.inputMethod());
  assert(model.messageCount() == 0 && !model.messageAt(0) && !model.findMessage("same-id"));
  assert(std::strcmp(model.agentId(), "") == 0);
  assert(model.beginHistory(route, "pending-old-account") == NativeChatResult::Stale);
  assert(model.beginMessage(route, "late-old-account", "late", NativeChatRole::Assistant,
                            0, 0, false) == NativeChatResult::Stale);
  // Failed configuration/identity validation must not reveal the prior view.
  assert(model.activate("codex", "default", 1, "bad\nidentifier") == NativeChatResult::InvalidInput);
  assert(!model.context() && !model.draft() && model.messageCount() == 0);
  model.hideContext();
  for (size_t i = 0; i < 4; ++i) {
    assert(model.activate("codex", "default", 1, bindings[i]) == NativeChatResult::Ok);
    assert(std::string(model.draft()->data()) == "draft-" + std::to_string(i));
    assert(std::string(model.findMessage("same-id")->text) == "account-history-" + std::to_string(i));
  }
  model.disconnect();
  assert(!model.ready() && !model.selectionReady() && model.context());
  assert(model.messageCount() == 1 && model.draft()->hasContent());
  assert(std::string(model.findMessage("same-id")->text) == "account-history-3");
}

static void bindingDraftCapacityAndLru() {
  NativeChatModel model;
  for (size_t i = 0; i < kNativeChatContextCount; ++i) {
    const std::string binding = "binding-" + std::to_string(i);
    assert(model.activate("codex", "default", 1, binding.c_str()) == NativeChatResult::Ok);
    assert(model.draft()->insert(binding.c_str()) == NativeChatEditResult::Ok);
  }
  assert(model.activate("codex", "default", 1, "seventeenth-binding") == NativeChatResult::DraftCapacity);
  assert(!model.context() && !model.draft() && !model.messageAt(0));
  for (size_t i = 0; i < kNativeChatContextCount; ++i) {
    const std::string binding = "binding-" + std::to_string(i);
    assert(model.activate("codex", "default", 1, binding.c_str()) == NativeChatResult::Ok);
    assert(std::string(model.draft()->data()) == binding);
  }
  assert(model.activate("codex", "default", 1, "binding-5") == NativeChatResult::Ok);
  model.draft()->clear();
  assert(model.activate("codex", "default", 1, "seventeenth-binding") == NativeChatResult::Ok);
  assert(model.draft()->empty() && std::string(model.context()->binding) == "seventeenth-binding");

  NativeChatModel composing;
  NativeChatKey letter; letter.kind = NativeChatKeyKind::Insert; letter.character = 'n';
  for (size_t i = 0; i < kNativeChatContextCount; ++i) {
    const std::string binding = "composing-" + std::to_string(i);
    assert(composing.activate("codex", "default", 1, binding.c_str()) == NativeChatResult::Ok);
    assert(composing.inputMethod()->handle(letter, *composing.draft()).handled);
    assert(composing.draft()->empty() && composing.inputMethod()->composing());
  }
  assert(composing.activate("codex", "default", 1, "extra-binding") == NativeChatResult::DraftCapacity);
  for (size_t i = 0; i < kNativeChatContextCount; ++i) {
    const std::string binding = "composing-" + std::to_string(i);
    assert(composing.activate("codex", "default", 1, binding.c_str()) == NativeChatResult::Ok);
    assert(std::strcmp(composing.inputMethod()->preedit(), "n") == 0);
  }

  NativeChatModel lru;
  const NativeChatRoute route("codex", "default", 1, "session");
  for (size_t i = 0; i < kNativeChatContextCount; ++i) {
    const std::string binding = "clean-" + std::to_string(i);
    assert(lru.activate("codex", "default", 1, binding.c_str()) == NativeChatResult::Ok);
    if (i < 2) {
      assert(lru.beginMessage(route, "request", "cached", NativeChatRole::Assistant, 0, 0, false) == NativeChatResult::Ok);
      assert(lru.appendChunk(route, "request", "cached", NativeChatRole::Assistant, 0, "old", 3, true) == NativeChatResult::Ok);
    }
  }
  assert(lru.activate("codex", "default", 1, "clean-0") == NativeChatResult::Ok && lru.messageCount() == 1);
  assert(lru.activate("codex", "default", 1, "new-binding") == NativeChatResult::Ok && lru.messageCount() == 0);
  assert(lru.activate("codex", "default", 1, "clean-0") == NativeChatResult::Ok && lru.messageCount() == 1);
  assert(lru.activate("codex", "default", 1, "clean-1") == NativeChatResult::Ok && lru.messageCount() == 0);

  // The only clean slot can also own the caller's borrowed binding pointer.
  NativeChatModel alias;
  for (size_t i = 0; i < kNativeChatContextCount; ++i) {
    const std::string binding = "alias-" + std::to_string(i);
    assert(alias.activate("codex", "default", 1, binding.c_str()) == NativeChatResult::Ok);
    if (i + 1 < kNativeChatContextCount) alias.draft()->insert("dirty");
  }
  assert(alias.activate("hermes", "other-profile", 2, alias.context()->binding) == NativeChatResult::Ok);
  assert(std::strcmp(alias.context()->binding, "alias-15") == 0);
}

static void bindingValidationAndLegacyNamespace() {
  NativeChatModel model;
  assert(model.activate("codex", "default", 1) == NativeChatResult::Ok);
  assert(model.context()->binding[0] == 0);
  model.draft()->insert("legacy draft");
  assert(model.activate("codex", "default", 1, "explicit-binding") == NativeChatResult::Ok);
  assert(model.draft()->empty()); model.draft()->insert("bound draft");
  assert(model.activate("codex", "default", 2, "") == NativeChatResult::Ok);
  assert(std::strcmp(model.draft()->data(), "legacy draft") == 0);
  assert(model.activate("codex", "default", 3, "explicit-binding") == NativeChatResult::Ok);
  assert(std::strcmp(model.draft()->data(), "bound draft") == 0);

  const std::string maximum(kNativeChatBindingBytes, 'b'), oversized(kNativeChatBindingBytes + 1, 'b');
  assert(model.activate("codex", "default", 1, maximum.c_str()) == NativeChatResult::Ok);
  assert(std::strcmp(model.context()->binding, maximum.c_str()) == 0);
  model.draft()->insert("maximum preserved");
  assert(model.activate("codex", "default", 1, oversized.c_str()) == NativeChatResult::InvalidInput);
  assert(!model.context() && !model.draft());
  assert(model.activate("codex", "default", 2, maximum.c_str()) == NativeChatResult::Ok);
  assert(std::strcmp(model.draft()->data(), "maximum preserved") == 0);
  for (const char* invalid : {static_cast<const char*>(nullptr), "bad\nkey", "bad\x7f", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4\xb8"}) {
    assert(model.activate("codex", "default", 1, invalid) == NativeChatResult::InvalidInput);
    assert(!model.context() && model.messageCount() == 0);
  }
  const char nulBinding[]{'a', 0, 'b'};
  assert(model.activate("codex", "default", 1, nulBinding, sizeof(nulBinding)) == NativeChatResult::InvalidInput);
  assert(!model.context());
  const char spanBinding[]{'a', 'b', 'c'};
  assert(model.activate("codex", "default", 1, spanBinding, sizeof(spanBinding)) == NativeChatResult::Ok);
  assert(std::strcmp(model.context()->binding, "abc") == 0);
  assert(model.activate("codex", "default", 2, "abc") == NativeChatResult::Ok);
  assert(model.activate("codex", "default", 1, nullptr, 0) == NativeChatResult::InvalidInput);
  assert(model.activate("codex", "default", 1, oversized.data(), oversized.size()) == NativeChatResult::InvalidInput);
  assert(model.activate("codex", "default", 1, "中文绑定") == NativeChatResult::Ok);
  assert(std::strcmp(model.context()->binding, "中文绑定") == 0);
}

int main() {
  streamAndRoutes(); longMessageWindows(); historyTransactions(); limitsAndEviction();
  draftsNeverEvicted(); allocationFree(); unfinishedHistorySnapshots();
  bindingIsolationAndHiddenViews(); bindingDraftCapacityAndLru(); bindingValidationAndLegacyNamespace();
  // Pending pinyin is unsent user text too: preserve it per Agent/profile and
  // never evict a composing context merely because its committed draft is empty.
  NativeChatModel composingContexts;
  NativeChatKey letter; letter.kind = NativeChatKeyKind::Insert; letter.character = 'n';
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    assert(composingContexts.activate(kNativeAgents[i].id, "default", i + 1) == NativeChatResult::Ok);
    assert(composingContexts.inputMethod()->enabled());
    assert(composingContexts.inputMethod()->handle(letter, *composingContexts.draft()).handled);
    assert(composingContexts.draft()->empty() && composingContexts.inputMethod()->composing());
  }
  assert(composingContexts.activate("codex", "another-profile", 99) == NativeChatResult::DraftCapacity);
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    assert(composingContexts.activate(kNativeAgents[i].id, "default", 100 + i) == NativeChatResult::Ok);
    assert(std::strcmp(composingContexts.inputMethod()->preedit(), "n") == 0);
  }
  std::cout << "Chat model: stream/offset/routes/session/window/history/LRU/dirty-draft/zero-heap passed; sizeof="
            << sizeof(NativeChatModel) << " (context=" << sizeof(NativeChatContext)
            << ", message=" << sizeof(NativeChatMessage) << ")\n";
}
