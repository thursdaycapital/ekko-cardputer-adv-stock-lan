#pragma once
// Host canvas spy only: verifies renderer coordinates/text and allocation-free
// control flow. It does not emulate fonts, LCD color transfer or physical UI.
#include "ChatUtf8.h"
#include <array>
#include <cassert>
#include <cstring>

namespace fonts { static const int Font0 = 8; static const int efontCN_12 = 12; static const int efontCN_14 = 14; }
namespace lgfx {
struct FontMetrics {
  int16_t width = 0, x_advance = 0, x_offset = 0, height = 0;
  int16_t y_advance = 0, y_offset = 0, baseline = 0;
};
struct TextStyle { float size_x = 1, size_y = 1; };
struct LGFXBase {
  int32_t clipX = 0, clipY = 0, clipW = 240, clipH = 135;
  int32_t drawnX = 0, drawnY = 0, drawnW = 0, drawnH = 0;
  void getClipRect(int32_t* x, int32_t* y, int32_t* w, int32_t* h) {
    *x = clipX; *y = clipY; *w = clipW; *h = clipH;
  }
  void setClipRect(int32_t x, int32_t y, int32_t w, int32_t h) {
    clipX = x; clipY = y; clipW = w; clipH = h;
  }
};
struct U8g2font {
  const uint8_t* data;
  constexpr U8g2font(const uint8_t* value) : data(value) {}
  virtual void getDefaultMetric(FontMetrics* metric) const {
    metric->height = data[10]; metric->width = data[9];
    metric->baseline = data[10] + static_cast<int8_t>(data[12]);
    metric->y_offset = -metric->baseline; metric->y_advance = metric->height;
  }
  virtual size_t drawChar(LGFXBase* gfx, int32_t, int32_t, uint16_t c,
      const TextStyle*, FontMetrics*, int32_t&) const {
    gfx->getClipRect(&gfx->drawnX, &gfx->drawnY, &gfx->drawnW, &gfx->drawnH);
    return c < 128 ? 6 : 12; // Clip contract only; no raster emulation.
  }
};
}
struct ChatDrawnText {
  char text[97]{};
  int x = 0, y = 0, fontPixels = 0, textScale = 0;
  uint16_t foreground = 0, background = 0;
};
class M5Canvas {
 public:
  std::array<ChatDrawnText, 128> strings{};
  size_t stringCount = 0, imageRows = 0;
  bool smallFont = false;
  int fontPixels = 12, fontCell = 12, textScale = 1;
  uint16_t foreground = 0, background = 0;
  void fillScreen(uint16_t) {}
  void setTextSize(int scale) { assert(scale > 0); textScale = scale; }
  void setFont(const int* font) { fontPixels = fontCell = *font; smallFont = *font == 8; }
  void setFont(const lgfx::U8g2font* font) {
    lgfx::FontMetrics metrics;
    font->getDefaultMetric(&metrics);
    fontPixels = metrics.height; fontCell = 12; smallFont = false;
    // Region/flow spy only. Official decoder checks actual glyph pixels/advance.
  }
  void setTextColor(uint16_t fg, uint16_t bg) { foreground = fg; background = bg; }
  int textWidth(const char* text) {
    int width = 0;
    const size_t length = std::strlen(text);
    assert(native_chat::validUtf8(text, length));
    for (size_t position = 0; position < length;) {
      const uint8_t lead = static_cast<uint8_t>(text[position]);
      width += (smallFont ? 6 : lead < 0x80 ? fontCell / 2 : fontCell) * textScale;
      position += native_chat::codepointBytes(lead);
    }
    return width;
  }
  int drawString(const char* text, int x, int y) {
    assert(stringCount < strings.size() && std::strlen(text) < sizeof(strings[0].text));
    assert(x >= 0 && y >= 0 && x + textWidth(text) <= 240 && y + fontPixels * textScale <= 135);
    ChatDrawnText& record = strings[stringCount++];
    std::strcpy(record.text, text); record.x = x; record.y = y;
    record.fontPixels = fontPixels; record.textScale = textScale;
    record.foreground = foreground; record.background = background;
    return textWidth(text);
  }
  void fillRect(int x, int y, int width, int height, uint16_t) { bounds(x, y, width, height); }
  void fillRoundRect(int x, int y, int width, int height, int, uint16_t) { bounds(x, y, width, height); }
  void drawRoundRect(int x, int y, int width, int height, int, uint16_t) { bounds(x, y, width, height); }
  void drawFastHLine(int x, int y, int length, uint16_t) { bounds(x, y, length, 1); }
  void drawFastVLine(int x, int y, int length, uint16_t) { bounds(x, y, 1, length); }
  void fillCircle(int x, int y, int radius, uint16_t) { bounds(x - radius, y - radius, radius * 2 + 1, radius * 2 + 1); }
  template <class Pixel> void pushImage(int x, int y, int width, int height, const Pixel* pixels) {
    assert(pixels); bounds(x, y, width, height); imageRows += height;
  }
  bool contains(const char* text) const {
    for (size_t i = 0; i < stringCount; ++i) if (std::strstr(strings[i].text, text)) return true;
    return false;
  }
 private:
  static void bounds(int x, int y, int width, int height) {
    assert(x >= 0 && y >= 0 && width >= 0 && height >= 0 && x + width <= 240 && y + height <= 135);
  }
};
