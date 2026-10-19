#include "ChatBrowser.h"
#include "ChatKeyboard.h"
#include "ChatRenderer.h"
#include <cassert>
#include <iostream>
#include <string>

static NativeChatBrowseViewport viewportOf(const NativeChatRenderMetrics& metrics) {
  NativeChatBrowseViewport viewport;
  viewport.firstMessage = metrics.firstVisibleMessage;
  viewport.lastMessage = metrics.lastVisibleMessage;
  viewport.maxBottomOffset = metrics.maxBottomOffset;
  viewport.topWindowBoundary = metrics.topWindowBoundary;
  viewport.bottomWindowBoundary = metrics.bottomWindowBoundary;
  return viewport;
}
static void addWindow(NativeChatModel& model, const NativeChatRoute& route, const char* request,
    const char* id, const std::string& text, uint32_t offset, uint32_t total, bool messageFinal = true) {
  assert(model.beginMessage(route, request, id, NativeChatRole::Assistant, offset, total,
                            messageFinal, true) == NativeChatResult::Ok);
  size_t position = 0;
  do {
    const size_t length = std::min<size_t>(512, text.size() - position);
    assert(model.appendChunk(route, request, id, NativeChatRole::Assistant,
        offset + static_cast<uint32_t>(position), text.data() + position, length,
        position + length == text.size()) == NativeChatResult::Ok);
    position += length;
  } while (position < text.size());
}
static void page(NativeChatModel& model, const NativeChatRoute& route, const char* request,
    unsigned first, unsigned count, bool older, bool newer) {
  assert(model.expectHistory(request) == NativeChatResult::Ok);
  assert(model.beginHistory(route, request) == NativeChatResult::Ok);
  for (unsigned i = 0; i < count; ++i) {
    const std::string id = "message-" + std::to_string(first + i);
    const std::string text = "content-" + std::to_string(first + i);
    addWindow(model, route, request, id.c_str(), text, 0, static_cast<uint32_t>(text.size()));
  }
  assert(model.endHistory(route, request, older, newer) == NativeChatResult::Ok);
}

int main() {
  NativeChatModel model;
  assert(model.activate("hermes", "default", 1) == NativeChatResult::Ok);
  const NativeChatRoute route("hermes", "default", 1, "native-hermes");
  NativeChatRenderOptions options;
  options.historyMode = true;
  NativeChatModifiers mods;

  // An actual bare physical Up event changes the rendered viewport, not the draft.
  assert(model.expectHistory("long-page") == NativeChatResult::Ok);
  assert(model.beginHistory(route, "long-page") == NativeChatResult::Ok);
  std::string lines;
  for (unsigned i = 0; i < 30; ++i) lines += "line-" + std::to_string(i) + "!\n";
  addWindow(model, route, "long-page", "scrollable", lines, 0, static_cast<uint32_t>(lines.size()), false);
  assert(model.endHistory(route, "long-page", false, false) == NativeChatResult::Ok);
  assert(nativeChatKey(';', ';', mods, true).kind == NativeChatKeyKind::ScrollUp);
  M5Canvas bottomFrame;
  auto metrics = nativeDrawChat(bottomFrame, model, options);
  const uint16_t bottomFirst = metrics.firstLine;
  auto plan = nativeChatBrowsePlan(model, viewportOf(metrics), 0, NativeChatBrowseDirection::Up);
  assert(plan.action == NativeChatBrowseAction::Scroll && plan.bottomOffset == 3);
  options.bottomOffsetLines = static_cast<uint16_t>(plan.bottomOffset);
  M5Canvas olderFrame;
  metrics = nativeDrawChat(olderFrame, model, options);
  assert(metrics.firstLine + 3 == bottomFirst);
  assert(model.draft()->empty());
  plan = nativeChatBrowsePlan(model, viewportOf(metrics), options.bottomOffsetLines, NativeChatBrowseDirection::Down);
  assert(plan.action == NativeChatBrowseAction::Scroll && plan.bottomOffset == 0);

  // Both page directions preserve the chronological reading edge.
  page(model, route, "middle-page", 13, 12, true, true);
  options.bottomOffsetLines = 0;
  M5Canvas middleFrame;
  metrics = nativeDrawChat(middleFrame, model, options);
  assert(nativeChatKey(',', ',', mods, true).kind == NativeChatKeyKind::HistoryOlder);
  plan = nativeChatBrowsePlan(model, viewportOf(metrics), 0, NativeChatBrowseDirection::OlderPage);
  assert(plan.action == NativeChatBrowseAction::OlderPage && plan.messageIndex == 0);
  assert(plan.focus == NativeChatBrowseFocus::NewestEnd);
  assert(nativeChatPageBottomOffset(plan.focus, 8) == 0);
  assert(nativeChatKey('/', '/', mods, true).kind == NativeChatKeyKind::HistoryNewer);
  plan = nativeChatBrowsePlan(model, viewportOf(metrics), 0, NativeChatBrowseDirection::NewerPage);
  assert(plan.action == NativeChatBrowseAction::NewerPage && plan.messageIndex == 11);
  assert(plan.focus == NativeChatBrowseFocus::OldestStart);
  page(model, route, "newer-page", 25, 12, true, false);
  M5Canvas newestBottom;
  metrics = nativeDrawChat(newestBottom, model, options);
  assert(metrics.firstVisibleMessage > 0); // Old offset=0 would skip the new page's beginning.
  options.bottomOffsetLines = static_cast<uint16_t>(nativeChatPageBottomOffset(plan.focus, metrics.maxBottomOffset));
  M5Canvas newestStart;
  metrics = nativeDrawChat(newestStart, model, options);
  assert(metrics.firstVisibleMessage == 0 && newestStart.contains("content-25"));
  // R is an explicit return to the latest page. It never submits the draft.
  assert(nativeChatKey('r', 'r', mods, true).kind == NativeChatKeyKind::Refresh);
  options.bottomOffsetLines = 0;
  M5Canvas latest;
  metrics = nativeDrawChat(latest, model, options);
  assert(latest.contains("content-36"));
  plan = nativeChatBrowsePlan(model, viewportOf(metrics), 0, NativeChatBrowseDirection::Down);
  assert(plan.action == NativeChatBrowseAction::None);

  // Long message windows remain readable with finish_reason absent.
  assert(model.expectHistory("first-window") == NativeChatResult::Ok);
  assert(model.beginHistory(route, "first-window") == NativeChatResult::Ok);
  addWindow(model, route, "first-window", "large", std::string(1024, 'x'), 0, 2500, false);
  assert(model.endHistory(route, "first-window", false, false) == NativeChatResult::Ok);
  M5Canvas firstWindow;
  metrics = nativeDrawChat(firstWindow, model, options);
  plan = nativeChatBrowsePlan(model, viewportOf(metrics), 0, NativeChatBrowseDirection::Down);
  assert(plan.action == NativeChatBrowseAction::NextWindow && plan.byteOffset == 1024);
  assert(plan.focus == NativeChatBrowseFocus::MessageStart);
  assert(model.expectHistory("second-window", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "second-window") == NativeChatResult::Ok);
  addWindow(model, route, "second-window", "large", std::string(1024, 'y'), 1024, 2500, false);
  assert(model.endHistory(route, "second-window", false, false) == NativeChatResult::Ok);
  options.bottomOffsetLines = UINT16_MAX;
  M5Canvas secondWindow;
  metrics = nativeDrawChat(secondWindow, model, options);
  plan = nativeChatBrowsePlan(model, viewportOf(metrics), UINT16_MAX, NativeChatBrowseDirection::Up);
  assert(plan.action == NativeChatBrowseAction::PreviousWindow && plan.byteOffset == 0);
  assert(plan.focus == NativeChatBrowseFocus::MessageEnd);
  // At an incomplete live window, keep scrolling locally rather than fetch nonexistent DB text.
  assert(model.beginMessage(route, "live", "unfinished", NativeChatRole::Assistant, 0, 0, false) == NativeChatResult::Ok);
  NativeChatBrowseViewport stale;
  stale.lastMessage = model.messageCount() - 1; stale.bottomWindowBoundary = true;
  plan = nativeChatBrowsePlan(model, stale, 0, NativeChatBrowseDirection::Down);
  assert(plan.action == NativeChatBrowseAction::None);
  std::cout << "Chat browser: physical scroll/render movement, both page focus edges, long windows and latest return passed\n";
}
