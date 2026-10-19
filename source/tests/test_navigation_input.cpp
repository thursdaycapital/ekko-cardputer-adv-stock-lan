#include "NavigationInput.h"
#include "AgentSelection.h"
#include <cassert>
#include <iostream>
#include <string>

NativeNavigationKeys keys(const std::string& pressed, bool enter = false) {
  return nativeNavigationKeys([&](char key) {
    return pressed.find(key) != std::string::npos;
  }, enter);
}

int main() {
  // Both printable layers of the four physical arrows retain their action.
  for (const auto& item : std::array<std::pair<char, int>, 8>{{
           {',', -1}, {'<', -1}, {';', -1}, {':', -1},
           {'.', 1}, {'>', 1}, {'/', 1}, {'?', 1}}}) {
    NativeNavigationInput input;
    input.update(keys(std::string(1, item.first)));
    assert(input.pending());
    assert(input.pop() == item.second);
    assert(input.pop() == 0);
    input.update(keys(std::string(1, item.first)));
    assert(input.pop() == 0); // Holding a key must not repeat or flood the ACK.
    input.update(keys(""));
    input.update(keys(std::string(1, item.first)));
    assert(input.pop() == item.second);
    assert(input.count() == 2);
  }

  NativeNavigationInput modifiers;
  modifiers.update(keys(",;./"));
  assert(modifiers.count() == 4);
  modifiers.update(keys("<:>?")); // Shift/Ctrl/Caps layer changes while held.
  assert(modifiers.count() == 4);
  assert(modifiers.pop() == -1); // left
  assert(modifiers.pop() == 1);  // right
  assert(modifiers.pop() == -1); // up
  assert(modifiers.pop() == 1);  // down
  assert(modifiers.pop() == 0);
  const auto stats = modifiers.stats();
  assert(stats.left == 1 && stats.right == 1 && stats.up == 1 && stats.down == 1);

  NativeNavigationInput independent;
  independent.update(keys(","));
  independent.update(keys(",/")); // A second physical direction has its own edge.
  assert(independent.pop() == -1 && independent.pop() == 1);
  independent.update(keys("/"));
  independent.update(keys(",/"));
  assert(independent.pop() == -1);
  independent.update(keys("abc123 ", true));
  assert(independent.pop() == 2); // Enter retry is preserved.
  independent.update(keys("", true));
  assert(independent.pop() == 0);
  NativeNavigationInput pageKeys;
  pageKeys.update(keys("", true));
  assert(pageKeys.pending() && !pageKeys.interruptPending());
  assert(pageKeys.stats().ok == 1 && pageKeys.pop() == 2);
  pageKeys.update(keys("`", true));
  assert(pageKeys.interruptPending() && pageKeys.pop() == 3);
  pageKeys.update(keys("~", true)); // Same Esc key, shifted while held.
  assert(pageKeys.pop() == 0 && pageKeys.stats().back == 1);
  assert(pageKeys.count() == 0); // OK/Esc never pretend to be direction keys.
  NativeNavigationInput saturatedOk;
  for (int i = 0; i < 8; ++i) {
    saturatedOk.update(keys("", true)); saturatedOk.update(keys(""));
  }
  assert(saturatedOk.stats().queued == 8 && !saturatedOk.interruptPending());
  saturatedOk.update(keys("`"));
  assert(saturatedOk.interruptPending() && saturatedOk.stats().dropped == 1);
  for (int i = 0; i < 7; ++i) assert(saturatedOk.pop() == 2);
  assert(saturatedOk.pop() == 3 && saturatedOk.pop() == 0);

  NativeNavigationInput fifo;
  for (int cycle = 0; cycle < 32; ++cycle) {
    fifo.update(keys(",;./", true)); // Five simultaneous edges fit the queue.
    assert(fifo.stats().queued == 5);
    assert(fifo.pop() == -1 && fifo.pop() == 1 && fifo.pop() == -1);
    assert(fifo.pop() == 1 && fifo.pop() == 2 && fifo.pop() == 0);
    fifo.update(keys(""));
  }
  assert(fifo.stats().dropped == 0);
  for (int press = 0; press < 9; ++press) {
    fifo.update(keys(",")); fifo.update(keys(""));
  }
  assert(fifo.stats().queued == 8 && fifo.stats().dropped == 1);
  for (int i = 0; i < 8; ++i) assert(fifo.pop() == -1);
  assert(fifo.pop() == 0);

  // Exercise the same movement action used by the native Agent selector,
  // including both ends of the 16-item carousel.
  for (char key : std::string(",;<:.>/?")) {
    NativeNavigationInput input;
    NativeAgentSelection selection;
    input.update(keys(std::string(1, key)));
    const int step = input.pop();
    selection.move(step);
    assert(selection.browse == (step < 0 ? kNativeAgentCount - 1 : 1));
    for (size_t i = 1; i < kNativeAgentCount; ++i) {
      input.update(keys("")); input.update(keys(std::string(1, key)));
      selection.move(input.pop());
    }
    assert(selection.browse == 0);
  }
  std::cout << "Four arrows: physical legends, modifier layers, edges, FIFO, counters and 16-agent wrap passed\n";
}
