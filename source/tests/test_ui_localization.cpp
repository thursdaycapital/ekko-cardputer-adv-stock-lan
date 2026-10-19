#include "UiLocalization.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
  const char* profile = "default";
  assert(std::strcmp(native_ui::profileLabel(profile), "默认配置") == 0);
  assert(std::strcmp(profile, "default") == 0);  // The wire/saved name stays intact.
  assert(std::strcmp(native_ui::profileLabel("team-alpha"), "team-alpha") == 0);
  assert(std::strcmp(native_ui::profileLabel("我的配置"), "我的配置") == 0);
  assert(std::strcmp(native_ui::interactionLabel("idle"), "空闲") == 0);
  assert(std::strcmp(native_ui::interactionLabel("speaking"), "正在播放") == 0);
  assert(std::strcmp(native_ui::interactionLabel("future_status"), "处理中") == 0);
  assert(std::strcmp(native_ui::statusLabel("OFFLINE"), "连接断开") == 0);
  assert(std::strcmp(native_ui::statusLabel("READY"), "就绪") == 0);
  assert(std::strcmp(native_ui::statusLabel("192.168.4.1"), "192.168.4.1") == 0);
  struct Error { const char* original; const char* display; };
  const Error errors[] = {
    {"HTTP 401 invalid token", "登录或设备授权已失效，请重新登录"},
    {"HTTP 429", "模型额度或限流，请查看电脑端"},
    {"NO PROFILE", "请在设备网页选择配置档"},
    {"REMOTE OFFLINE", "远程设备已离线，请检查电脑端"},
    {"audio receive buffer reservation failed", "音频内存不足，请稍后重试"},
    {"adpcm download truncated or timed out", "音频或网络等待超时，请重试"},
    {"I2S write failed err=-1", "音频接口读写失败，请重试"},
    {"voice stream final send failed", "语音提交失败，请检查连接后重试"},
    {"adpcm played bytes=100, stream bytes=100", "音频播放完成"},
    {"tts_failed", "播放失败，文字回复已保留"},
  };
  for (const auto& error : errors)
    assert(std::strcmp(native_ui::knownDetailLabel(error.original), error.display) == 0);
  assert(native_ui::knownDetailLabel("future_provider_error") == nullptr);
  std::cout << "UI localization: profile values, status/fallbacks and audio/auth/network diagnostics passed\n";
}
