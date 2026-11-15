#pragma once
#include "DevicePresentation.h"
#include "ChatRenderer.h"
#include <cstdio>

// Uses the same production canvas and unchanged 12px glyph/44px avatar assets
// as chat. This function is also exercised by the host canvas contract tests.
inline void drawDeviceHome(M5Canvas& frame, const CardputerPresentation& view) {
  using namespace native_chat_render;
  const auto index = view.selection.browse;
  if (index >= kNativeAgentCount) return;
  frame.fillScreen(paper);
  frame.setFont(&ekko_pixel_font::kFont);
  frame.setTextSize(1);
  frame.fillRect(0, 0, 240, 25, ink);
  frame.setTextColor(paper, ink);
  frame.drawString("EKKO STUDIO", 10, 6);
  drawClipped(frame, view.modeLabel && *view.modeLabel ? view.modeLabel : "Studio模型", 98, 6, 77);
  char position[20]{};
  std::snprintf(position, sizeof(position), "%02u / %02u", unsigned(index + 1), unsigned(kNativeAgentCount));
  frame.drawString(position, 181, 6);
  frame.fillRoundRect(9, 35, 50, 50, 4, lineColor);
  frame.fillRect(12, 38, 44, 44, paper);
  const auto* avatar = nativeAgentLogo(index);
  if (avatar && avatar->pixels && avatar->width == 44 && avatar->height == 44)
    frame.pushImage(12, 38, 44, 44, nativeRgb565Pixels(avatar->pixels));
  else {
    frame.setTextColor(ink, paper);
    frame.drawString("缺图", 20, 53);
  }
  frame.setTextColor(ink, paper);
  drawClipped(frame, kNativeAgents[index].name, 68, 36, 160);
  frame.setTextColor(quiet, paper);
  drawClipped(frame, view.status && *view.status ? view.status :
      view.online ? "电脑已连接" : "连接后台", 68, 55, 160);
  drawClipped(frame, view.exiting ? "正在返回原厂菜单" : view.waiting ? "正在打开对话" : "按 OK 进入聊天", 68, 72, 160);
  for (size_t i = 0; i < kNativeAgentCount; ++i)
    frame.fillRect(12 + i * 14, 91, 8, 3, i == index ? ink : lineColor);
  frame.setTextColor(ink, paper);
  const char* message = view.message && *view.message ? view.message :
      !view.online && view.address && *view.address ? view.address : "";
  drawClipped(frame, message, 12, 99, 216);
  frame.fillRect(0, 116, 240, 19, ink);
  frame.setTextColor(paper, ink);
  frame.drawString("左右选择 OK进入 Tab模式", 10, 119);
}
