#pragma once
#include <cstdint>
#include <lgfx/v1/misc/colortype.hpp>

// Assets and generated interaction rows hold numerical RGB565 values. An
// untyped uint16_t* would depend on LGFX's global swapBytes setting (false
// means already-swapped bus bytes). The typed overload is unambiguous.
inline const lgfx::rgb565_t* nativeRgb565Pixels(const uint16_t* pixels) {
  return reinterpret_cast<const lgfx::rgb565_t*>(pixels);
}
