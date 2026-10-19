#pragma once
#include <M5GFX.h>
#include <cstdint>

// Ekko Pixel12 Chinese Compatible: Fusion Pixel Font 2026.09.01 with 180 original
// M5GFX 0.2.33 efontCN_12 compatibility glyphs. This is not a pure Fusion font.
// Copyright (c) 2022, TakWolf (https://takwolf.com).
// Copyright 2000-2003 /efont/ The Electronic Font Open Laboratory.
// Preserve OFL.txt, EFONT-LICENSE.txt, NOTICE.md and upstream component licenses.
// Glyph pixels / BBX / advances are unchanged. See coverage and pixel validation.
// Data has one external definition shared by all M5GFX and U8g2 consumers.
namespace ekko_pixel_font {
constexpr uint8_t kPixels = 12;
constexpr uint8_t kBaseline = 10;
constexpr uint32_t kDataSize = 654185;
constexpr uint16_t kGlyphCount = 24025;
extern const uint8_t kData[kDataSize];
class Font final : public lgfx::U8g2font {
 public:
  constexpr Font() : lgfx::U8g2font(kData) {}
  size_t drawChar(lgfx::LGFXBase* gfx, int32_t x, int32_t y, uint16_t codepoint,
      const lgfx::TextStyle* style, lgfx::FontMetrics* metrics, int32_t& filledX) const override;
  void getDefaultMetric(lgfx::FontMetrics* metrics) const override {
    metrics->width = kPixels;
    metrics->x_advance = kPixels;
    metrics->height = kPixels;
    metrics->y_advance = kPixels;
    metrics->baseline = kBaseline;
    metrics->y_offset = -kBaseline;
    metrics->x_offset = 0;
  }
};
extern const Font kFont;
} // namespace ekko_pixel_font
