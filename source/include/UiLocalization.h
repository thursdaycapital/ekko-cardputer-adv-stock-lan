#pragma once
#include <cstring>
#include <string>

// Display-only labels. Wire values, saved Profile names and diagnostics remain
// unchanged; callers use these only when drawing or building a human UI.
namespace native_ui {
inline const char* profileLabel(const char* profile) {
  return profile && std::strcmp(profile, "default") == 0 ? "默认配置" : (profile ? profile : "");
}

inline const char* interactionLabel(const char* status) {
  if (!status || !*status || !std::strcmp(status, "idle")) return "空闲";
  struct Label { const char* value; const char* label; };
  static const Label labels[] = {
    {"transcribing", "正在识别"}, {"thinking", "正在思考"}, {"clearing", "清空会话"},
    {"tool", "正在执行"}, {"speaking", "正在播放"}, {"completed", "已完成"},
    {"failed", "请求失败"}, {"aborted", "已停止"}, {"listening", "正在监听"},
    {"recording", "正在录音"}, {"queued", "等待处理"}, {"pending", "等待处理"},
    {"ready", "就绪"}, {"connected", "已连接"}, {"disconnected", "连接断开"},
  };
  for (const auto& label : labels) if (!std::strcmp(status, label.value)) return label.label;
  return "处理中";
}

inline const char* statusLabel(const char* value) {
  struct Label { const char* value; const char* label; };
  static const Label labels[] = {
    {"BOOT", "启动中"}, {"WIFI ONLY", "正在初始化网络"}, {"WIFI", "无线网络"},
    {"BUSY", "忙碌中"}, {"AUDIO", "音频"}, {"INPUT OFF", "麦克风未就绪"},
    {"VOICE", "语音"}, {"MEMORY", "内存不足"}, {"LISTEN", "正在监听"},
    {"SAY NOW", "请说话"}, {"I2S", "音频接口"}, {"READ FAIL", "读取失败"}, {"DONE", "已完成"}, {"START", "开始执行"},
    {"WAIT I2S", "等待麦克风"}, {"RECORDING", "正在录音"}, {"MIC", "麦克风"},
    {"NO DATA", "未收到声音"}, {"SCAN", "扫描中"}, {"LOGIN", "登录"},
    {"MCU", "设备"}, {"OPEN", "请打开设备网页"}, {"OK", "成功"},
    {"REMOTE", "远程连接"}, {"VERIFY", "正在验证"}, {"FAILED", "失败"},
    {"RELOGIN", "请重新登录"}, {"REAUTH", "验证登录"}, {"RETRY", "正在重试"},
    {"OPEN /device", "请打开设备网页"}, {"PLAY", "正在播放"}, {"REC", "录音回放"},
    {"FAIL", "失败"}, {"ERROR", "发生错误"}, {"DETAIL", "请查看错误提示"},
    {"OFFLINE", "连接断开"}, {"AUTO REC", "自动录音"}, {"PRESS OK", "请按 OK"},
    {"OPEN CHAT", "打开对话"}, {"AGENT", "智能体"}, {"CONFIRM IN STUDIO", "请在电脑端确认"},
    {"REQUIRED", "需要登录"}, {"SOCKET", "后台连接"}, {"REPLACED", "连接已被接管"},
    {"CONNECT", "正在连接"}, {"SHARE SAVE ERROR", "共享网络保存失败"},
    {"RESTART", "即将重启"}, {"SETUP", "配置网络"}, {"FORGET", "清除网络历史"},
    {"AP START", "启动配网热点"}, {"AP ERR", "热点错误"}, {"OPEN WIFI AP", "请连接配网热点"},
    {"AP FAILED", "热点启动失败"}, {"READY", "就绪"}, {"RECONNECT", "正在重连"},
    {"LOST", "网络断开"}, {"IP", "网络地址"},
  };
  for (const auto& label : labels) if (!std::strcmp(value, label.value)) return label.label;
  return value;
}

inline const char* knownDetailLabel(const char* raw) {
  std::string value(raw ? raw : "");
  for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  const auto has = [&value](const char* needle) { return value.find(needle) != std::string::npos; };
  if (value.empty() || value == "not started") return "尚未启动";
  if (has("429") || has("resource-exhausted") || has("too many requests") || has("rate limit") || has("quota"))
    return "模型额度或限流，请查看电脑端";
  if (has("unauthorized") || has("forbidden") || has("invalid token") || has("token_invalid") || has("401") || has("403"))
    return "登录或设备授权已失效，请重新登录";
  if (has("no profile") || has("profile_not_found")) return "请在设备网页选择配置档";
  if (has("no device") || has("missing mcu login target") || has("login required")) return "请先在设备网页登录";
  if (has("remote offline") || has("remote client disconnected")) return "远程设备已离线，请检查电脑端";
  if (has("socket off") || has("socket unavailable") || has("disconnected")) return "后台连接断开，请稍后重试";
  if (has("missing_stt") || has("stt_not_configured") || has("missing stt")) return "请先在电脑端配置语音识别";
  if (has("tts_failed") || has("playback failed")) return "播放失败，文字回复已保留";
  if (has("initialization failed")) return "音频硬件初始化失败";
  if (has("es8311 via m5unified")) return "Cardputer ADV 音频硬件就绪";
  if (has("input is not ready")) return "麦克风未就绪，请重试";
  if (has("stream is not ready") || has("playback is not ready")) return "音频播放尚未就绪";
  if (has("alloc failed") || has("insufficient heap") || has("buffer reservation failed")) return "音频内存不足，请稍后重试";
  if (has("buffer is invalid")) return "音频缓冲区异常，请重试";
  if (has("interrupted by listen")) return "已暂停播放，开始录音";
  if (has("cancelled by queue clear")) return "音频队列已清空";
  if (has("navigation_cancelled")) return "已取消录音";
  if (has("audio busy")) return "音频忙碌中，请稍后重试";
  if (has("timed out") || has("timeout")) return "音频或网络等待超时，请重试";
  if (has("empty") || has("no data")) return "未收到有效音频，请重新说话";
  if (has("truncated") || has("did not complete") || has("prebuffer")) return "音频数据接收不完整，请重试";
  if (has("adpcm header") || has("sample rate or index") || has("length does not match")) return "音频格式异常，请检查电脑端";
  if (has("mp3 is not supported")) return "设备暂不支持 MP3，请使用 PCM 音频";
  if (has("audio url must")) return "音频地址无效，请检查电脑端";
  if (has("cannot connect") || has("cannot open") || has("http")) return "网络请求失败，请检查连接后重试";
  if (has("write failed") || has("read failed")) return "音频接口读写失败，请重试";
  if (has("send failed") || has("stream start failed") || has("record failed")) return "语音提交失败，请检查连接后重试";
  if (has("played bytes=") || has("playback bytes=")) return "音频播放完成";
  if (has("voice wav bytes=") || has("voice adpcm bytes=")) return "录音已提交";
  if (has("output volume ")) return "音量已更新";
  return nullptr;
}
}  // namespace native_ui
