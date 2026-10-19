#include "RenderPolicy.h"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
  static_assert(kNativeFramePixelBytes == 64800);
  static_assert(kNativeFrameAllocationBytes == 64804);
  const size_t minimumFree = kNativeFrameAllocationBytes + kNativeRenderHeapReserve;
  assert(nativeSpriteMemoryAvailable(minimumFree, kNativeFrameAllocationBytes));
  assert(!nativeSpriteMemoryAvailable(minimumFree - 1, kNativeFrameAllocationBytes));
  assert(!nativeSpriteMemoryAvailable(minimumFree, kNativeFrameAllocationBytes - 1));
  assert(!nativeSpriteMemoryAvailable(0, 0));
  assert(!nativeSpriteMemoryAvailable(kNativeFrameAllocationBytes - 1,
                                     std::numeric_limits<size_t>::max()));

  std::array<uint8_t, kNativeMonochromeBytes> pixels{};
  NativeRenderInput input;
  input.message = "已选择"; input.title = "就绪"; input.hint = "等待输入";
  input.address = "192.168.0.102/device"; input.online = true;
  input.monochrome = pixels.data();
  NativeRenderPolicy home;
  assert(home.consider(input, 0) == NativeRenderDecision::Draw);
  home.commit(input, 0);
  // Cover many old eye blinks and look-around frames: no physical submission.
  for (uint32_t now = 160; now <= 20000; now += 160) {
    pixels[(now / 160) % pixels.size()] ^= 0x80;
    assert(home.consider(input, now) == NativeRenderDecision::SameState);
  }
  input.monochrome = nullptr;
  assert(home.consider(input, 20001) == NativeRenderDecision::SameState);

  // Every state/text field that affects painting invalidates immediately.
  const NativeRenderInput baseline = input;
  for (int field = 0; field < 10; ++field) {
    NativeRenderInput changed = baseline;
    switch (field) {
      case 0: changed.online = false; break;
      case 1: changed.active = 1; break;
      case 2: changed.browse = 15; break;
      case 3: changed.message = "等待后台确认"; break;
      case 4: changed.title = "选择中"; break;
      case 5: changed.hint = "新提示"; break;
      case 6: changed.address = "192.168.4.1"; break;
      case 7: changed.error = "native_agent_ack_timeout"; break;
      case 8: changed.interaction = true; break;
      case 9: changed.conversation = true; break;
    }
    assert(home.consider(changed, 1) == NativeRenderDecision::Draw);
  }
  assert(home.consider(baseline, 1) == NativeRenderDecision::SameState);
  NativeRenderPolicy idleChat;
  NativeRenderInput chat = baseline;
  chat.conversation = true;
  idleChat.commit(chat, 1);
  for (uint32_t now = 160; now <= 20000; now += 160) {
    pixels[(now / 160) % pixels.size()] ^= 0x80;
    chat.monochrome = pixels.data();
    assert(idleChat.consider(chat, now) == NativeRenderDecision::SameState);
  }
  assert(idleChat.consider(baseline, 20001) == NativeRenderDecision::Draw);
  chat.chat_revision = 1;
  idleChat.commit(chat, 21000);
  chat.chat_revision = 2;
  assert(idleChat.consider(chat, 21001) == NativeRenderDecision::Throttled);
  assert(idleChat.consider(chat, 21080) == NativeRenderDecision::Draw);
  chat.editor_revision = 1;
  assert(idleChat.consider(chat, 21001) == NativeRenderDecision::Draw);
  chat.editor_revision = 0; chat.chat_scroll = 3;
  assert(idleChat.consider(chat, 21001) == NativeRenderDecision::Draw);
  chat.chat_scroll = 0; chat.chat_notice = "模型限流429";
  assert(idleChat.consider(chat, 21001) == NativeRenderDecision::Draw);
  chat.chat_notice = ""; chat.chat_status = "正在回复";
  assert(idleChat.consider(chat, 21001) == NativeRenderDecision::Draw);
  chat.chat_status = ""; chat.chat_history_mode = true;
  assert(idleChat.consider(chat, 21001) == NativeRenderDecision::Draw);
  idleChat.commit(chat, 21001);
  assert(idleChat.consider(chat, 21002) == NativeRenderDecision::SameState);
  chat.chat_history_mode = false;
  assert(idleChat.consider(chat, 21002) == NativeRenderDecision::Draw);

  chat.input_method_revision = 1;
  assert(idleChat.consider(chat, 21002) == NativeRenderDecision::Draw);
  idleChat.commit(chat, 21002);
  chat.input_method_revision = 2;
  assert(idleChat.consider(chat, 21003) == NativeRenderDecision::Draw);

  NativeRenderPolicy dynamic;
  input = baseline; input.interaction = true; input.monochrome = pixels.data();
  dynamic.commit(input, 100);
  assert(dynamic.consider(input, 101) == NativeRenderDecision::SameState);
  pixels.back() ^= 1; // Compare the full bitmap, not a prefix or pointer.
  assert(dynamic.consider(input, 101) == NativeRenderDecision::Throttled);
  assert(dynamic.consider(input, 179) == NativeRenderDecision::Throttled);
  assert(dynamic.consider(input, 180) == NativeRenderDecision::Draw);
  // Merely considering/throttling an update does not commit its state/time.
  dynamic.commit(input, 180);
  assert(dynamic.consider(input, 181) == NativeRenderDecision::SameState);
  NativeRenderInput error = input;
  error.error = "send_failed";
  assert(dynamic.consider(error, 181) == NativeRenderDecision::Draw);
  NativeRenderInput navigation = input;
  navigation.browse = 1;
  assert(dynamic.consider(navigation, 181) == NativeRenderDecision::Draw);

  input.monochrome = nullptr;
  assert(dynamic.consider(input, 259) == NativeRenderDecision::Throttled);
  assert(dynamic.consider(input, 260) == NativeRenderDecision::Draw);
  dynamic.commit(input, 260);
  assert(dynamic.consider(input, 261) == NativeRenderDecision::SameState);
  input.monochrome = pixels.data();
  assert(dynamic.consider(input, 261) == NativeRenderDecision::Throttled);
  assert(dynamic.consider(input, 340) == NativeRenderDecision::Draw);

  NativeRenderPolicy wrapped;
  wrapped.commit(input, std::numeric_limits<uint32_t>::max() - 39);
  pixels.front() ^= 1;
  assert(wrapped.consider(input, 39) == NativeRenderDecision::Throttled);
  assert(wrapped.consider(input, 40) == NativeRenderDecision::Draw);

  // Cache owns copied strings, so caller buffer reuse cannot change history.
  NativeRenderPolicy owned;
  char mutableTitle[] = "abc";
  NativeRenderInput textInput;
  textInput.title = mutableTitle; textInput.hint = nullptr;
  owned.commit(textInput, 1);
  mutableTitle[2] = 'd';
  assert(owned.consider(textInput, 2) == NativeRenderDecision::Draw);
  mutableTitle[2] = 'c'; textInput.hint = "";
  assert(owned.consider(textInput, 2) == NativeRenderDecision::SameState);
  std::cout << "Render policy: static/bitmap/navigation/errors/throttle/wrap/memory passed\n";
}
