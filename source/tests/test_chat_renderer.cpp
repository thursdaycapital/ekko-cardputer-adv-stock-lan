#include "ChatRenderer.h"
#include <cassert>
#include <iostream>
#include <string>

static void nativeTypographyAndStatusRegions() {
  NativeChatModel model;
  assert(model.activate("hermes", "default", 1) == NativeChatResult::Ok);
  const char* labels[] = {"网络断开", "需要登录", "验证登录", "连接后台", "恢复连接", "正在播放",
      "确认选择", "需更新", "读取记录", "同步会话", "发送中", "正在回复", "就绪", "正在录音",
      "正在识别", "正在执行", "清空会话", "请求失败", "已完成", "已停止", "等待会话"};
  static_assert(ekko_pixel_font::kPixels == 12, "Original Fusion pixel grid");
  static_assert(ekko_pixel_font::kDataSize < 700000, "Chinese pixel font fits the existing app slot");
  for (const char* label : labels) {
    NativeChatRenderOptions options; options.status = label;
    M5Canvas frame;
    nativeDrawChat(frame, model, options);
    bool found = false;
    for (size_t i = 0; i < frame.stringCount; ++i) {
      const auto& text = frame.strings[i];
      assert(text.textScale == 1); // No enlarged 12px bitmaps or fractional scaling.
      assert(text.fontPixels == 12);
      if (!std::strcmp(text.text, label)) {
        found = true;
        assert(text.foreground == 0xFFFF);
        assert(text.x == 164 && text.y == 2 && text.x + frame.textWidth(label) <= 232);
      } else if (text.y < native_chat_render::headerHeight) {
        const int right = text.y == 18 && text.x == 100 ? 232 : 156;
        assert(text.x + frame.textWidth(text.text) <= right);
        assert(text.y + 12 <= 31);
      }
    }
    assert(found);
  }
  NativeChatKey toggle; toggle.kind = NativeChatKeyKind::ToggleChinese;
  assert(model.inputMethod()->handle(toggle, *model.draft()).handled);
  M5Canvas english;
  nativeDrawChat(english, model, NativeChatRenderOptions{});
  assert(english.contains("英文 Ctrl+Space") && !english.contains("中文 Ctrl+Space"));
  assert(nativeChatVisibleLines(false, false) == 4);
  assert(nativeChatVisibleLines(false, true) == 3);
  assert(nativeChatVisibleLines(false, false, true) == 3);
  assert(nativeChatVisibleLines(false, true, true) == 2);
  assert(nativeChatVisibleLines(true, false) == 5);
  assert(nativeChatVisibleLines(true, true) == 4);
}

static void cleanMessageDisplay() {
  NativeChatModel model;
  assert(model.activate("hermes", "default", 1) == NativeChatResult::Ok);
  const NativeChatRoute route("hermes", "default", 1, "native-render-test");
  const char* user = "我想保留**我的原文**";
  const char* assistant = "AI **加粗标题**\n普通 **内容**，`2**3`，a*b*c。\n[官网](https://example.com/x)\n👋😄🙋️";
  assert(model.beginMessage(route, "turn", "u", NativeChatRole::User, 0, 0, false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "turn", "u", NativeChatRole::User, 0, user, std::strlen(user), true) == NativeChatResult::Ok);
  assert(model.beginMessage(route, "turn", "a", NativeChatRole::Assistant, 0, 0, false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "turn", "a", NativeChatRole::Assistant, 0, assistant, std::strlen(assistant), true) == NativeChatResult::Ok);
  NativeChatRenderOptions options; options.historyMode = true; options.bottomOffsetLines = UINT16_MAX;
  M5Canvas frame;
  const auto metrics = nativeDrawChat(frame, model, options);
  options.bottomOffsetLines = 0;
  M5Canvas tail;
  nativeDrawChat(tail, model, options);
  const auto visible = [&frame, &tail](const char* text) { return frame.contains(text) || tail.contains(text); };
  assert(frame.contains(user)); // The user's actual first 我 and markup remain literal.
  assert(visible("加粗标题") && visible("内容") && !visible("**加粗标题**"));
  assert(visible("2**3") && visible("a*b*c") && visible("example.com"));
  assert(visible("[招手]") && visible("[微笑]") && visible("[举手]"));
  bool userAtBubbleStart = false, assistantAtBubbleStart = false;
  for (size_t i = 0; i < frame.stringCount; ++i) {
    const auto& text = frame.strings[i];
    assert(std::strcmp(text.text, "我") != 0 && std::strcmp(text.text, "AI") != 0); // No synthetic role labels.
    if (std::strcmp(text.text, user) == 0) { assert(text.x == 32); userAtBubbleStart = true; }
    if (std::strcmp(text.text, "AI 加粗标题") == 0) { assert(text.x == 13); assistantAtBubbleStart = true; }
  }
  assert(userAtBubbleStart && assistantAtBubbleStart);
  assert(std::strcmp(model.messageAt(0)->text, user) == 0);
  assert(std::strcmp(model.messageAt(1)->text, assistant) == 0); // Original offsets/content are unchanged.
  assert(metrics.totalLines == native_chat_render::messageLines(frame, *model.messageAt(0)) +
      native_chat_render::messageLines(frame, *model.messageAt(1)));
}

int main() {
  nativeTypographyAndStatusRegions();
  cleanMessageDisplay();
  NativeChatModel model;
  assert(model.activate("codex", "default", 1) == NativeChatResult::Ok);
  const NativeChatRoute route("codex", "default", 1, "native-session");
  NativeChatRenderOptions options;
  options.agentIndex = 3; options.online = true;
  {
    M5Canvas frame;
    const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.totalLines == 0 && result.firstVisibleMessage == 0 && frame.imageRows == 18);
    assert(frame.contains("等待会话") && frame.contains("输入消息"));
    assert(!frame.contains("语音") && frame.contains("发送") && frame.contains("Fn+空格说话"));
    for (size_t i = 0; i < frame.stringCount; ++i)
      if (std::strcmp(frame.strings[i].text, "发送") == 0) assert(frame.strings[i].y == 118);
    assert(frame.fontPixels == 12); // Renderer keeps the native 12px measurement font.
    assert(!frame.contains("Fn+SPACE / G0") && !frame.contains("NATIVE / CHAT"));
    assert(frame.contains("中文 Ctrl+Space"));
    assert(frame.contains("Tab历史"));
    assert(result.visibleLines == nativeChatVisibleLines(false, false));
  }
  assert(model.beginMessage(route, "turn", "user", NativeChatRole::User, 0, 0, false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "turn", "user", NativeChatRole::User, 0, "用户问题", 12, true, 12) == NativeChatResult::Ok);
  assert(model.beginMessage(route, "turn", "assistant", NativeChatRole::Assistant, 0, 0, false) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "turn", "assistant", NativeChatRole::Assistant, 0, "真实中文回复", 18, true, 18) == NativeChatResult::Ok);
  assert(model.draft()->insert("hello world") == NativeChatEditResult::Ok);
  {
    M5Canvas frame;
    const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(frame.contains("用户问题") && frame.contains("真实中文回复") && frame.contains("hello world"));
    assert(!frame.contains("语音") && frame.contains("发送") && !frame.contains("Fn+SPACE / G0"));
    assert(!frame.contains("Fn+空格说话"));
    assert(result.totalLines == 2 && result.firstVisibleMessage == 0 && result.lastVisibleMessage == 1);
  }
  options.notice = "发送失败，草稿保留"; options.sending = true;
  {
    M5Canvas frame;
    const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.visibleLines == 3 && frame.contains("发送失败") && !frame.contains("语音") && frame.contains("发送"));
  }
  // Cursor may be between CR and LF. It must remain bounded and NUL-terminated.
  const std::string crlfDraft = std::string(96, 'a') + "\r\n后续";
  model.draft()->set(crlfDraft.c_str());
  assert(model.draft()->setCursor(97));
  { M5Canvas frame; nativeDrawChat(frame, model, options); }
  // A full two-line draft stays left of both physical-key legend buttons.
  model.draft()->set("abcdefghijklmnopqrstuvwxzy0123456789abcdefghijklmnopqrstuvwxyz");
  { M5Canvas frame; const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.draftLines >= 2 && !frame.contains("语音") && frame.contains("发送"));
    size_t visibleDraftLines = 0;
    for (size_t i = 0; i < frame.stringCount; ++i) {
      const ChatDrawnText& text = frame.strings[i];
      assert(text.textScale == 1 && text.fontPixels == 12);
      if (text.x == 11 && (text.y == 101 || text.y == 115)) {
        assert(text.x + frame.textWidth(text.text) <= 192); ++visibleDraftLines;
      }
      if (std::strcmp(text.text, "语音") == 0 || std::strcmp(text.text, "发送") == 0)
        assert(text.x >= 198);
    }
    assert(visibleDraftLines == 2); }

  // Pinyin has its own visible composition/candidate rows, never sent as a
  // message by the renderer or confused with the removed voice instructions.
  options.notice = "";
  for (const char* text = "nihao"; *text; ++text) {
    NativeChatKey key; key.kind = NativeChatKeyKind::Insert; key.character = *text;
    assert(model.inputMethod()->handle(key, *model.draft()).handled);
  }
  {
    M5Canvas frame; const auto result = nativeDrawChat(frame, model, options);
    assert(frame.contains("nihao") && frame.contains("你好") && frame.contains("中文 Ctrl+Space"));
    assert(frame.contains("发送") && !frame.contains("语音") && !frame.contains("Fn+SPACE"));
    assert(!frame.contains("Fn+空格说话"));
    assert(result.visibleLines == nativeChatVisibleLines(false, false, true));
    assert(result.visibleLines < nativeChatVisibleLines(false, false));
    bool candidateSeen = false, sendSeen = false;
    for (size_t i = 0; i < frame.stringCount; ++i) {
      const auto& text = frame.strings[i];
      if (text.y == 119) {
        candidateSeen = true;
        assert(text.fontPixels == 12 && text.x >= 10 && text.x <= 186 && text.y + 12 <= 133);
      }
      if (!std::strcmp(text.text, "发送")) {
        sendSeen = true;
        assert(text.y == 104 && text.x == 203 && text.y + ekko_pixel_font::kPixels < 119);
      }
    }
    assert(candidateSeen && sendSeen);
  }
  options.historyMode = true; options.notice = "";
  {
    M5Canvas frame;
    const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.visibleLines == 5 && result.visibleLines == nativeChatVisibleLines(true, false));
    assert(frame.contains("浏览历史") && frame.contains("Tab输入") && frame.contains("R最新"));
    assert(!frame.contains("发送") && !frame.contains("语音"));
    assert(!frame.contains("abcdefghijkl")); // Draft is retained, not drawn over history.
    assert(model.draft()->hasContent() && model.inputMethod()->composing());
  }
  options.notice = "正在同步历史记录";
  {
    M5Canvas frame;
    const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.visibleLines == 4 && result.visibleLines == nativeChatVisibleLines(true, true));
    assert(frame.contains("Tab输入") && frame.contains("正在同步"));
  }
  options.historyMode = false;
  model.inputMethod()->reset();

  assert(model.expectHistory("window", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "window") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "window", "assistant", NativeChatRole::Assistant, 1024, 2030, true, true) == NativeChatResult::Ok);
  const std::string part(512, 'x');
  assert(model.appendChunk(route, "window", "assistant", NativeChatRole::Assistant, 1024, part.c_str(), part.size(), true) == NativeChatResult::Ok);
  assert(model.endHistory(route, "window", false) == NativeChatResult::Ok);
  options.notice = ""; options.sending = false;
  {
    M5Canvas frame;
    const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.lastVisibleMessage == 1 && result.bottomWindowBoundary && !result.topWindowBoundary);
    assert(frame.contains("后续") && result.maxBottomOffset > 0);
  }
  options.bottomOffsetLines = 65535;
  {
    M5Canvas frame;
    const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.firstVisibleMessage == 0 && !result.topWindowBoundary);
  }
  model.clearMessages();
  assert(model.expectHistory("single-window", false) == NativeChatResult::Ok);
  assert(model.beginHistory(route, "single-window") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "single-window", "only", NativeChatRole::Assistant, 1024, 2030, true, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "single-window", "only", NativeChatRole::Assistant, 1024, part.c_str(), part.size(), true) == NativeChatResult::Ok);
  { M5Canvas frame; const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.firstVisibleMessage == 0 && result.topWindowBoundary && !result.bottomWindowBoundary); }
  assert(model.expectHistory("paged-history") == NativeChatResult::Ok);
  assert(model.beginHistory(route, "paged-history") == NativeChatResult::Ok);
  assert(model.beginMessage(route, "paged-history", "page-message", NativeChatRole::User, 0, 3, true, true) == NativeChatResult::Ok);
  assert(model.appendChunk(route, "paged-history", "page-message", NativeChatRole::User, 0, "one", 3, true) == NativeChatResult::Ok);
  assert(model.endHistory(route, "paged-history", true, true) == NativeChatResult::Ok);
  { M5Canvas frame; const NativeChatRenderMetrics result = nativeDrawChat(frame, model, options);
    assert(result.totalLines == 3 && result.firstVisibleMessage == 0 && result.lastVisibleMessage == 0);
    assert(frame.contains("更早消息") && frame.contains("更新消息") && frame.contains("Tab历史")); }
  std::cout << "Chat renderer: bounded drawing/real text/composer/notice/cursor/visible-window metrics passed (canvas spy)\n";
}
