#include "KeyClick.h"
#include "ChatKeyboard.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

using native_key_click::Input;
using native_key_click::Request;
using Keys = std::array<NativeChatPhysicalKey, 56>;

static void factoryWaveform() {
  static_assert(native_key_click::kSampleCount == 1920, "Factory duplicate sample count");
  static_assert(native_key_click::kPlaybackRate == 44100, "Factory SDK default input rate");
  std::array<int16_t, native_key_click::kSampleCount + 2> guarded{};
  guarded.front() = 12345;
  guarded.back() = -12345;
  constexpr int scale[] = {60, 62, 64, 65, 67, 69, 71};
  constexpr double pi = 3.14159265358979323846;
  for (uint8_t note = 0; note < 7; ++note) {
    native_key_click::render(note, guarded.data() + 1);
    const int frequency = static_cast<int>(440.0 * std::pow(2.0, (scale[note] + 48 - 69) / 12.0));
    int peak = 0;
    for (int frame = 0; frame < 960; ++frame) {
      // Independent transcription of the factory generator; compare every sample.
      float amplitude = 32767.0f / 5;
      if (frame >= 960 - 200) amplitude *= static_cast<float>(960 - frame) / 200;
      const int16_t expected = static_cast<int16_t>(amplitude * std::sin(2.0 * pi * frequency * frame / 48000));
      assert(guarded[1 + frame * 2] == expected);
      assert(guarded[2 + frame * 2] == expected);
      const int magnitude = std::abs(static_cast<int>(expected));
      peak = std::max(peak, magnitude);
      assert(magnitude <= 6553);
      if (frame >= 760) assert(magnitude <= static_cast<int>(6553.4 * (960 - frame) / 200));
    }
    assert(peak > 6500);
    assert(guarded[1] == 0 && guarded[2] == 0);
    assert(std::abs(static_cast<int>(guarded[1920])) <= 32);
    assert(guarded.front() == 12345 && guarded.back() == -12345);
  }
  std::array<int16_t, native_key_click::kSampleCount> expected{}, normalized{};
  native_key_click::render(255 % 7, expected.data());
  native_key_click::render(255, normalized.data());
  assert(expected == normalized);
  native_key_click::render(0, nullptr);
}

static void primeAndEdges() {
  Input input;
  Keys keys{};
  Request request;
  unsigned randomCalls = 0;
  const auto random = [&]() { ++randomCalls; return 10u; };
  keys[3] = {true, 'a', 'a'};
  input.update(keys, 0, random); // Already held at boot is silent.
  assert(!input.take(0, true, request));
  assert(input.stats().pressed == 0 && input.stats().suppressed == 0);
  input.update(keys, 1000, random);
  assert(!input.take(1000, true, request));
  keys[3].down = false;
  input.update(keys, 1001, random);
  assert(!input.take(1001, true, request));
  keys[3].down = true;
  input.update(keys, 1002, random);
  assert(input.take(1002, true, request) && request.noteIndex == 3 && request.atMs == 1002);
  assert(!input.take(1002, true, request));
  for (uint32_t now : {1003u, 1422u, 3000u}) {
    input.update(keys, now, random);
    assert(!input.take(now, true, request)); // No long-press repeats.
  }
  keys[4] = {true, 0, 0}; // Fn; no printable/semantic event is required.
  input.update(keys, 3001, random);
  assert(input.take(3001, true, request) && request.noteIndex == 3);
  keys[5] = {true, 0x81, 0}; // Shift-like physical modifier.
  input.update(keys, 3002, random);
  assert(input.take(3002, true, request));
  assert(input.stats().pressed == 3 && input.stats().suppressed == 0 && randomCalls == 3);
}

static void notesAndReplacement() {
  Input input;
  Keys keys{};
  Request request;
  unsigned randomCalls = 0;
  const auto random = [&]() { ++randomCalls; return 20u; };
  input.update(keys, 0, random);
  for (uint8_t note = 0; note < 7; ++note) {
    keys = {};
    input.update(keys, note * 10 + 1, random);
    keys[1] = {true, static_cast<uint8_t>('1' + note), 0};
    input.update(keys, note * 10 + 2, random);
    assert(input.take(note * 10 + 2, true, request) && request.noteIndex == note);
  }
  assert(randomCalls == 0);
  keys[2] = {true, '8', '8'};
  input.update(keys, 80, random);
  keys[3] = {true, 'q', 'q'}; // Adjacent press while the previous key remains held.
  input.update(keys, 81, random);
  assert(input.stats().pressed == 9 && input.stats().suppressed == 1);
  assert(input.take(81, true, request) && request.noteIndex == 6 && request.atMs == 81);
  keys[4] = {true, '1', '1'};
  keys[5] = {true, '7', '7'};
  input.update(keys, 82, random); // Same scan: newest by physical scan order wins.
  assert(input.take(82, true, request) && request.noteIndex == 6);
  assert(input.stats().pressed == 11 && input.stats().suppressed == 2);
}

static void blockedAndDiscarded() {
  Input input;
  Keys keys{};
  Request request;
  unsigned randomCalls = 0;
  const auto random = [&]() { ++randomCalls; return 0u; };
  input.update(keys, 0, random);
  keys[0] = {true, 'a', 'a'};
  input.update(keys, 1, random, false);
  assert(input.stats().pressed == 1 && input.stats().suppressed == 1 && randomCalls == 0);
  input.update(keys, 2, random, true);
  assert(!input.take(2, true, request)); // Becoming available never replays held keys.
  keys[1] = {true, 'b', 'b'};
  input.update(keys, 3, random);
  assert(!input.take(3, false, request));
  assert(!input.take(4, true, request));
  keys[2] = {true, 'c', 'c'};
  input.update(keys, 5, random);
  input.discard();
  input.discard(); // Empty discard does not double count.
  assert(!input.take(5, true, request));
  keys[3] = {true, 'd', 'd'};
  input.update(keys, 6, random);
  input.update(keys, 7, random, false); // Ownership loss drops an already queued click.
  input.update(keys, 8, random);
  assert(!input.take(8, true, request));
  assert(input.stats().pressed == 4 && input.stats().suppressed == 4);
}

static void ageAndWrap() {
  Input input;
  Keys keys{};
  Request request;
  const auto random = []() { return std::numeric_limits<uint32_t>::max(); };
  input.update(keys, 0, random);
  keys[0] = {true, 'x', 'x'};
  input.update(keys, 100, random);
  assert(input.take(180, true, request)); // 80 ms is still current.
  assert(request.noteIndex == std::numeric_limits<uint32_t>::max() % 7);
  keys[1] = {true, 'x', 'x'};
  input.update(keys, 200, random);
  assert(!input.take(281, true, request));
  const uint32_t beforeWrap = std::numeric_limits<uint32_t>::max() - 39;
  keys[2] = {true, 'x', 'x'};
  input.update(keys, beforeWrap, random);
  assert(input.take(40, true, request) && request.atMs == beforeWrap); // 80 ms across wrap.
  keys[3] = {true, 'x', 'x'};
  input.update(keys, beforeWrap, random);
  assert(!input.take(41, true, request));
  assert(input.stats().pressed == 4 && input.stats().suppressed == 2);
}

int main() {
  factoryWaveform();
  primeAndEdges();
  notesAndReplacement();
  blockedAndDiscarded();
  ageAndWrap();
  std::cout << "key click: waveform and input tests passed\n";
}
