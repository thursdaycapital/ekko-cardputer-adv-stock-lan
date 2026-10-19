#include "ConversationView.h"
#include <cassert>
#include <iostream>

NativeAgentSelection selected(size_t index = 0) {
  NativeAgentSelection value;
  value.browse = index;
  assert(value.request(index));
  assert(value.acknowledge(index, 1));
  return value;
}

int main() {
  auto selection = selected();
  NativeConversationView view;
  assert(!view.conversation() && !view.waiting());
  view.requestOpen(selection, "default", true);
  assert(view.conversation() && !view.waiting());
  // Idle and repeated synchronization never close a user-opened page.
  for (int i = 0; i < 100; ++i)
    assert(!view.synchronize(selection, "default", true, false));
  assert(view.conversation());
  view.back();
  assert(!view.conversation());
  view.synchronize(selection, "default", true, false);
  assert(!view.conversation()); // A late ACK/status cannot reopen after Esc.

  selection.move(1);
  assert(selection.request(selection.browse));
  view.requestOpen(selection, "default", true);
  assert(view.waiting() && !view.conversation());
  view.synchronize(selection, "default", true, false);
  assert(!view.conversation());
  assert(!selection.acknowledge(0, 2)); // Stale ACK cannot open the wrong Agent.
  assert(selection.acknowledge(1, 2));
  assert(view.synchronize(selection, "default", true, false));
  assert(view.conversation());

  selection.disconnect();
  view.synchronize(selection, "default", false, false);
  assert(view.conversation()); // Keep the page but gate real interaction/input.
  view.back();
  view.requestOpen(selection, "default", false);
  assert(view.waiting() && !view.conversation());
  view.synchronize(selection, "default", false, true);
  assert(!view.waiting() && !view.conversation());
  selection = selected(1);
  view.synchronize(selection, "default", true, false);
  assert(!view.conversation()); // An error cleared the previous open intent.

  selection.move(1);
  assert(selection.request(selection.browse));
  view.requestOpen(selection, "default", true);
  selection.move(1);
  view.synchronize(selection, "default", true, false);
  assert(!view.waiting() && !view.conversation());
  assert(selection.acknowledge(2, 3));
  view.synchronize(selection, "default", true, false);
  assert(!view.conversation()); // Changing target cancels the entering intent.

  selection = selected();
  view.requestOpen(selection, "default", true);
  view.synchronize(selection, "another-profile", true, false);
  assert(!view.conversation());
  view.requestOpen(selection, "", true);
  assert(!view.conversation()); // A missing profile cannot become usable chat.
  view.requestOpen(selection, "default", false);
  assert(view.waiting());
  view.synchronize(selection, "default", true, false);
  assert(view.conversation());

  // Every canonical Agent supports OK without creating or mutating a run.
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    selection = selected(i);
    const auto version = selection.version;
    view.back(); view.requestOpen(selection, "default", true);
    assert(view.conversation() && selection.active == i && selection.version == version);
  }
  std::cout << "OK chat: ready/waiting/ACK/errors/Back/profile/reconnect and all 16 Agents passed\n";
}
