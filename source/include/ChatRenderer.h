#pragma once
#include "ChatModel.h"
#include "ChatLayout.h"
#include "ChatPlainText.h"
#include "ChatSymbols.h"
#include "AgentAvatarAssets.h"
#include "RenderColors.h"
#include "EkkoPixelFont12.h"
#include <M5GFX.h>
#include <array>

struct NativeChatRenderOptions {
  size_t agentIndex = 0;
  const char* status = "";
  const char* notice = "";
  uint16_t bottomOffsetLines = 0;
  bool cursorVisible = true;
  bool online = false;
  bool sending = false;
  bool historyMode = false;
};
struct NativeChatRenderMetrics {
  uint16_t totalLines = 0;
  uint16_t visibleLines = 0;
  uint16_t maxBottomOffset = 0;
  uint16_t bottomOffset = 0;
  uint16_t firstLine = 0;
  uint16_t draftLines = 0;
  uint16_t draftFirstLine = 0;
  size_t firstVisibleMessage = 0;
  size_t lastVisibleMessage = 0;
  bool topWindowBoundary = false;
  bool bottomWindowBoundary = false;
  bool truncatedBefore = false;
  bool truncatedAfter = false;
};

namespace native_chat_render {
constexpr uint16_t paper = 0xEF3A, ink = 0x18E5, amber = 0xECA6;
constexpr uint16_t sheet = 0xF7BE, userFill = 0xDDF6, lineColor = 0xBDF3, quiet = 0x6B6D;
constexpr uint16_t statusWhite = 0xFFFF;
constexpr int rowHeight = 14;
constexpr int headerHeight = 33, contentTop = 34, noticeContentTop = 52;
constexpr int composerTop = 98, composingTop = 84, historyFooterTop = 117;
constexpr int draftWidth = 181;

inline void drawLine(M5Canvas& frame, const char* source, const NativeChatLine& line, int x, int y) {
  char buffer[kNativeChatLineBytes + 1]{};
  if (line.bytes) std::memcpy(buffer, source + line.offset, line.bytes);
  frame.drawString(buffer, x, y);
}

inline int messageScalarWidth(M5Canvas& frame, const char* scalar) {
  const char* label = nativeChatSymbolLabel(nativeChatCodepoint(scalar, std::strlen(scalar)));
  return frame.textWidth(label ? label : scalar);
}
inline void drawMessageLine(M5Canvas& frame, const char* source, const NativeChatLine& line, int x, int y) {
  char run[kNativeChatLineBytes + 1]{};
  size_t buffered = 0;
  const auto flush = [&frame, &run, &buffered, &x, y]() {
    if (!buffered) return;
    run[buffered] = 0;
    frame.drawString(run, x, y); x += frame.textWidth(run); buffered = 0;
  };
  for (size_t pos = 0; pos < line.bytes;) {
    const char* scalar = source + line.offset + pos;
    const size_t bytes = native_chat::codepointBytes(static_cast<uint8_t>(*scalar));
    const char* label = nativeChatSymbolLabel(nativeChatCodepoint(scalar, bytes));
    if (label) {
      flush(); frame.drawString(label, x, y); x += frame.textWidth(label);
    } else {
      std::memcpy(run + buffered, scalar, bytes); buffered += bytes;
    }
    pos += bytes;
  }
  flush();
}

inline void drawClipped(M5Canvas& frame, const char* text, int x, int y, uint16_t width) {
  if (!text) return;
  const size_t length = std::strlen(text);
  nativeChatWrappedLines(text, length, width,
      [&frame](const char* scalar) { return frame.textWidth(scalar); },
      [&frame, text, x, y](const NativeChatLine& line) {
        if (line.index == 0) drawLine(frame, text, line, x, y);
      });
}

// Display formatting never changes the native message or its byte offsets.
// User text remains literal; only assistant Markdown gets a reading copy.
struct MessageDisplayText {
  char plain[kNativeChatMessageBytes + 1];
  const char* text;
  size_t bytes;
  explicit MessageDisplayText(const NativeChatMessage& message)
      : text(plain), bytes(message.stored_bytes) {
    bool formatted = false;
    if (message.role == NativeChatRole::Assistant) {
      NativeChatPlainTextOptions options;
      options.truncatedBefore = message.truncated_before;
      options.truncatedAfter = message.truncated_after || !message.window_complete;
      const auto result = nativeChatPlainText(message.text, message.stored_bytes,
                                              plain, sizeof(plain), options);
      if (result.valid && !result.truncated) { bytes = result.bytes; formatted = true; }
    }
    if (!formatted) { std::memcpy(plain, message.text, bytes); plain[bytes] = 0; }
    bytes = nativeChatNormalizeSymbols(plain, bytes);
  }
};
inline uint16_t messageBodyLines(M5Canvas& frame, const NativeChatMessage& message) {
  const MessageDisplayText display(message);
  return nativeChatWrappedLines(display.text, display.bytes, 194,
      [&frame](const char* scalar) { return messageScalarWidth(frame, scalar); },
      [](const NativeChatLine&) {});
}
inline uint16_t messageHintLines(const NativeChatMessage& message) {
  return (message.truncated_before ? 1 : 0) + (message.truncated_after ? 1 : 0) + (message.gap ? 1 : 0);
}
inline uint16_t messageLines(M5Canvas& frame, const NativeChatMessage& message) {
  return messageBodyLines(frame, message) + messageHintLines(message);
}
} // namespace native_chat_render

inline uint16_t nativeChatVisibleLines(bool historyMode, bool hasNotice, bool composing = false) {
  return static_cast<uint16_t>(((historyMode ? native_chat_render::historyFooterTop :
      composing ? native_chat_render::composingTop : native_chat_render::composerTop) -
      (hasNotice ? native_chat_render::noticeContentTop : native_chat_render::contentTop)) /
                               native_chat_render::rowHeight);
}

// Draw into the caller's already allocated offscreen frame. No LCD access,
// sprite allocation, String construction, history fetch or audio side effects.
inline NativeChatRenderMetrics nativeDrawChat(M5Canvas& frame, const NativeChatModel& model,
                                               const NativeChatRenderOptions& options) {
  using namespace native_chat_render;
  NativeChatRenderMetrics metrics;
  const NativeChineseInput* ime = model.inputMethod();
  const bool composing = !options.historyMode && ime && ime->composing();
  metrics.firstVisibleMessage = metrics.lastVisibleMessage = model.messageCount();
  frame.fillScreen(paper);
  frame.setTextSize(1); frame.setFont(&ekko_pixel_font::kFont);
  frame.fillRect(0, 0, 240, headerHeight, ink);
  const AgentAvatarAsset* avatar = nativeAgentLogo(options.agentIndex);
  if (avatar && avatar->pixels && avatar->width && avatar->height) {
    uint16_t row[18];
    for (int y = 0; y < 18; ++y) {
      for (int x = 0; x < 18; ++x)
        row[x] = avatar->pixels[(y * avatar->height / 18) * avatar->width + x * avatar->width / 18];
      frame.pushImage(7, 7 + y, 18, 1, nativeRgb565Pixels(row));
    }
  }
  frame.setTextColor(paper, ink);
  frame.setFont(&ekko_pixel_font::kFont);
  drawClipped(frame, options.agentIndex < kNativeAgentCount ? kNativeAgents[options.agentIndex].name : "未知智能体", 32, 2, 124);
  frame.setFont(&ekko_pixel_font::kFont);
  frame.setTextColor(statusWhite, ink);
  const char* status = options.status && *options.status ? options.status : (model.ready() ? "就绪" : "等待会话");
  drawClipped(frame, status, 164, 2, 68);
  frame.setFont(&ekko_pixel_font::kFont);
  frame.setTextColor(paper, ink);
  drawClipped(frame, options.historyMode ? "浏览历史" : "Tab历史", 32, 18, 65);
  frame.setTextColor(lineColor, ink);
  drawClipped(frame, ime && ime->enabled() ? "中文 Ctrl+Space" : "英文 Ctrl+Space", 100, 18, 132);
  frame.fillCircle(236, 15, 2, options.online ? amber : quiet);
  frame.drawFastHLine(0, headerHeight - 1, 240, amber);
  frame.setFont(&ekko_pixel_font::kFont);

  const bool hasNotice = options.notice && *options.notice;
  const int top = hasNotice ? noticeContentTop : contentTop;
  if (hasNotice) {
    frame.fillRoundRect(5, contentTop, 230, 16, 3, userFill);
    frame.setTextColor(ink, userFill);
    drawClipped(frame, options.notice, 10, contentTop + 1, 219);
  }
  const uint16_t visible = nativeChatVisibleLines(options.historyMode, hasNotice, composing);
  std::array<uint16_t, kNativeChatMessageCount> bodyLineCounts{};
  uint16_t total = model.context() && model.context()->history_has_more ? 1 : 0;
  if (model.context() && model.context()->history_has_newer) ++total;
  for (size_t i = 0; i < model.messageCount(); ++i) {
    const NativeChatMessage* message = model.messageAt(i);
    bodyLineCounts[i] = messageBodyLines(frame, *message);
    total += bodyLineCounts[i] + messageHintLines(*message);
    metrics.truncatedBefore = metrics.truncatedBefore || message->truncated_before;
    metrics.truncatedAfter = metrics.truncatedAfter || message->truncated_after;
  }
  const NativeChatViewport viewport = nativeChatViewport(total, visible, options.bottomOffsetLines);
  metrics.totalLines = total; metrics.visibleLines = visible;
  metrics.maxBottomOffset = viewport.maxBottomOffset;
  metrics.bottomOffset = viewport.bottomOffset; metrics.firstLine = viewport.firstLine;
  uint16_t virtualLine = 0;
  const auto hintRow = [&frame, &virtualLine, &viewport, top](const char* label) {
    if (virtualLine >= viewport.firstLine && virtualLine < viewport.firstLine + viewport.visibleLines) {
      frame.setTextColor(quiet, paper);
      drawClipped(frame, label, 12, top + (virtualLine - viewport.firstLine) * rowHeight, 216);
    }
    ++virtualLine;
  };
  if (model.context() && model.context()->history_has_more) hintRow(options.historyMode ? "← 加载更早消息" : "Tab历史 · 更早消息");
  for (size_t index = 0; index < model.messageCount(); ++index) {
    const NativeChatMessage* message = model.messageAt(index);
    const uint16_t messageStart = virtualLine;
    if (message->truncated_before) hintRow(options.historyMode ? "↑ 前一段" : "↑ 前一段 · Fn+上");
    const uint16_t bodyLines = bodyLineCounts[index];
    const uint16_t first = virtualLine;
    const uint16_t messageEnd = first + bodyLines + (message->truncated_after ? 1 : 0) + (message->gap ? 1 : 0);
    if (messageEnd > viewport.firstLine && messageStart < viewport.firstLine + viewport.visibleLines) {
      if (metrics.firstVisibleMessage == model.messageCount()) {
        metrics.firstVisibleMessage = index;
        metrics.topWindowBoundary = message->truncated_before && viewport.firstLine <= first;
      }
      metrics.lastVisibleMessage = index;
      metrics.bottomWindowBoundary = message->truncated_after &&
          viewport.firstLine + viewport.visibleLines >= first + bodyLines;
    }
    const uint16_t clippedTop = std::max(first, viewport.firstLine);
    const uint16_t clippedEnd = std::min<uint16_t>(first + bodyLines, viewport.firstLine + viewport.visibleLines);
    const bool user = message->role == NativeChatRole::User;
    const int x = user ? 26 : 7;
    const uint16_t background = user ? userFill : sheet;
    if (clippedEnd > clippedTop) {
      frame.fillRoundRect(x, top + (clippedTop - viewport.firstLine) * rowHeight, 207,
                          (clippedEnd - clippedTop) * rowHeight - 1, 4, background);
      frame.setTextColor(ink, background);
      const MessageDisplayText display(*message);
      nativeChatWrappedLines(display.text, display.bytes, 194,
          [&frame](const char* scalar) { return messageScalarWidth(frame, scalar); },
          [&frame, &display, first, &viewport, top, x](const NativeChatLine& line) {
            const uint16_t position = first + line.index;
            if (position < viewport.firstLine || position >= viewport.firstLine + viewport.visibleLines) return;
            const int y = top + (position - viewport.firstLine) * rowHeight;
            drawMessageLine(frame, display.text, line, x + 6, y);
          });
    }
    virtualLine += bodyLines;
    if (message->truncated_after) hintRow(options.historyMode ? "↓ 后续" : "↓ 后续 · Fn+下");
    if (message->gap) hintRow("片段缺失，请重新加载");
  }
  if (model.context() && model.context()->history_has_newer) hintRow(options.historyMode ? "→ 加载更新消息" : "Tab历史 · 更新消息");
  if (!total) {
    frame.setTextColor(quiet, paper);
    drawClipped(frame, model.ready() ? "输入消息，开始对话" : "正在加载原生会话…", 18, top + rowHeight, 204);
  }
  if (viewport.maxBottomOffset) {
    const int railHeight = visible * rowHeight;
    frame.drawFastVLine(237, top, railHeight, lineColor);
    const int thumb = std::max(5, railHeight * visible / std::max<uint16_t>(visible, total));
    const int position = (railHeight - thumb) * viewport.firstLine / viewport.maxBottomOffset;
    frame.fillRect(237, top + position, 2, thumb, quiet);
  }

  if (options.historyMode) {
    frame.fillRect(0, historyFooterTop, 240, 18, ink);
    frame.setFont(&ekko_pixel_font::kFont);
    frame.setTextColor(paper, ink);
    drawClipped(frame, "Tab输入 ;↑ .↓ ,← /→ R最新", 7, historyFooterTop + 3, 226);
    frame.setFont(&ekko_pixel_font::kFont);
    return metrics;
  }

  const int inputTop = composing ? composingTop : composerTop;
  frame.fillRoundRect(5, inputTop, 230, 134 - inputTop, 4, sheet);
  frame.drawRoundRect(5, inputTop, 230, 134 - inputTop, 4, lineColor);
  frame.setFont(&ekko_pixel_font::kFont); frame.setTextColor(ink, sheet);
  const NativeChatInput* input = model.draft();
  uint16_t cursorLine = 0;
  if (input) {
    metrics.draftLines = nativeChatWrappedLines(input->data(), input->size(), draftWidth,
        [&frame](const char* scalar) { return frame.textWidth(scalar); },
        [&cursorLine, input](const NativeChatLine& line) {
          if (input->cursor() >= line.offset) cursorLine = line.index;
        });
    metrics.draftFirstLine = composing ? cursorLine : cursorLine > 0 ? cursorLine - 1 : 0;
  }
  if (!input || input->empty()) {
    frame.setTextColor(quiet, sheet);
    if (!composing) {
      drawClipped(frame, "输入消息…", 11, inputTop + 3, draftWidth);
      drawClipped(frame, "Fn+空格说话", 11, inputTop + 17, draftWidth);
    }
    if (input && options.cursorVisible) frame.drawFastVLine(10, inputTop + 4, 11, ink);
  } else {
    nativeChatWrappedLines(input->data(), input->size(), draftWidth,
        [&frame](const char* scalar) { return frame.textWidth(scalar); },
        [&frame, input, &metrics, &options, cursorLine, composing, inputTop](const NativeChatLine& line) {
          if (line.index < metrics.draftFirstLine || line.index >= metrics.draftFirstLine + (composing ? 1 : 2)) return;
          const int y = inputTop + 3 + (line.index - metrics.draftFirstLine) * rowHeight;
          drawLine(frame, input->data(), line, 11, y);
          if (!options.cursorVisible || line.index != cursorLine) return;
          char prefix[kNativeChatLineBytes + 1]{};
          const size_t bytes = std::min(input->cursor() - line.offset, line.bytes);
          if (bytes) std::memcpy(prefix, input->data() + line.offset, bytes);
          const int x = 11 + frame.textWidth(prefix);
          frame.drawFastVLine(std::min(x, 193), y + 1, 11, ink);
        });
  }
  // Voice remains a physical shortcut; the composer exposes only Send.
  const int sendTop = composing ? 102 : 116;
  frame.fillRoundRect(198, sendTop, 35, 17, 3, options.sending ? quiet : ink);
  frame.setFont(&ekko_pixel_font::kFont); frame.setTextColor(paper, options.sending ? quiet : ink);
  frame.drawString("发送", 203, sendTop + 2);
  // Body, input and candidates share the same native 12px font metrics.
  frame.setFont(&ekko_pixel_font::kFont);
  if (composing) {
    frame.setTextColor(ink, sheet);
    const char* raw = ime->preedit();
    // Preedit is ASCII Pinyin. Show its measured suffix so the last typed
    // syllable remains visible with native 12px pixel advances.
    while (*raw && frame.textWidth(raw) > draftWidth) ++raw;
    drawClipped(frame, raw, 11, 103, draftWidth);
    frame.drawFastHLine(11, 118, draftWidth, lineColor);
    const auto& page = ime->candidates();
    for (size_t i = 0; i < page.count && i < 5; ++i) {
      char label[64]{};
      label[0] = static_cast<char>('1' + i);
      const size_t bytes = std::min(std::strlen(page.candidates[i].text), sizeof(label) - 3);
      std::memcpy(label + 1, page.candidates[i].text, bytes);
      drawClipped(frame, label, 10 + i * 44, 119, 43);
    }
    if (!page.count) drawClipped(frame, "无候选 · OK保留原文", 11, 119, 217);
  }
  return metrics;
}
