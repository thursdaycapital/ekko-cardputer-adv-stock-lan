#include "ChatStatus.h"
#include <cassert>
#include <iostream>

int main() {
  NativeChatStatusState state;
  const auto expect = [&state](const char* expected) {
    assert(std::strcmp(nativeChatStatus(state), expected) == 0);
  };
  expect("网络断开");
  state.audioPlaying = state.audioTransportPaused = true;
  expect("网络断开"); // Playback cannot conceal a real Wi-Fi loss.
  state.wifiConnected = true;
  expect("正在播放");
  state.loginRequired = true;
  expect("需要登录");
  state.loginRequired = false; state.reauthenticating = true;
  expect("验证登录");
  state.reauthenticating = false; state.audioPlaying = false;
  expect("恢复连接");
  state.audioTransportPaused = false;
  expect("连接后台");
  state.namespaceReady = true;
  expect("确认选择"); // No ACK is not evidence of an outdated desktop.
  state.agentConfirmed = true; state.agentPending = true;
  expect("确认选择");
  state.agentPending = false;
  expect("需更新"); // Only a matched ACK can establish protocol support.
  state.protocolReady = true;
  expect("同步会话");
  state.historyPending = true;
  expect("读取记录");
  state.historyPending = false; state.modelReady = true;
  expect("就绪");
  state.sendPending = true;
  expect("发送中");
  state.sendPending = false; state.remoteRunActive = true;
  expect("正在回复");
  state.remoteRunActive = false; state.interactionActive = true;
  for (const char* recording : {"listening", "recording"}) {
    state.interactionStatus = recording; expect("正在录音");
  }
  const char* inputs[] = {"transcribing", "speaking", "tool", "failed", "completed", "aborted", "clearing", "thinking", nullptr};
  const char* outputs[] = {"正在识别", "正在播放", "正在执行", "请求失败", "已完成", "已停止", "清空会话", "正在回复", "正在回复"};
  for (size_t i = 0; i < sizeof(inputs) / sizeof(*inputs); ++i) {
    state.interactionStatus = inputs[i]; expect(outputs[i]);
  }
  state.audioPlaying = true; state.historyPending = true;
  expect("正在播放");
  state.agentConfirmed = false; state.audioTransportPaused = true;
  expect("正在播放");
  state.namespaceReady = false; state.audioTransportPaused = false;
  expect("连接后台"); // Unplanned backend loss must remain visible.
  std::cout << "Chat status: connection, audio pause, ACK/protocol and recording cases passed\n";
}
