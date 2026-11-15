#include "CardputerHardware.h"
#include "AgentAvatarAssets.h"
#include "RenderPolicy.h"
#include "RenderColors.h"
#include "ChatRenderer.h"
#include "DeviceHomeRenderer.h"
#include "DeviceGestureMailbox.h"
#include "EkkoPixelFont12.h"
#include "AudioStreamQueue.h"
#include "KeyClick.h"
#include "../assets/boot-video/BootVideoAssets.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>

namespace {
NativeNavigationInput navigation;
NativeChatKeyboard chatKeyboard;
DeviceGestureMailbox gestureMailbox;
bool conversationMode = false, inputModeChanged = false, chatHistoryMode = false;
bool chatSendUncertain = false;
bool bootPressed = false, voiceChordPressed = false;
NativeChatRenderMetrics chatMetrics;
uint32_t audioRate = 24000;
int16_t microphone[256];
size_t microphoneSamples = 0;
bool capturePending = false;
std::unique_ptr<mic_capture::Queue> continuousCapture;
std::atomic<bool> continuousCaptureActive{false};
uint32_t continuousCaptureRate=0;
CardputerCaptureStats lastCaptureStats;
void captureBufferReleased(void* context,void* data,size_t samples) {
  auto* queue=static_cast<mic_capture::Queue*>(context);
  if(!continuousCaptureActive.load(std::memory_order_acquire)) return;
  if(!queue->completeExactPointer(static_cast<const int16_t*>(data),samples)) return;
  const auto next=queue->claim();
  if(next && continuousCaptureActive.load(std::memory_order_acquire) &&
      !M5Cardputer.Mic.record(next.data,next.samples,continuousCaptureRate,true) &&
      continuousCaptureActive.load(std::memory_order_acquire) && !queue->stopped())
    queue->failSdkSubmission(); // Intentional concurrent stop is not a failed take.
}
AudioStreamQueue audioQueue;
uint32_t audioSourceEmpty = 0, audioMaxSubmitGapUs = 0, audioWriteTimeouts = 0;
uint32_t audioLastStreamEmpty = 0, audioLastStreamMaxGapUs = 0, audioLastStreamQueued = 0;
uint32_t audioLastSubmitUs = 0;
bool audioStreamStarted = false;
native_key_click::Input keyClickInput;
std::array<int16_t, native_key_click::kSampleCount> keyClickBuffer{};
bool keyClickEnabled = true, keyClickBlocked = true, keyClickPlaying = false;
bool keyClickSpeakerOwned = false, keyClickWarming = false;
std::atomic<bool> keyClickBufferFree{true};
std::atomic<uint32_t> keyClickBufferReleases{0};
uint32_t keyClickSpeakerStarts = 0, keyClickWarmups = 0;
uint32_t keyClicksPlayed = 0, keyClicksCompleted = 0, keyClicksPreempted = 0, keyClickFailures = 0;
void audioBufferReleased(void*, const void* data, uint8_t channel) {
  if (channel == 0) audioQueue.releaseExactPointer(data);
  else if (channel == 1 && data == keyClickBuffer.data()) {
    keyClickBufferReleases.fetch_add(1, std::memory_order_relaxed);
    keyClickBufferFree.store(true, std::memory_order_release);
  }
}
void stopAudioAndJoin() {
  M5Cardputer.Speaker.end();
  keyClickBufferFree.store(true, std::memory_order_release);
  keyClickSpeakerOwned = keyClickWarming = false;
  if (keyClickPlaying) { keyClickPlaying = false; ++keyClicksPreempted; }
  audioQueue.resetAfterJoin();
  audioStreamStarted = false;
}
bool beginSpeakerOutput(uint8_t volume) {
  // ADV input/output share codec clocks. Acquire them once per audio owner.
  if (!cardputerCaptureEnd()) return false;
  if (M5Cardputer.Mic.isRunning()) M5Cardputer.Mic.end();
  capturePending = false;
  M5Cardputer.Speaker.setVolume(volume);
  return M5Cardputer.Speaker.begin();
}
constexpr uint16_t ink = 0x18E5, paper = 0xEF3A, amber = 0xECA6, muted = 0x94D1;
NativeRenderPolicy renderPolicy;
CardputerRenderStats renderStats;
NativeBootVideoStats bootVideoStats;
bool renderAttempted = false, failureNoticeShown = false;
M5Canvas& renderCanvas() {
  // Construct lazily, after the M5 globals and physical board are initialized.
  static M5Canvas canvas;
  return canvas;
}
void sampleRenderMemory() {
  constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  renderStats.free_heap = heap_caps_get_free_size(internal);
  renderStats.min_free_heap = heap_caps_get_minimum_free_size(internal);
  renderStats.dma_free_heap = heap_caps_get_free_size(MALLOC_CAP_DMA);
  renderStats.largest_dma_block = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
}
void showStableRenderFailure(const char* label) {
  if (failureNoticeShown) return;
  failureNoticeShown = true;
  // A single small warning preserves the last image. Never fall back to the
  // old repeating clear/full-screen/direct-draw path when allocation fails.
  auto& display = M5Cardputer.Display;
  display.fillRect(0, 112, 240, 23, ink);
  display.setTextColor(amber, ink);
  display.setFont(&ekko_pixel_font::kFont);
  display.setTextSize(1);
  display.drawString(label, 8, 115);
  ++renderStats.fallback_notices;
}
bool ensureRenderCanvas() {
  if (renderAttempted) return renderStats.ready;
  renderAttempted = true;
  sampleRenderMemory();
  if (!nativeSpriteMemoryAvailable(renderStats.free_heap, renderStats.largest_dma_block)) {
    ++renderStats.allocation_failures;
    renderStats.last_error = "insufficient_internal_ram";
    showStableRenderFailure("显示内存不足");
    return false;
  }
  auto& canvas = renderCanvas();
  canvas.setPsram(false);
  canvas.setColorDepth(16);
  ++renderStats.allocation_attempts;
  if (!canvas.createSprite(kNativeFrameWidth, kNativeFrameHeight)) {
    ++renderStats.allocation_failures;
    renderStats.last_error = "sprite_allocation_failed";
    showStableRenderFailure("显示内存不足");
    return false;
  }
  renderStats.ready = true;
  renderStats.color_depth = 16;
  renderStats.buffer_bytes = heap_caps_get_allocated_size(canvas.getBuffer());
  sampleRenderMemory();
  return true;
}
}

bool cardputerBegin() {
  auto cfg = M5.config();
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(140);
  Preferences factoryPreferences;
  if (factoryPreferences.begin("cardputer", true)) {
    keyClickEnabled = !factoryPreferences.getBool("quiet_mode", false);
    factoryPreferences.end();
  }
  ensureRenderCanvas();
  return M5.getBoard() == m5::board_t::board_M5CardputerADV;
}

NativeBootVideoStats cardputerBootVideoStats() { return bootVideoStats; }

void cardputerPlayBootVideo() {
  if (bootVideoStats.attempted) return;
  if (!ensureRenderCanvas()) {
    bootVideoStats.attempted = true;
    bootVideoStats.last_error = "display_not_ready";
    inputModeChanged = true;
    cardputerPoll();
    return;
  }
  NativeBootVideoAsset asset;
  asset.data = native_boot_video::kJpegData;
  asset.bytes = native_boot_video::kDataBytes;
  asset.offsets = native_boot_video::kFrameOffsets;
  asset.offsetCount = sizeof(native_boot_video::kFrameOffsets) / sizeof(native_boot_video::kFrameOffsets[0]);
  asset.frames = native_boot_video::kFrameCount;
  asset.width = native_boot_video::kWidth;
  asset.height = native_boot_video::kHeight;
  asset.durationMs = native_boot_video::kDurationMs;
  auto& frame = renderCanvas();
  uint32_t maxDecodeUs = 0;
  bootVideoStats = nativePlayBootVideo(asset,
      []() { return uint32_t(millis()); },
      [](uint32_t ms) { delay(ms); },
      [&frame, &maxDecodeUs](const uint8_t* jpeg, size_t bytes) {
        M5Cardputer.Display.waitDMA();
        frame.fillScreen(ink); // RAM only; never submit a partly decoded frame.
        const uint32_t started = micros();
        const bool decoded = frame.drawJpg(jpeg, bytes, 0, 0, 240, 135);
        maxDecodeUs = std::max(maxDecodeUs, uint32_t(micros() - started));
        if (!decoded) return false;
        frame.pushSprite(&M5Cardputer.Display, 0, 0);
        M5Cardputer.Display.waitDMA();
        ++renderStats.frame_count; // A completed LCD transfer, including boot.
        sampleRenderMemory();
        return true;
      },
      []() {
        // Raw sampling only: skip must not become a queued OK/chat action.
        M5Cardputer.update();
        return M5Cardputer.BtnA.isPressed() || M5Cardputer.Keyboard.isPressed() != 0;
      });
  bootVideoStats.max_decode_us = maxDecodeUs;
  M5Cardputer.Display.waitDMA();
  inputModeChanged = true;
  cardputerPoll(); // Prime both held-key paths before normal UI starts.
  sampleRenderMemory();
}

void cardputerPoll() {
  M5Cardputer.update();
  NativeNavigationKeys keys = nativeNavigationKeys([](char key) {
    return M5Cardputer.Keyboard.isKeyPressed(key);
  }, M5Cardputer.Keyboard.keysState().enter);
  auto& state = M5Cardputer.Keyboard.keysState();
  keys.mode = state.tab && !state.fn && !state.ctrl && !state.alt && !state.opt;
  bootPressed = M5Cardputer.BtnA.isPressed();
  voiceChordPressed = state.fn && state.space;
  gestureMailbox.update(millis(), bootPressed, voiceChordPressed);
  keys = nativeNavigationForPage(keys, conversationMode, state.fn);
  if (inputModeChanged) navigation.prime(keys); else navigation.update(keys);
  std::array<NativeChatPhysicalKey, 56> physical{};
  for (const auto& point : M5Cardputer.Keyboard.keyList()) {
    if (point.x < 0 || point.x >= 14 || point.y < 0 || point.y >= 4) continue;
    auto& key = physical[point.y * 14 + point.x];
    key.down = true;
    key.base = static_cast<uint8_t>(M5Cardputer.Keyboard.getKeyValue(point).value_first);
    key.character = M5Cardputer.Keyboard.getKey(point);
  }
  const bool voiceHeld = bootPressed || voiceChordPressed;
  keyClickInput.update(physical, millis(), []() { return esp_random(); },
      keyClickEnabled && !keyClickBlocked && !voiceHeld && !audioStreamStarted &&
      !capturePending && !continuousCapture && !M5Cardputer.Mic.isRecording());
  NativeChatModifiers mods;
  mods.fn = state.fn; mods.shift = state.shift; mods.ctrl = state.ctrl; mods.alt = state.alt || state.opt;
  chatKeyboard.update(physical, mods, conversationMode, millis(), inputModeChanged, chatHistoryMode);
  inputModeChanged = false;
}
void cardputerSetConversationMode(bool enabled) {
  if (conversationMode != enabled) { conversationMode = enabled; inputModeChanged = true; }
}
void cardputerSetChatHistoryMode(bool enabled) { chatHistoryMode = enabled; }
bool cardputerChatKey(NativeChatKey& key) { return chatKeyboard.pop(key); }
uint32_t cardputerChatKeyCount() { return chatKeyboard.pressed(); }
uint32_t cardputerChatKeyDropped() { return chatKeyboard.dropped(); }
size_t cardputerChatScrollLimit() { return chatMetrics.maxBottomOffset; }
CardputerChatViewport cardputerChatViewport() {
  CardputerChatViewport result;
  result.first = chatMetrics.firstVisibleMessage; result.last = chatMetrics.lastVisibleMessage;
  result.maxOffset = chatMetrics.maxBottomOffset;
  result.topWindowBoundary = chatMetrics.topWindowBoundary;
  result.bottomWindowBoundary = chatMetrics.bottomWindowBoundary;
  return result;
}
size_t cardputerChatContentLines(const NativeChatModel& chat) {
  if (!renderStats.ready) return 0;
  auto& frame = renderCanvas(); frame.setFont(&ekko_pixel_font::kFont);
  size_t total = chat.context() && chat.context()->history_has_more ? 1 : 0;
  if (chat.context() && chat.context()->history_has_newer) ++total;
  for (size_t i = 0; i < chat.messageCount(); ++i)
    total += native_chat_render::messageLines(frame, *chat.messageAt(i));
  return total;
}
size_t cardputerChatMaxScroll(const NativeChatModel& chat, bool hasNotice) {
  const size_t total = cardputerChatContentLines(chat);
  const size_t visible = nativeChatVisibleLines(chatHistoryMode, hasNotice, chat.inputMethod() && chat.inputMethod()->composing(), chatSendUncertain);
  return total > visible ? total - visible : 0;
}
size_t cardputerChatMessageScroll(const NativeChatModel& chat, const char* id, bool atEnd, bool hasNotice) {
  if (!renderStats.ready || !id) return 0;
  auto& frame = renderCanvas(); frame.setFont(&ekko_pixel_font::kFont);
  size_t total = chat.context() && chat.context()->history_has_more ? 1 : 0;
  size_t targetStart = 0, targetEnd = 0;
  for (size_t i = 0; i < chat.messageCount(); ++i) {
    const auto* message = chat.messageAt(i);
    const size_t lines = native_chat_render::messageLines(frame, *message);
    if (strcmp(message->id, id) == 0) { targetStart = total; targetEnd = total + lines; }
    total += lines;
  }
  if (chat.context() && chat.context()->history_has_newer) ++total;
  const size_t visible = nativeChatVisibleLines(chatHistoryMode, hasNotice, chat.inputMethod() && chat.inputMethod()->composing(), chatSendUncertain);
  const size_t maximum = total > visible ? total - visible : 0;
  const size_t first = atEnd ? (targetEnd > visible ? targetEnd - visible : 0) : targetStart;
  return maximum > first ? maximum - first : 0;
}
bool cardputerBootPressed() { return bootPressed; }
bool cardputerVoiceChordPressed() { return voiceChordPressed; }
bool cardputerTalkPressed() { return bootPressed || voiceChordPressed; }
uint8_t cardputerGesture() { return gestureMailbox.pop(); }
void cardputerSuppressGestureUntilReleased() { gestureMailbox.suppressUntilReleased(); }
int cardputerNavigation() { return navigation.pop(); }
uint32_t cardputerNavigationCount() { return navigation.count(); }
NativeNavigationStats cardputerNavigationStats() { return navigation.stats(); }
bool cardputerNavigationPending() { return navigation.pending(); }
bool cardputerNavigationInterruptPending() { return navigation.interruptPending(); }
bool cardputerRecordingInterruptPending() { return navigation.interruptPending() || chatKeyboard.pending(); }
void cardputerDisplayPower(bool on) { M5Cardputer.Display.setBrightness(on ? 140 : 0); }

CardputerRenderStats cardputerRenderStats() {
  sampleRenderMemory();
  return renderStats;
}

void cardputerDraw(const CardputerPresentation& view) {
  chatSendUncertain = view.sendUncertain;
  ++renderStats.draw_calls;
  if (!ensureRenderCanvas()) return;
  const auto& selection = view.selection;
  const auto& chat = view.chat;
  if (selection.active >= kNativeAgentCount || selection.browse >= kNativeAgentCount) {
    renderStats.last_error = "invalid_agent_index";
    showStableRenderFailure("显示状态异常");
    return;
  }
  NativeRenderInput input;
  input.conversation = view.conversation;
  input.online = view.online;
  input.active = selection.active; input.browse = selection.browse;
  input.message = view.message; input.address = view.address;
  input.mode_label = view.modeLabel;
  input.hint = view.waiting ? "opening" : "";
  input.chat_status = view.status; input.chat_notice = view.notice;
  input.recording = view.recording;
  input.send_uncertain = view.sendUncertain; input.send_reviewable = view.sendReviewable;
  input.recording_elapsed_tenths = view.recording ? view.recordingElapsedMs / 100 : 0;
  input.recording_limit_ms = view.recordingLimitMs;
  if (view.conversation) {
    input.chat_revision = chat.revision(); input.editor_revision = chat.draft() ? chat.draft()->revision() : 0;
    input.chat_scroll = view.bottomOffset;
    input.chat_history_mode = view.historyMode;
    input.input_method_revision = chat.inputMethod() ? chat.inputMethod()->revision() : 0;
  }
  const uint32_t now = millis();
  const auto decision = renderPolicy.consider(input, now);
  if (decision == NativeRenderDecision::SameState) { ++renderStats.skipped_same_state; return; }
  if (decision == NativeRenderDecision::Throttled) { ++renderStats.skipped_throttled; return; }
  const uint32_t started = micros();
  auto& frame = renderCanvas();
  // A single unchanged sprite owns the entire LCD transfer. Never clear the
  // physical panel while painting the next frame in RAM.
  M5Cardputer.Display.waitDMA();
  if (view.conversation) {
    NativeChatRenderOptions options;
    options.agentIndex = selection.active; options.status = view.status; options.notice = view.notice;
    options.bottomOffsetLines = std::min<size_t>(view.bottomOffset, UINT16_MAX);
    options.online = view.online; options.sending = view.sending; options.historyMode = view.historyMode;
    options.recording = view.recording; options.recordingElapsedMs = view.recordingElapsedMs;
    options.sendUncertain = view.sendUncertain; options.sendReviewable = view.sendReviewable;
    options.recordingLimitMs = view.recordingLimitMs;
    chatMetrics = nativeDrawChat(frame, chat, options);
  } else {
    const auto* avatar = nativeAgentLogo(selection.browse);
    if (!avatar || !avatar->pixels || avatar->width != 44 || avatar->height != 44)
      renderStats.last_error = "avatar_asset_missing";
    drawDeviceHome(frame, view);
  }
  frame.pushSprite(&M5Cardputer.Display, 0, 0);
  M5Cardputer.Display.waitDMA();
  ++renderStats.frame_count;
  renderStats.render_us_last = uint32_t(micros() - started);
  renderStats.render_us_max = std::max(renderStats.render_us_max, renderStats.render_us_last);
  renderPolicy.commit(input, now);
  sampleRenderMemory();
}

bool cardputerAudioBegin() {
  stopAudioAndJoin();
  M5Cardputer.Speaker.setBufferReleaseCallback(nullptr, audioBufferReleased);
  capturePending = false;
  return M5.getBoard() == m5::board_t::board_M5CardputerADV &&
         M5Cardputer.Mic.isEnabled() && M5Cardputer.Speaker.isEnabled() &&
         M5Cardputer.Mic.begin();
}
bool cardputerSpeakerEnable(bool enable) {
  if (!enable) {
    stopAudioAndJoin();
    return true;
  }
  if (keyClickSpeakerOwned) stopAudioAndJoin();
  keyClickInput.discard();
  return beginSpeakerOutput(180); // Native PCM shaping applies UI volume.
}
bool cardputerAudioRate(uint32_t rate) {
  if (rate < 8000 || rate > 96000) return false;
  audioRate = rate; return true;
}
void cardputerAudioClear() {
  // A released playing bit does not guarantee the task has stopped using its
  // source pointer. Join before resetting queue ownership or switching clocks.
  cardputerCaptureEnd();
  stopAudioAndJoin();
  keyClickInput.discard();
  if (capturePending) { M5Cardputer.Mic.end(); capturePending = false; }
}
bool cardputerAudioDrain(TickType_t timeout) {
  const TickType_t start = xTaskGetTickCount();
  while (audioQueue.stats().in_flight || M5Cardputer.Speaker.isPlaying(0)) {
    if (xTaskGetTickCount() - start >= timeout) {
      ++audioWriteTimeouts;
      stopAudioAndJoin();
      return false;
    }
    cardputerPoll(); vTaskDelay(1);
  }
  return true;
}
CardputerAudioStats cardputerAudioStats() {
  const auto queue = audioQueue.stats();
  CardputerAudioStats result;
  result.queued = queue.queued; result.released = queue.released;
  result.in_flight = queue.in_flight; result.claim_failures = queue.claim_failures;
  result.cancelled_unqueued = queue.cancelled_unqueued; result.rejected_releases = queue.rejected_releases;
  result.discarded_after_join = queue.discarded_after_join;
  result.source_empty_before_submit = audioSourceEmpty;
  result.max_submit_gap_us = audioMaxSubmitGapUs; result.write_timeouts = audioWriteTimeouts;
  result.buffer_bytes = queue.buffer_bytes;
  result.last_stream_source_empty = audioLastStreamEmpty;
  result.last_stream_max_gap_us = audioLastStreamMaxGapUs;
  result.last_stream_queued = audioLastStreamQueued;
  return result;
}
CardputerCaptureStats cardputerCaptureStats() {
  if(!continuousCapture) return lastCaptureStats;
  CardputerCaptureStats result;
  result.active=continuousCaptureActive.load(std::memory_order_acquire);
  result.failed=continuousCapture->failed();result.error=continuousCapture->error();
  result.queue=continuousCapture->stats();result.sampleRate=continuousCaptureRate;
  result.bufferBytes=mic_capture::kPcmBytes;
  result.storageRetained=!result.active;
  return result;
}

void cardputerCaptureStop() {
  if(!continuousCapture) return;
  // Never hold an application lock while Mic.end() waits for its callback.
  // A pending request dropped by end() is NOT a completed recording block.
  continuousCaptureActive.store(false,std::memory_order_release);
  continuousCapture->requestStop();
  M5Cardputer.Mic.end();
  M5Cardputer.Mic.setBufferReleaseCallback(nullptr,nullptr);
}

bool cardputerCaptureEnd() {
  if(!continuousCapture) return true;
  cardputerCaptureStop();
  lastCaptureStats=cardputerCaptureStats();
  if(!continuousCapture->resetAfterJoin()) return false;
  lastCaptureStats.queue=continuousCapture->stats();
  lastCaptureStats.storageRetained=false;lastCaptureStats.bufferBytes=0;
  continuousCapture.reset();return true;
}

bool cardputerCaptureBegin(uint32_t sampleRate) {
  if(sampleRate<8000 || sampleRate>96000 || !cardputerCaptureEnd()) return false;
  cardputerAudioClear();M5Cardputer.Mic.end();
  continuousCapture.reset(new(std::nothrow)mic_capture::Queue);
  if(!continuousCapture) return false;
  continuousCaptureRate=sampleRate;audioRate=sampleRate;capturePending=false;
  const auto initial=continuousCapture->claim();
  if(!initial) {cardputerCaptureEnd();return false;}
  // All application allocation and setup precede the first request. Once it
  // starts, the official release callback alone submits the next same-rate
  // request before the SDK checks for an empty queue, preserving source carry.
  M5Cardputer.Mic.setBufferReleaseCallback(continuousCapture.get(),captureBufferReleased);
  continuousCaptureActive.store(true,std::memory_order_release);
  if(!M5Cardputer.Mic.record(initial.data,initial.samples,sampleRate,true)) {
    continuousCapture->failSdkSubmission();cardputerCaptureEnd();return false;
  }
  return true;
}

mic_capture::Lease cardputerCaptureTake() {
  return continuousCapture?continuousCapture->take():mic_capture::Lease{};
}
CardputerKeyClickStats cardputerKeyClickStats() {
  const auto input = keyClickInput.stats();
  CardputerKeyClickStats result;
  result.enabled = keyClickEnabled; result.playing = keyClickPlaying;
  result.pressed = input.pressed; result.suppressed = input.suppressed;
  result.played = keyClicksPlayed; result.completed = keyClicksCompleted;
  result.preempted = keyClicksPreempted; result.failures = keyClickFailures;
  result.buffer_bytes = sizeof(keyClickBuffer);
  result.speaker_owned = keyClickSpeakerOwned; result.warming = keyClickWarming;
  result.buffer_free = keyClickBufferFree.load(std::memory_order_acquire);
  result.speaker_starts = keyClickSpeakerStarts; result.warmups = keyClickWarmups;
  result.buffer_releases = keyClickBufferReleases.load(std::memory_order_relaxed);
  return result;
}
void cardputerTickKeyClick(bool allowed) {
  keyClickBlocked = !allowed;
  const bool available = allowed && keyClickEnabled && !capturePending && !continuousCapture &&
      !M5Cardputer.Mic.isRecording() && !audioStreamStarted && !audioQueue.stats().in_flight;
  if (!available) {
    keyClickInput.discard();
    if (keyClickSpeakerOwned) stopAudioAndJoin();
    return;
  }
  if (!keyClickSpeakerOwned) {
    // Match the factory's persistent output clock. Reopening the codec for
    // every 44 ms key sound introduces reset transients and loses its onset.
    if (M5Cardputer.Speaker.isRunning()) return; // Another audio owner has priority.
    if (!beginSpeakerOutput(90)) { ++keyClickFailures; return; }
    keyClickSpeakerOwned = true; ++keyClickSpeakerStarts;
    keyClickBuffer.fill(0);
    keyClickBufferFree.store(false, std::memory_order_release);
    M5Cardputer.Speaker.setVolume(90);
    if (!M5Cardputer.Speaker.playRaw(keyClickBuffer.data(), keyClickBuffer.size(),
            native_key_click::kPlaybackRate, false, 1, 1)) {
      ++keyClickFailures; stopAudioAndJoin(); return;
    }
    keyClickWarming = true; ++keyClickWarmups;
    return;
  }
  if (keyClickWarming || keyClickPlaying) {
    // Exact-pointer release guarantees that the SDK no longer reads this
    // source. Its already mixed DMA tail keeps playing on the resident clock.
    if (!keyClickBufferFree.load(std::memory_order_acquire) ||
        M5Cardputer.Speaker.isPlaying(1)) return;
    if (keyClickPlaying) ++keyClicksCompleted;
    keyClickPlaying = keyClickWarming = false;
  }
  native_key_click::Request request;
  if (!keyClickInput.take(millis(), available, request)) return;
  native_key_click::render(request.noteIndex, keyClickBuffer.data());
  keyClickBufferFree.store(false, std::memory_order_release);
  M5Cardputer.Speaker.setVolume(90); // Same UI feedback gain as the factory menu.
  if (!M5Cardputer.Speaker.playRaw(keyClickBuffer.data(), keyClickBuffer.size(),
          native_key_click::kPlaybackRate, false, 1, 1)) {
    ++keyClickFailures;
    stopAudioAndJoin();
    return;
  }
  keyClickPlaying = true; ++keyClicksPlayed;
}
esp_err_t cardputerAudioRead(void* data, size_t length, size_t* read, TickType_t timeout) {
  if (!read || !data || length < 4) return ESP_ERR_INVALID_ARG;
  *read = 0;
  if (continuousCapture) return ESP_ERR_INVALID_STATE;
  keyClickInput.discard();
  if (M5Cardputer.Speaker.isRunning()) cardputerSpeakerEnable(false);
  if (!capturePending) {
    microphoneSamples = std::min(length / sizeof(int16_t), sizeof(microphone) / sizeof(microphone[0]));
    microphoneSamples &= ~size_t(1);
    if (!M5Cardputer.Mic.record(microphone, microphoneSamples, audioRate, true)) return ESP_FAIL;
    capturePending = true;
  }
  const TickType_t start = xTaskGetTickCount();
  while (M5Cardputer.Mic.isRecording()) {
    if (timeout == 0 || xTaskGetTickCount() - start >= timeout) return ESP_OK;
    vTaskDelay(1);
  }
  const size_t bytes = std::min(length, microphoneSamples * sizeof(int16_t));
  memcpy(data, microphone, bytes);
  *read = bytes; capturePending = false;
  return ESP_OK;
}
esp_err_t cardputerAudioWrite(const void* data, size_t length, size_t* written, TickType_t timeout) {
  if (!written || !data || length % 4) return ESP_ERR_INVALID_ARG;
  *written = 0;
  if (!length) return ESP_OK;
  keyClickInput.discard();
  if (keyClickSpeakerOwned) stopAudioAndJoin();
  if (!M5Cardputer.Speaker.isRunning() && !cardputerSpeakerEnable(true)) return ESP_FAIL;
  const TickType_t start = xTaskGetTickCount();
  while (*written < length) {
    AudioStreamClaim claim;
    // Keep the currently playing source queued while the producer fills the
    // next owned slot. Only wait for capacity; never drain between PCM chunks.
    while (M5Cardputer.Speaker.isPlaying(0) >= 2 || !audioQueue.tryClaim(claim)) {
      if (xTaskGetTickCount() - start >= timeout) {
        ++audioWriteTimeouts;
        stopAudioAndJoin();
        return ESP_ERR_TIMEOUT;
      }
      cardputerPoll(); vTaskDelay(1);
    }
    const size_t bytes = std::min(length - *written, claim.capacity_bytes);
    memcpy(claim.data, static_cast<const uint8_t*>(data) + *written, bytes);
    if (!audioQueue.submit(claim, bytes)) {
      audioQueue.abandon(claim); return ESP_FAIL;
    }
    const uint32_t now = micros();
    if (!audioStreamStarted) {
      audioLastStreamEmpty = audioLastStreamMaxGapUs = audioLastStreamQueued = 0;
    }
    if (audioStreamStarted) {
      const uint32_t gap = now - audioLastSubmitUs;
      audioMaxSubmitGapUs = std::max(audioMaxSubmitGapUs, gap);
      audioLastStreamMaxGapUs = std::max(audioLastStreamMaxGapUs, gap);
      // Diagnostic source-slot state only: DMA may still hold audible samples.
      if (M5Cardputer.Speaker.isPlaying(0) == 0) { ++audioSourceEmpty; ++audioLastStreamEmpty; }
    }
    if (!M5Cardputer.Speaker.playRaw(claim.data, bytes / sizeof(int16_t), audioRate, true, 1, 0)) {
      audioQueue.cancelUnqueued(claim); return ESP_FAIL;
    }
    audioLastSubmitUs = now; audioStreamStarted = true;
    ++audioLastStreamQueued;
    *written += bytes;
  }
  return ESP_OK;
}
