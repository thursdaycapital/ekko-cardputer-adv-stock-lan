#pragma once
#include <cstring>

// Display-only policy: it never reconnects, authenticates or enables input.
struct NativeChatStatusState {
  bool wifiConnected = false;
  bool loginRequired = false;
  bool reauthenticating = false;
  bool namespaceReady = false;
  bool audioTransportPaused = false;
  bool audioPlaying = false;
  bool agentConfirmed = false;
  bool agentPending = false;
  bool protocolReady = false;
  bool historyPending = false;
  bool modelReady = false;
  bool sendPending = false;
  bool remoteRunActive = false;
  bool interactionActive = false;
  const char* interactionStatus = "";
};

inline const char* nativeChatStatus(const NativeChatStatusState& state) {
  if (!state.wifiConnected) return "网络断开";
  if (state.loginRequired) return "需要登录";
  if (state.reauthenticating) return "验证登录";
  if (!state.namespaceReady) {
    if (state.audioTransportPaused)
      return state.audioPlaying ? "正在播放" : "恢复连接";
    return "连接后台";
  }
  // A planned audio TLS pause also disconnects the selection/model locally.
  if (state.audioTransportPaused && state.audioPlaying) return "正在播放";
  if (!state.agentConfirmed || state.agentPending) return "确认选择";
  if (!state.protocolReady) return "需更新";
  if (state.audioPlaying) return "正在播放";
  const char* status = state.interactionStatus ? state.interactionStatus : "";
  if (state.interactionActive &&
      (std::strcmp(status, "listening") == 0 || std::strcmp(status, "recording") == 0))
    return "正在录音";
  if (state.interactionActive && std::strcmp(status, "clearing") == 0) return "清空会话";
  if (state.historyPending) return "读取记录";
  if (!state.modelReady) return "同步会话";
  if (state.sendPending) return "发送中";
  if (state.remoteRunActive) return "正在回复";
  if (!state.interactionActive) return "就绪";
  if (std::strcmp(status, "transcribing") == 0) return "正在识别";
  if (std::strcmp(status, "speaking") == 0) return "正在播放";
  if (std::strcmp(status, "tool") == 0) return "正在执行";
  if (std::strcmp(status, "failed") == 0) return "请求失败";
  if (std::strcmp(status, "completed") == 0) return "已完成";
  if (std::strcmp(status, "aborted") == 0) return "已停止";
  return "正在回复";
}
