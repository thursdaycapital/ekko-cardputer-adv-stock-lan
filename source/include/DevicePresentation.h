#pragma once
#include "AgentSelection.h"
#include "ChatModel.h"
#include <cstddef>
#include <cstdint>

// One coherent, borrowed frame snapshot. Rendering cannot choose an Agent,
// change a session, allocate a microphone or infer state from old notices.
struct CardputerPresentation {
  const NativeAgentSelection& selection;
  const NativeChatModel& chat;
  bool conversation = false, waiting = false, exiting = false, online = false, sending = false;
  bool historyMode = false, recording = false;
  bool sendUncertain = false, sendReviewable = false;
  const char* status = "";
  const char* notice = "";
  const char* message = "";
  const char* address = "";
  const char* modeLabel = "Studio模型";
  size_t bottomOffset = 0;
  uint32_t recordingElapsedMs = 0, recordingLimitMs = 5000;

  CardputerPresentation(const NativeAgentSelection& selected, const NativeChatModel& model)
      : selection(selected), chat(model) {}
};
