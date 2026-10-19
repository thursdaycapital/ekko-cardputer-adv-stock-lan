#include "RenderColors.h"
#include "AgentAvatarAssets.h"
#include <lgfx/v1/misc/pixelcopy.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <type_traits>

static void verifyPixels(const uint16_t* pixels, size_t count) {
  using Input = std::remove_cv_t<std::remove_reference_t<decltype(*nativeRgb565Pixels(pixels))>>;
  static_assert(std::is_same<Input, lgfx::rgb565_t>::value);
  // This is the actual locked LGFX create_pc<T> copy function used by
  // typed pushImage, writing the SPI-order RGB565 sprite framebuffer.
  auto copy = lgfx::pixelcopy_t::get_fp_copy_rgb_affine<Input>(lgfx::rgb565_2Byte);
  std::array<lgfx::swap565_t, 256> framebuffer{};
  for (size_t offset = 0; offset < count; offset += framebuffer.size()) {
    const auto length = static_cast<uint32_t>(std::min(framebuffer.size(), count - offset));
    lgfx::pixelcopy_t pc;
    pc.src_data = nativeRgb565Pixels(pixels + offset);
    pc.src_bitwidth = length;
    assert(copy(framebuffer.data(), 0, length, &pc) == length);
    for (size_t i = 0; i < length; ++i) {
      const uint32_t actual = lgfx::color_convert<lgfx::rgb565_t, lgfx::swap565_t>(framebuffer[i].get());
      assert(actual == pixels[offset + i]);
    }
  }
}

int main() {
  // Exhaustive numerical RGB565 coverage catches endian mistakes even when
  // a particular avatar happens to use mostly byte-symmetric black/white.
  std::array<uint16_t, 256> colors{};
  for (uint32_t base = 0; base < 65536; base += colors.size()) {
    for (size_t i = 0; i < colors.size(); ++i) colors[i] = base + i;
    verifyPixels(colors.data(), colors.size());
  }
  const uint16_t interactionRow[] = {0x18E5, 0xEF3A, 0xF800, 0x07E0, 0x001F};
  verifyPixels(interactionRow, std::size(interactionRow));
  for (size_t index = 0; index < 16; ++index) {
    const auto* asset = nativeAgentLogo(index);
    assert(asset && asset->pixels && asset->width == 44 && asset->height == 44);
    assert(asset->pixels[0] == 0xEF3A); // Same paper color as fillRect().
    verifyPixels(asset->pixels, asset->width * asset->height);
  }
  std::cout << "Locked LGFX RGB565: all 65,536 colors, 16 avatars and interaction rows passed\n";
}
