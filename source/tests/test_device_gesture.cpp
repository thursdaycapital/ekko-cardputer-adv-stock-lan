#include "DeviceGesture.h"
#include <cassert>
#include <cstdint>
#include <iostream>

static void arm(DeviceGesture& gesture, uint32_t now = 0) {
  assert(gesture.update(now, false, false) == DeviceGesture::None);
  assert(gesture.update(now + 30, false, false) == DeviceGesture::None);
  assert(gesture.armed());
}

int main() {
  DeviceGesture back;
  arm(back);
  assert(back.update(100, true, false) == DeviceGesture::None);
  assert(back.update(120, false, false) == DeviceGesture::None); // switch bounce
  assert(back.update(150, false, false) == DeviceGesture::None);
  assert(back.update(200, true, false) == DeviceGesture::None);
  assert(back.update(230, true, false) == DeviceGesture::None);
  assert(back.update(300, false, false) == DeviceGesture::None);
  assert(back.update(329, false, false) == DeviceGesture::None);
  assert(back.update(330, false, false) == DeviceGesture::ShortBack);
  assert(back.update(1000, false, false) == DeviceGesture::None);

  DeviceGesture longPress;
  arm(longPress);
  longPress.update(100, true, false);
  longPress.update(130, true, false);
  assert(longPress.update(489, true, false) == DeviceGesture::None);
  assert(longPress.update(490, true, false) == DeviceGesture::VoiceStart);
  assert(longPress.activeVoice());
  assert(longPress.update(700, true, false) == DeviceGesture::None);
  longPress.update(710, false, false);
  assert(longPress.update(740, false, false) == DeviceGesture::VoiceRelease);
  assert(!longPress.activeVoice());
  assert(longPress.update(1000, false, false) == DeviceGesture::None); // no short Back after voice

  DeviceGesture mixed;
  arm(mixed);
  mixed.update(100, false, true);
  assert(mixed.update(130, false, true) == DeviceGesture::VoiceStart);
  mixed.update(200, true, true);
  assert(mixed.update(230, true, true) == DeviceGesture::None);
  mixed.update(300, true, false);
  assert(mixed.update(330, true, false) == DeviceGesture::None); // G0 still holds voice
  mixed.update(500, false, false);
  assert(mixed.update(530, false, false) == DeviceGesture::VoiceRelease);
  assert(mixed.update(1000, false, false) == DeviceGesture::None);

  DeviceGesture cap;
  arm(cap);
  cap.update(100, false, true);
  assert(cap.update(130, false, true) == DeviceGesture::VoiceStart);
  cap.suppressUntilReleased();
  assert(!cap.activeVoice() && !cap.armed());
  assert(cap.update(6000, false, true) == DeviceGesture::None);
  assert(cap.update(7000, false, true) == DeviceGesture::None);
  cap.update(7100, false, false);
  cap.update(7130, false, false);
  cap.update(7160, false, false);
  assert(cap.armed());
  cap.update(7200, false, true);
  assert(cap.update(7230, false, true) == DeviceGesture::VoiceStart);

  DeviceGesture startup;
  assert(startup.update(0, true, true) == DeviceGesture::None);
  assert(startup.update(2000, true, true) == DeviceGesture::None);
  startup.update(2100, false, false);
  startup.update(2130, false, false);
  startup.update(2160, false, false);
  assert(startup.armed());
  startup.update(2200, true, false);
  startup.update(2230, true, false);
  startup.update(2250, false, false);
  assert(startup.update(2280, false, false) == DeviceGesture::ShortBack);

  // Millisecond overflow cannot turn a short press into an immediate voice.
  DeviceGesture wrap;
  constexpr uint32_t nearWrap = UINT32_MAX - 200;
  arm(wrap, nearWrap - 100);
  wrap.update(nearWrap, true, false);
  wrap.update(nearWrap + 30, true, false);
  assert(wrap.update(nearWrap + 389, true, false) == DeviceGesture::None);
  assert(wrap.update(nearWrap + 390, true, false) == DeviceGesture::VoiceStart);
  std::cout << "Device gestures: startup release, debounce, short back, long voice, chords, cap and wrap passed\n";
}
