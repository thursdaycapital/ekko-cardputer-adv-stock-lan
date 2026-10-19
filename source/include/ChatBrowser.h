#pragma once
#include "ChatModel.h"
#include <algorithm>

enum class NativeChatBrowseDirection : uint8_t { Up, Down, OlderPage, NewerPage };
enum class NativeChatBrowseAction : uint8_t { None, Scroll, PreviousWindow, NextWindow, OlderPage, NewerPage };
enum class NativeChatBrowseFocus : uint8_t { Preserve, MessageStart, MessageEnd, OldestStart, NewestEnd };

struct NativeChatBrowseViewport {
  size_t firstMessage = 0, lastMessage = 0, maxBottomOffset = 0;
  bool topWindowBoundary = false, bottomWindowBoundary = false;
};
struct NativeChatBrowsePlan {
  NativeChatBrowseAction action = NativeChatBrowseAction::None;
  NativeChatBrowseFocus focus = NativeChatBrowseFocus::Preserve;
  size_t bottomOffset = 0, messageIndex = 0;
  uint32_t byteOffset = 0;
};

// No transport or display mutation. The caller copies messageId before sending
// a request and applies focus only when that exact history request completes.
inline NativeChatBrowsePlan nativeChatBrowsePlan(const NativeChatModel& model,
    const NativeChatBrowseViewport& viewport, size_t bottomOffset, NativeChatBrowseDirection direction) {
  NativeChatBrowsePlan plan;
  plan.bottomOffset = std::min(bottomOffset, viewport.maxBottomOffset);
  const size_t count = model.messageCount();
  const bool up = direction == NativeChatBrowseDirection::Up;
  const bool down = direction == NativeChatBrowseDirection::Down;
  if (up || down) {
    plan.messageIndex = up ? viewport.firstMessage : viewport.lastMessage;
    const NativeChatMessage* message = model.messageAt(plan.messageIndex);
    if (message && nativeChatWindowReadable(*message) &&
        (up ? viewport.topWindowBoundary && message->truncated_before :
              viewport.bottomWindowBoundary && message->truncated_after)) {
      plan.action = up ? NativeChatBrowseAction::PreviousWindow : NativeChatBrowseAction::NextWindow;
      plan.focus = up ? NativeChatBrowseFocus::MessageEnd : NativeChatBrowseFocus::MessageStart;
      plan.byteOffset = up ? (message->window_offset > 1024 ? message->window_offset - 1020 : 0) :
          message->window_offset + message->stored_bytes;
      return plan;
    }
    if (up && plan.bottomOffset < viewport.maxBottomOffset) {
      plan.action = NativeChatBrowseAction::Scroll;
      plan.bottomOffset += std::min<size_t>(3, viewport.maxBottomOffset - plan.bottomOffset);
      return plan;
    }
    if (down && plan.bottomOffset > 0) {
      plan.action = NativeChatBrowseAction::Scroll;
      plan.bottomOffset -= std::min<size_t>(3, plan.bottomOffset);
      return plan;
    }
  }
  if (count && model.context() && (up || direction == NativeChatBrowseDirection::OlderPage) &&
      model.context()->history_has_more) {
    plan.action = NativeChatBrowseAction::OlderPage;
    plan.focus = NativeChatBrowseFocus::NewestEnd;
    plan.messageIndex = 0;
  } else if (count && model.context() && (down || direction == NativeChatBrowseDirection::NewerPage) &&
      model.context()->history_has_newer) {
    plan.action = NativeChatBrowseAction::NewerPage;
    plan.focus = NativeChatBrowseFocus::OldestStart;
    plan.messageIndex = count - 1;
  }
  return plan;
}

// A newly loaded newer page starts at its oldest message. Starting at its end
// would silently skip the first eleven messages in a full twelve-message page.
inline size_t nativeChatPageBottomOffset(NativeChatBrowseFocus focus, size_t maxBottomOffset,
                                         size_t currentOffset = 0) {
  if (focus == NativeChatBrowseFocus::OldestStart) return maxBottomOffset;
  if (focus == NativeChatBrowseFocus::NewestEnd) return 0;
  return std::min(currentOffset, maxBottomOffset);
}
