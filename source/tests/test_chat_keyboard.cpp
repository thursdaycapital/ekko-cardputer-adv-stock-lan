#include "ChatKeyboard.h"
#include <cassert>
#include <iostream>

int main() {
  NativeChatModifiers mods;
  for (uint8_t symbol : {uint8_t(','), uint8_t('.'), uint8_t(';'), uint8_t('/'), uint8_t(' ')}) {
    const auto key = nativeChatKey(symbol, symbol, mods);
    assert(key.kind == NativeChatKeyKind::Insert && key.character == symbol);
  }
  assert(nativeChatKey('8', '*', mods).kind == NativeChatKeyKind::Insert);
  assert(nativeChatKey('9', '(', mods).kind == NativeChatKeyKind::Insert);
  assert(nativeChatKey('=', '+', mods).kind == NativeChatKeyKind::Insert);
  assert(nativeChatKey(0x2b, 0x2b, mods).kind == NativeChatKeyKind::ToggleHistory);
  assert(nativeChatKey(';', ';', mods, true).kind == NativeChatKeyKind::ScrollUp);
  assert(nativeChatKey('.', '.', mods, true).kind == NativeChatKeyKind::ScrollDown);
  assert(nativeChatKey(',', ',', mods, true).kind == NativeChatKeyKind::HistoryOlder);
  assert(nativeChatKey('/', '/', mods, true).kind == NativeChatKeyKind::HistoryNewer);
  assert(nativeChatKey('r', 'r', mods, true).kind == NativeChatKeyKind::Refresh);
  assert(nativeChatKey('a', 'a', mods, true).kind == NativeChatKeyKind::Insert);
  assert(nativeChatKey(0x28, 0x28, mods, true).kind == NativeChatKeyKind::ToggleHistory);
  assert(nativeChatKey(0x28, 0x28, mods).kind == NativeChatKeyKind::Send);
  mods.ctrl = true;
  assert(nativeChatKey(0x28, 0x28, mods).kind == NativeChatKeyKind::AcknowledgeSend);
  assert(nativeChatKey(0x28, 0x28, mods, true).kind == NativeChatKeyKind::AcknowledgeSend);
  mods.shift = true;
  assert(nativeChatKey(0x28, 0x28, mods).kind == NativeChatKeyKind::Newline);
  mods.shift = false; mods.fn = true;
  assert(nativeChatKey(0x28, 0x28, mods).kind == NativeChatKeyKind::Newline);
  mods.fn = false; mods.alt = true;
  assert(nativeChatKey(0x28, 0x28, mods).kind == NativeChatKeyKind::None);
  mods = {};
  assert(nativeChatKey('`', '`', mods).kind == NativeChatKeyKind::None);
  mods.shift = true;
  assert(nativeChatKey(0x28, 0x28, mods).kind == NativeChatKeyKind::Newline);
  mods.shift = false; mods.fn = true;
  assert(nativeChatKey(' ', ' ', mods).kind == NativeChatKeyKind::None);
  assert(nativeChatKey(0x2b, 0x2b, mods).kind == NativeChatKeyKind::Insert);
  assert(nativeChatKey(0x28, 0x28, mods, true).kind == NativeChatKeyKind::ToggleHistory);
  assert(nativeChatKey(',', '<', mods).kind == NativeChatKeyKind::Left);
  assert(nativeChatKey('/', '?', mods).kind == NativeChatKeyKind::Right);
  assert(nativeChatKey(';', ':', mods).kind == NativeChatKeyKind::ScrollUp);
  assert(nativeChatKey('.', '>', mods).kind == NativeChatKeyKind::ScrollDown);
  assert(nativeChatKey('`', '~', mods).character == '~');
  assert(nativeChatKey(0x2a, 0x2a, mods).kind == NativeChatKeyKind::Delete);
  mods.fn = false; mods.ctrl = true;
  assert(nativeChatKey('a', 'A', mods).kind == NativeChatKeyKind::None);
  assert(nativeChatKey(' ', ' ', mods).kind == NativeChatKeyKind::ToggleChinese);
  assert(nativeChatKey(' ', ' ', mods, true).kind == NativeChatKeyKind::ToggleChinese);
  mods.shift = true;
  assert(nativeChatKey(' ', ' ', mods).kind == NativeChatKeyKind::ToggleChinese);
  mods.shift = false; mods.fn = true;
  assert(nativeChatKey(' ', ' ', mods).kind == NativeChatKeyKind::None);
  mods.fn = false; mods.alt = true;
  assert(nativeChatKey(' ', ' ', mods).kind == NativeChatKeyKind::None);
  mods.alt = false;
  mods.ctrl = false;

  NativeChatKeyboard keyboard;
  std::array<NativeChatPhysicalKey, 56> keys{};
  keys[41].down = true; keys[41].base = 0x28; keys[41].character = 0x28;
  keyboard.update(keys, mods, false, 0);
  keyboard.update(keys, mods, true, 1, true); // OK opening chat must not submit it.
  NativeChatKey event;
  assert(!keyboard.pop(event));
  keys[41].down = false; keyboard.update(keys, mods, true, 2);
  keys[2].down = true; keys[2].base = '2'; keys[2].character = '2';
  keyboard.update(keys, mods, true, 3);
  assert(keyboard.pop(event) && event.character == '2');
  keys[2].character = '@'; mods.shift = true;
  keyboard.update(keys, mods, true, 4);
  assert(!keyboard.pop(event)); // A modifier change is not another physical press.
  keys[2].down = false; keyboard.update(keys, mods, true, 5);
  keys[13].down = true; keys[13].base = 0x2a; keys[13].character = 0x2a;
  keyboard.update(keys, mods, true, 6);
  assert(keyboard.pop(event) && event.kind == NativeChatKeyKind::Backspace);
  keyboard.update(keys, mods, true, 425); assert(!keyboard.pop(event));
  keyboard.update(keys, mods, true, 426); assert(keyboard.pop(event));
  keyboard.update(keys, mods, true, 427); assert(!keyboard.pop(event));
  keys[13].down = false; keyboard.update(keys, mods, true, 428);
  for (unsigned i = 0; i < 40; ++i) {
    keys[2].down = true; keyboard.update(keys, mods, true, 500 + i * 2);
    keys[2].down = false; keyboard.update(keys, mods, true, 501 + i * 2);
  }
  assert(keyboard.dropped() == 16);
  unsigned count = 0; while (keyboard.pop(event)) ++count;
  assert(count == 24);

  // A physical Tab opens history; Enter leaves it and never sends the draft.
  NativeChatKeyboard history;
  keys = {}; mods = {};
  keys[14].down = true; keys[14].base = 0x2b; keys[14].character = 0x2b;
  history.update(keys, mods, true, 0, false, false);
  assert(history.pop(event) && event.kind == NativeChatKeyKind::ToggleHistory);
  keys[14].down = false; history.update(keys, mods, true, 1, false, true);
  keys[39].down = true; keys[39].base = ';'; keys[39].character = ';';
  history.update(keys, mods, true, 2, false, true);
  assert(history.pop(event) && event.kind == NativeChatKeyKind::ScrollUp);
  history.update(keys, mods, true, 421, false, true); assert(!history.pop(event));
  history.update(keys, mods, true, 422, false, true);
  assert(history.pop(event) && event.kind == NativeChatKeyKind::ScrollUp);
  keys[39].down = false;
  keys[41].down = true; keys[41].base = 0x28; keys[41].character = 0x28;
  history.update(keys, mods, true, 423, false, true);
  assert(history.pop(event) && event.kind == NativeChatKeyKind::ToggleHistory);
  history.update(keys, mods, true, 424, false, false);
  assert(!history.pop(event)); // Held Enter cannot become Send on leaving history.

  // A slightly late Fn chord must scroll on this scan, not after 420ms.
  NativeChatKeyboard chord;
  keys = {}; mods = {};
  keys[39].down = true; keys[39].base = ';'; keys[39].character = ';';
  chord.update(keys, mods, true, 0);
  assert(!chord.pop(event) && chord.pending());
  mods.fn = true; chord.update(keys, mods, true, 1);
  assert(chord.pop(event) && event.kind == NativeChatKeyKind::ScrollUp);
  chord.update(keys, mods, true, 2); assert(!chord.pop(event));
  mods.fn = false; chord.update(keys, mods, true, 3); assert(!chord.pop(event));
  chord.update(keys, mods, true, 4, false, true);
  assert(!chord.pop(event)); // Mode change never replays an already held arrow.

  // Ctrl+Space is one toggle, never a printable space or a repeat action.
  NativeChatKeyboard chinese;
  keys = {}; mods = {}; mods.ctrl = true;
  keys[55].base = keys[55].character = ' '; keys[55].down = true;
  chinese.update(keys, mods, true, 0);
  assert(chinese.pop(event) && event.kind == NativeChatKeyKind::ToggleChinese);
  chinese.update(keys, mods, true, 5000); assert(!chinese.pop(event));
  mods.ctrl = false; chinese.update(keys, mods, true, 5001);
  assert(!chinese.pop(event)); // Releasing Ctrl never inserts the held Space.
  mods.ctrl = true; chinese.update(keys, mods, true, 5002);
  assert(chinese.pop(event) && event.kind == NativeChatKeyKind::ToggleChinese);
  chinese.update(keys, mods, true, 5003); assert(!chinese.pop(event));
  keys[55].down = false; chinese.update(keys, mods, true, 5004);
  keys[55].down = true; chinese.update(keys, mods, true, 5005);
  assert(chinese.pop(event) && event.kind == NativeChatKeyKind::ToggleChinese);
  chinese.update(keys, mods, true, 10000, false, true); assert(!chinese.pop(event));
  keys[55].down = false; mods.ctrl = false; chinese.update(keys, mods, true, 10001, false, true);
  keys[55].down = true; mods.ctrl = true; chinese.update(keys, mods, true, 10002, false, true);
  assert(chinese.pop(event) && event.kind == NativeChatKeyKind::ToggleChinese);

  // Space first, Ctrl on the next scan: no provisional space reaches the draft.
  NativeChatKeyboard lateCtrl;
  keys = {}; mods = {};
  keys[55].base = keys[55].character = ' '; keys[55].down = true;
  lateCtrl.update(keys, mods, true, 0);
  assert(!lateCtrl.pop(event) && lateCtrl.pending());
  mods.ctrl = true; lateCtrl.update(keys, mods, true, 1);
  assert(lateCtrl.pop(event) && event.kind == NativeChatKeyKind::ToggleChinese);
  assert(!lateCtrl.pop(event) && !lateCtrl.pending() && lateCtrl.pressed() == 1);
  keys[55].down = false; mods.ctrl = false; lateCtrl.update(keys, mods, true, 2);
  assert(!lateCtrl.pop(event));

  // Plain Space is committed once on the next scan, before newer ordinary keys.
  NativeChatKeyboard plainSpace;
  keys = {}; mods = {};
  keys[55].base = keys[55].character = ' '; keys[55].down = true;
  plainSpace.update(keys, mods, true, 0); assert(!plainSpace.pop(event));
  keys[30].base = keys[30].character = 'a'; keys[30].down = true;
  plainSpace.update(keys, mods, true, 1);
  assert(plainSpace.pop(event) && event.kind == NativeChatKeyKind::Insert && event.character == ' ');
  assert(plainSpace.pop(event) && event.kind == NativeChatKeyKind::Insert && event.character == 'a');
  plainSpace.update(keys, mods, true, 10000); assert(!plainSpace.pop(event));
  keys[55].down = keys[30].down = false; plainSpace.update(keys, mods, true, 10001);
  keys[55].down = true; plainSpace.update(keys, mods, true, 10002);
  keys[55].down = false; plainSpace.update(keys, mods, true, 10003);
  assert(plainSpace.pop(event) && event.kind == NativeChatKeyKind::Insert && event.character == ' ');
  assert(!plainSpace.pop(event));

  // Voice owns Fn+Space with or without Ctrl. Releasing Fn/Alt cannot create
  // a new input-mode shortcut while both Ctrl and Space remain held.
  NativeChatKeyboard priority;
  keys = {}; mods = {}; mods.fn = true; mods.ctrl = true;
  keys[55].base = keys[55].character = ' '; keys[55].down = true;
  priority.update(keys, mods, true, 0); assert(!priority.pop(event));
  mods.fn = false; priority.update(keys, mods, true, 1); assert(!priority.pop(event));
  keys[55].down = false; priority.update(keys, mods, true, 2);
  mods.alt = true; keys[55].down = true; priority.update(keys, mods, true, 3);
  assert(!priority.pop(event));
  mods.alt = false; priority.update(keys, mods, true, 4); assert(!priority.pop(event));
  keys[55].down = false; priority.update(keys, mods, true, 5);
  keys[55].down = true; priority.update(keys, mods, true, 6);
  assert(priority.pop(event) && event.kind == NativeChatKeyKind::ToggleChinese);

  // Ctrl letters never become draft characters, including after Ctrl release.
  NativeChatKeyboard controls;
  keys = {}; mods = {}; mods.ctrl = true;
  keys[30].base = 'a'; keys[30].character = 'A'; keys[30].down = true;
  controls.update(keys, mods, true, 0); assert(!controls.pop(event));
  mods.ctrl = false; controls.update(keys, mods, true, 1); assert(!controls.pop(event));
  keys[30].down = false; controls.update(keys, mods, true, 2);
  keys[55].base = keys[55].character = ' '; keys[55].down = true; mods.ctrl = true;
  controls.update(keys, mods, false, 3);
  controls.update(keys, mods, true, 4, true);
  controls.update(keys, mods, true, 5000); assert(!controls.pop(event));
  keys[55].down = false; controls.update(keys, mods, true, 5001);
  keys[55].down = true; controls.update(keys, mods, true, 5002);
  assert(controls.pop(event) && event.kind == NativeChatKeyKind::ToggleChinese);
  // Printed direction legends stay exact ASCII when tapped, while Fn may
  // claim them during the 20ms grace period before any draft mutation.
  const uint8_t bases[] = {',', '/', ';', '.'};
  const uint8_t shifted[] = {'<', '?', ':', '>'};
  const NativeChatKeyKind directions[] = {NativeChatKeyKind::Left, NativeChatKeyKind::Right,
      NativeChatKeyKind::ScrollUp, NativeChatKeyKind::ScrollDown};
  for (size_t i = 0; i < 4; ++i) {
    NativeChatKeyboard tap;
    keys = {}; mods = {}; mods.shift = true;
    keys[4].base = bases[i]; keys[4].character = shifted[i]; keys[4].down = true;
    tap.update(keys, mods, true, 0); assert(!tap.pop(event));
    tap.update(keys, mods, true, 19); assert(!tap.pop(event));
    tap.update(keys, mods, true, 20);
    assert(tap.pop(event) && event.kind == NativeChatKeyKind::Insert && event.character == shifted[i]);
    mods.fn = true; tap.update(keys, mods, true, 21);
    assert(!tap.pop(event)); // Already resolved printables cannot become a second action.
    tap.update(keys, mods, true, 2000); assert(!tap.pop(event));

    NativeChatKeyboard fast;
    keys[4].down = true; mods = {};
    fast.update(keys, mods, true, 0); assert(!fast.pop(event));
    keys[4].down = false; fast.update(keys, mods, true, 1);
    assert(fast.pop(event) && event.character == shifted[i] && !fast.pop(event));

    NativeChatKeyboard late;
    keys[4].down = true;
    late.update(keys, mods, true, 0); assert(!late.pop(event));
    mods.fn = true; late.update(keys, mods, true, 15);
    assert(late.pop(event) && event.kind == directions[i] && !late.pop(event));
    late.update(keys, mods, true, 419); assert(!late.pop(event));
    late.update(keys, mods, true, 420); assert(late.pop(event) && event.kind == directions[i]);
    late.update(keys, mods, true, 509); assert(!late.pop(event));
    late.update(keys, mods, true, 510); assert(late.pop(event) && event.kind == directions[i]);
    keys[4].down = false; mods.fn = false; late.update(keys, mods, true, 511);
    assert(!late.pop(event) && !late.pending());
  }

  NativeChatKeyboard lateVoice;
  keys = {}; mods = {};
  keys[55].down = true; keys[55].base = keys[55].character = ' ';
  lateVoice.update(keys, mods, true, 100); assert(lateVoice.pending());
  mods.fn = true; lateVoice.update(keys, mods, true, 115);
  assert(!lateVoice.pop(event) && !lateVoice.pending() && lateVoice.pressed() == 0);
  keys[55].down = false; mods.fn = false; lateVoice.update(keys, mods, true, 116);
  assert(!lateVoice.pop(event));

  // The SDK omits printable metadata for released keys. A modifier and
  // release on the same scan still resolve the saved physical base safely.
  NativeChatKeyboard releasedChord;
  keys = {}; mods = {};
  keys[4].base = keys[4].character = '/'; keys[4].down = true;
  releasedChord.update(keys, mods, true, 0);
  keys = {}; mods.fn = true; releasedChord.update(keys, mods, true, 15);
  assert(releasedChord.pop(event) && event.kind == NativeChatKeyKind::Right);
  assert(!releasedChord.pop(event));

  NativeChatKeyboard fastSpace;
  keys = {}; mods = {}; keys[55].base = keys[55].character = ' ';
  keys[55].down = true; fastSpace.update(keys, mods, true, 0);
  keys[55].down = false; fastSpace.update(keys, mods, true, 1);
  assert(fastSpace.pop(event) && event.kind == NativeChatKeyKind::Insert && event.character == ' ');
  assert(!fastSpace.pop(event));

  NativeChatKeyboard modePending;
  keys[4].base = keys[4].character = ';'; keys[4].down = true;
  modePending.update(keys, mods, true, 0); assert(modePending.pending());
  modePending.update(keys, mods, true, 1, false, true);
  assert(!modePending.pop(event) && !modePending.pending());
  modePending.update(keys, mods, true, 1000, true, false);
  assert(!modePending.pop(event) && !modePending.pending());

  NativeChatKeyboard ordered;
  keys = {}; keys[4].base = keys[4].character = ','; keys[4].down = true;
  ordered.update(keys, mods, true, 0);
  keys[7].base = 'a'; keys[7].character = 'A'; keys[7].down = true; mods.shift = true;
  ordered.update(keys, mods, true, 1);
  assert(ordered.pop(event) && event.character == ',');
  assert(ordered.pop(event) && event.character == 'A' && !ordered.pop(event));

  NativeChatKeyboard acknowledgement;
  keys = {}; mods = {}; mods.ctrl = true;
  keys[41].base = keys[41].character = 0x28; keys[41].down = true;
  acknowledgement.update(keys, mods, true, 0);
  assert(acknowledgement.pop(event) && event.kind == NativeChatKeyKind::AcknowledgeSend);
  acknowledgement.update(keys, mods, true, 5000); assert(!acknowledgement.pop(event));
  mods.ctrl = false; acknowledgement.update(keys, mods, true, 5001);
  assert(!acknowledgement.pop(event)); // Releasing Ctrl cannot turn held OK into Send.
  keys[41].down = false; acknowledgement.update(keys, mods, true, 5002);
  keys[41].down = true; acknowledgement.update(keys, mods, true, 5003);
  assert(acknowledgement.pop(event) && event.kind == NativeChatKeyKind::Send);
  std::cout << "Physical keyboard: typing, modifiers, mode edges, chord grace, repeat and overflow passed\n";
}
