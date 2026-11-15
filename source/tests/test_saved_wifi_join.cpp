#include "SavedWifiJoin.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
  using Join = native_wifi::SavedWifiJoin;
  using Result = Join::Result;
  Join join;
  assert(join.poll(0, true) == Result::Idle);
  join.begin(2, 100);
  assert(join.poll(100, true) == Result::Attempt); // Old link cannot win.
  assert(join.index() == 0 && join.poll(18099, false) == Result::Waiting);
  assert(join.poll(18100, false) == Result::Attempt && join.index() == 1);
  assert(join.poll(18101, true) == Result::Connected && !join.active());
  assert(join.poll(18102, false) == Result::Idle);
  join.begin(1, 0);
  assert(join.poll(0, false) == Result::Attempt);
  join.cancel();
  assert(join.poll(40000, true) == Result::Idle); // Explicit setup cancels retry.
  join.begin(0, 0);
  assert(join.poll(0, true) == Result::Exhausted);
  const uint32_t start = UINT32_MAX - 1000;
  join.begin(1, start);
  assert(join.poll(start, false) == Result::Attempt);
  assert(join.poll(start + 17999U, false) == Result::Waiting);
  assert(join.poll(start + 18000U, false) == Result::Exhausted);
  std::puts("Saved Wi-Fi join: nonblocking attempts, stale connection, cancellation and clock wrap passed");
}
