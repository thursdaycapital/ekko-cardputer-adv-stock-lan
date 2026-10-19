#pragma once
#include <M5Cardputer.h>
#include <esp_err.h>
#include "AgentSelection.h"
#include "NavigationInput.h"
#include "ConversationView.h"
#include "ChatKeyboard.h"
#include "ChatModel.h"
#include "BootVideoPlayer.h"
#include "MicCaptureQueue.h"

bool cardputerBegin();
void cardputerPlayBootVideo();
NativeBootVideoStats cardputerBootVideoStats();
void cardputerPoll();
void cardputerSetConversationMode(bool enabled);
void cardputerSetChatHistoryMode(bool enabled);
bool cardputerChatKey(NativeChatKey& key);
uint32_t cardputerChatKeyCount();
uint32_t cardputerChatKeyDropped();
size_t cardputerChatScrollLimit();
struct CardputerChatViewport {
  size_t first = 0, last = 0, maxOffset = 0;
  bool topWindowBoundary = false, bottomWindowBoundary = false;
};
CardputerChatViewport cardputerChatViewport();
size_t cardputerChatMessageScroll(const NativeChatModel& chat, const char* id, bool atEnd, bool hasNotice);
size_t cardputerChatContentLines(const NativeChatModel& chat);
size_t cardputerChatMaxScroll(const NativeChatModel& chat, bool hasNotice);
bool cardputerTalkPressed();
int cardputerNavigation(); // -1 / +1 browse, 2 confirm; zero means no event
uint32_t cardputerNavigationCount();
NativeNavigationStats cardputerNavigationStats();
bool cardputerNavigationPending();
bool cardputerNavigationInterruptPending();
bool cardputerRecordingInterruptPending();
void cardputerDisplayPower(bool on);
struct CardputerRenderStats {
  bool ready = false;
  uint8_t color_depth = 0;
  uint32_t frame_count = 0;
  uint32_t skipped_same_state = 0;
  uint32_t skipped_throttled = 0;
  uint32_t draw_calls = 0;
  uint32_t render_us_last = 0;
  uint32_t render_us_max = 0;
  uint32_t buffer_bytes = 0;
  uint32_t allocation_attempts = 0;
  uint32_t allocation_failures = 0;
  uint32_t fallback_notices = 0;
  uint32_t free_heap = 0;
  uint32_t min_free_heap = 0;
  uint32_t dma_free_heap = 0;
  uint32_t largest_dma_block = 0;
  const char* last_error = "none";
};
// Counters count actual completed sprite submissions, not draw() invocations.
CardputerRenderStats cardputerRenderStats();
void cardputerDraw(const uint8_t* monochrome, bool interaction,
                   const NativeAgentSelection& selection, const NativeConversationView& view, const String& title,
                   const String& hint, const String& address, bool socketReady,
                   const String& selectionError, const NativeChatModel& chat,
                   const char* chatStatus, const String& chatNotice, size_t bottomOffset,
                   bool sending, bool historyMode, uint32_t chatRevision);
bool cardputerAudioBegin();
bool cardputerSpeakerEnable(bool enable);
bool cardputerAudioRate(uint32_t rate);
void cardputerAudioClear();
bool cardputerAudioDrain(TickType_t timeout);
struct CardputerAudioStats {
  uint32_t queued = 0, released = 0, in_flight = 0, claim_failures = 0;
  uint32_t cancelled_unqueued = 0, rejected_releases = 0, discarded_after_join = 0;
  uint32_t source_empty_before_submit = 0, max_submit_gap_us = 0, write_timeouts = 0;
  uint32_t buffer_bytes = 0;
  uint32_t last_stream_source_empty = 0, last_stream_max_gap_us = 0;
  uint32_t last_stream_queued = 0;
};
CardputerAudioStats cardputerAudioStats();
struct CardputerCaptureStats {
  bool active=false,failed=false,storageRetained=false;
  uint32_t sampleRate=0,bufferBytes=0;
  mic_capture::Error error=mic_capture::Error::None;
  mic_capture::Stats queue;
};
// Main owns begin/stop/end. Only the SDK completion callback continues record()
// at the same rate, using preallocated slots. No callback does UI or file IO.
bool cardputerCaptureBegin(uint32_t sampleRate);
mic_capture::Lease cardputerCaptureTake();
void cardputerCaptureStop(); // Join Mic; keep completed Ready blocks to drain.
bool cardputerCaptureEnd(); // Join then release storage; false keeps a live Lease safe.
CardputerCaptureStats cardputerCaptureStats();
struct CardputerKeyClickStats {
  bool enabled = false, playing = false;
  bool speaker_owned = false, warming = false, buffer_free = true;
  uint32_t pressed = 0, suppressed = 0, played = 0, completed = 0, preempted = 0, failures = 0;
  uint32_t speaker_starts = 0, warmups = 0, buffer_releases = 0;
  uint32_t buffer_bytes = 0;
};
// Only the outer application loop may service UI audio; poll() also runs inside
// microphone capture and TTS waits and must never acquire the shared codec.
void cardputerTickKeyClick(bool allowed);
CardputerKeyClickStats cardputerKeyClickStats();
esp_err_t cardputerAudioRead(void* data, size_t length, size_t* read, TickType_t timeout);
esp_err_t cardputerAudioWrite(const void* data, size_t length, size_t* written, TickType_t timeout);
