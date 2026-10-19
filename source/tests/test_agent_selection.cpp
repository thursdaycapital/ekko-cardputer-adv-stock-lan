#include "AgentSelection.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main() {
  static_assert(kNativeAgentCount == 16, "Native catalog must remain complete");
  for (size_t i = 0; i < kNativeAgentCount; ++i) {
    assert(nativeAgentIndex(kNativeAgents[i].id) == static_cast<int>(i));
    for (size_t j = 0; j < i; ++j)
      assert(std::strcmp(kNativeAgents[i].id, kNativeAgents[j].id) != 0);
  }
  assert(nativeAgentIndex(nullptr) == -1);
  assert(nativeAgentIndex("ekko") == -1); // canonical catalog has no aliases
  assert(nativeAgentIndex("unknown") == -1);
  NativeAgentSelection state;
  assert(!state.ready());
  assert(!nativeInteractionVisible(true, state, false)); // stale thinking, offline
  state.move(-1); assert(state.browse == 15);
  state.move(1); assert(state.browse == 0);
  assert(!state.request(16));
  assert(state.request(0));
  assert(!state.acknowledge(0, 0));
  assert(!state.acknowledge(1, 1));
  assert(state.acknowledge(0, 1) && state.ready());
  assert(nativeInteractionVisible(true, state, false));
  assert(!nativeInteractionVisible(true, state, true)); // explicit routing error
  state.move(1); assert(state.request(state.browse) && !state.ready());
  assert(!nativeInteractionVisible(true, state, false)); // pending overrides task
  state.move(3); // fast keys update only latest target; old ACK cannot rewind it
  assert(!state.request(state.browse));
  assert(state.acknowledge(1, 2));
  assert(state.active == 1 && state.browse == 4);
  assert(!nativeInteractionVisible(true, state, false)); // latest browse wins
  assert(state.request(state.browse) && !state.ready());
  assert(!state.acknowledge(1, 2));
  assert(!state.acknowledge(4, 1));
  assert(state.acknowledge(4, 3) && state.ready());
  state.disconnect();
  assert(!state.ready() && state.version == 0 && state.active == 4);
  assert(!nativeInteractionVisible(true, state, false));
  assert(!state.acknowledge(4, 3));
  assert(state.request(4) && state.acknowledge(4, 1)); // new connection epoch
  std::puts("native 16-agent selection tests passed");
}
