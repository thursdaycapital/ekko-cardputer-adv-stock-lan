#include "EkkoPixelFont12.h"
#include <cassert>
#include <iostream>

int main() {
  lgfx::FontMetrics metrics;
  ekko_pixel_font::kFont.getDefaultMetric(&metrics);
  assert(metrics.height == 12 && metrics.y_advance == 12 && metrics.baseline == 10);
  assert(metrics.y_offset == -10 && metrics.width == 12 && metrics.x_advance == 12);
  lgfx::LGFXBase frame;
  lgfx::TextStyle style;
  int32_t filled = 0;
  // M5GFX passes the baseline. The wrapper clips the original tall glyph
  // to its native 12px line and restores the caller's narrower clip.
  frame.setClipRect(8, 36, 210, 60);
  assert(ekko_pixel_font::kFont.drawChar(&frame, 12, 44, 0x3031, &style, &metrics, filled) == 12);
  assert(frame.drawnX == 8 && frame.drawnY == 36 && frame.drawnW == 210 && frame.drawnH == 10);
  assert(frame.clipX == 8 && frame.clipY == 36 && frame.clipW == 210 && frame.clipH == 60);
  style.size_y = 2;
  ekko_pixel_font::kFont.drawChar(&frame, 12, 68, 0x3032, &style, &metrics, filled);
  assert(frame.drawnY == 48 && frame.drawnH == 24);
  assert(frame.clipY == 36 && frame.clipH == 60);
  style.size_y = 1;
  ekko_pixel_font::kFont.drawChar(&frame, 12, 120, 'A', &style, &metrics, filled);
  assert(frame.drawnH == 0); // No pixels escape a disjoint caller clip.
  assert(frame.clipY == 36 && frame.clipH == 60);
  std::cout << "Pixel font: 12px metrics, baseline/clip intersection/restore passed (canvas spy)\n";
}
