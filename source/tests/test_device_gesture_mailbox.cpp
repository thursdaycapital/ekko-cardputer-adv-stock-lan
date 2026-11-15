#include "DeviceGestureMailbox.h"
#include <cassert>
#include <cstdint>
#include <iostream>

static uint32_t shortBack(DeviceGestureMailbox& input, uint32_t now) {
  input.update(now, true, false);
  input.update(now + 30, true, false);
  input.update(now + 80, false, false);
  input.update(now + 110, false, false);
  return now + 150;
}

static uint32_t chord(DeviceGestureMailbox& input, uint32_t now) {
  input.update(now, false, true);
  input.update(now + 30, false, true);
  input.update(now + 80, false, false);
  input.update(now + 110, false, false);
  return now + 150;
}

static void arm(DeviceGestureMailbox& input) {
  input.update(0, false, false);
  input.update(30, false, false);
}

int main() {
  DeviceGestureMailbox wait;
  arm(wait);
  shortBack(wait, 100);
  // Simulate a bounded speaker/network wait doing all physical scans while
  // the application handler does not run. Released raw state loses no Back.
  wait.update(1000, false, false);
  assert(wait.pop() == DeviceGesture::ShortBack);
  assert(wait.pop() == DeviceGesture::None);

  chord(wait, 1100);
  assert(wait.pop() == DeviceGesture::VoiceStart);
  assert(wait.pop() == DeviceGesture::VoiceRelease);
  assert(!wait.pending());

  DeviceGestureMailbox repeatedBack;
  arm(repeatedBack);
  uint32_t now = 100;
  for (unsigned i = 0; i < 100; ++i) now = shortBack(repeatedBack, now);
  assert(repeatedBack.queuedEntries() == 1);
  for (unsigned i = 0; i < 100; ++i) assert(repeatedBack.pop() == DeviceGesture::ShortBack);
  assert(!repeatedBack.pending());

  DeviceGestureMailbox overflow;
  arm(overflow);
  now = 100;
  for (unsigned i = 0; i < 32; ++i) {
    now = chord(overflow, now);
    now = shortBack(overflow, now);
    assert(overflow.queuedEntries() <= DeviceGestureMailbox::kCapacity);
  }
  unsigned backs = 0;
  for (uint8_t event; (event = overflow.pop()) != DeviceGesture::None;)
    if (event == DeviceGesture::ShortBack) ++backs;
  assert(backs == 32); // Overflow may coalesce voice, never lose Back.
  assert(!overflow.pending());

  DeviceGestureMailbox cap;
  arm(cap);
  cap.update(100, true, false); cap.update(130, true, false);
  cap.update(490, true, false);
  assert(cap.pop() == DeviceGesture::VoiceStart);
  cap.suppressUntilReleased();
  cap.update(10000, true, false);
  cap.update(11000, true, false);
  assert(!cap.pending()); // A held key never reconstructs Start after cap.
  cap.update(11100, false, false); cap.update(11130, false, false); cap.update(11160, false, false);
  chord(cap, 11200);
  assert(cap.pop() == DeviceGesture::VoiceStart);
  cap.suppressUntilReleased();
  assert(cap.pop() == DeviceGesture::None); // Also clears the queued release.

  // Boot held while a new app starts is ignored until both sources release.
  DeviceGestureMailbox startup;
  startup.update(0, true, true); startup.update(1000, true, true);
  assert(!startup.pending());
  startup.update(1100, false, false); startup.update(1130, false, false); startup.update(1160, false, false);
  shortBack(startup, 1200);
  assert(startup.pop() == DeviceGesture::ShortBack && !startup.pending());
  std::cout << "Device gesture mailbox: bounded-wait events, FIFO voice, counted Back, overflow and held-key suppression passed\n";
}
