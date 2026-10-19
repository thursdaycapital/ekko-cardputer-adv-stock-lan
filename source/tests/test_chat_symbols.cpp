#include "ChatSymbols.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

static size_t allocations = 0;
void* operator new(size_t length) {
  ++allocations;
  if (void* result = std::malloc(length ? length : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](size_t length) { return ::operator new(length); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }

extern bool chatSymbolsSecondTranslationUnit();

static size_t encode(uint32_t cp, char* text) {
  if (cp < 0x80) { text[0] = static_cast<char>(cp); return 1; }
  if (cp < 0x800) {
    text[0] = static_cast<char>(0xC0 | (cp >> 6)); text[1] = static_cast<char>(0x80 | (cp & 63)); return 2;
  }
  if (cp < 0x10000) {
    text[0] = static_cast<char>(0xE0 | (cp >> 12)); text[1] = static_cast<char>(0x80 | ((cp >> 6) & 63));
    text[2] = static_cast<char>(0x80 | (cp & 63)); return 3;
  }
  text[0] = static_cast<char>(0xF0 | (cp >> 18)); text[1] = static_cast<char>(0x80 | ((cp >> 12) & 63));
  text[2] = static_cast<char>(0x80 | ((cp >> 6) & 63)); text[3] = static_cast<char>(0x80 | (cp & 63)); return 4;
}
static void normalize(const char* original, const char* expected) {
  char buffer[512];
  const size_t bytes = std::strlen(original), target = std::strlen(expected);
  assert(bytes < sizeof(buffer) && target <= bytes);
  std::memset(buffer, 0x5A, sizeof(buffer));
  std::memcpy(buffer, original, bytes + 1);
  const size_t result = nativeChatNormalizeSymbols(buffer, bytes);
  assert(result == target && std::strcmp(buffer, expected) == 0);
  assert(buffer[bytes + 1] == 0x5A);
  assert(nativeChatNormalizeSymbols(buffer, result) == result); // Idempotent.
}
static void actualCases() {
  struct Label { uint32_t cp; const char* label; };
  for (const auto& item : {Label{0x1F44B, "[招手]"}, Label{0x1F604, "[微笑]"},
                         Label{0x1F605, "[汗笑]"}, Label{0x1F64B, "[举手]"},
                         Label{0x1FAE1, "[敬礼]"}, Label{0x2705, "[勾选]"}})
    assert(std::strcmp(nativeChatSymbolLabel(item.cp), item.label) == 0);
  assert(std::strcmp(nativeChatSymbolLabel(0x1F680), "[表情]") == 0); // Rocket, not a special case.
  assert(nativeChatCodepoint("🙋", 4) == 0x1F64B);
  normalize("你好🙋️，x * y / 2 × 3 ÷ 4 ♪♫ ∑∞", "你好🙋，x * y / 2 × 3 ÷ 4 ♪♫ ∑∞");
  normalize("👋🏽👋🏻", "👋👋");
  normalize("👩‍💻", "👩💻");
  normalize("👩🏽‍💻", "👩💻");
  normalize("❤️‍🔥", "❤🔥");
  normalize("क्‍ष می‍روم A‍B 你‍好 abc‍🚀 ➕‍➖", "क्‍ष می‍روم A‍B 你‍好 abc‍🚀 ➕‍➖");
  normalize("1️⃣ *️⃣ #️⃣", "1 * #");
  normalize("甲︎乙️丙", "甲乙丙");
  normalize("🏴\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xBF", "🏴");
  normalize("代码 */ 0123456789 +- × ÷ ± ∑ ∫ ∞ ⊕ ➕ ➖ ➗ 🟰 ♪♫𝄞 é", "代码 */ 0123456789 +- × ÷ ± ∑ ∫ ∞ ⊕ ➕ ➖ ➗ 🟰 ♪♫𝄞 é");
  for (uint32_t cp : {0x23u, 0x2Au, 0x30u, 0x39u, 0xD7u, 0xF7u, 0xB1u, 0x221Eu,
                     0x2211u, 0x222Bu, 0x266Au, 0x266Bu, 0x1D11Eu, 0x2795u, 0x2796u,
                     0x2797u, 0x1F7F0u, 0x2600u, 0x26A0u, 0x2716u})
    assert(nativeChatSymbolLabel(cp) == nullptr);
  char empty[1] = {};
  assert(nativeChatNormalizeSymbols(empty, 0) == 0 && !empty[0]);
  assert(nativeChatNormalizeSymbols(nullptr, 3) == 0);
  assert(chatSymbolsSecondTranslationUnit());
}
static void malformed() {
  assert(nativeChatCodepoint(nullptr, 1) == 0xFFFD && nativeChatCodepoint("", 0) == 0xFFFD);
  struct Invalid { const char* data; size_t bytes; };
  for (const auto& item : {Invalid{"\x80", 1}, Invalid{"\xC0\x80", 2}, Invalid{"\xC2", 1},
                          Invalid{"\xE0\x80\x80", 3}, Invalid{"\xED\xA0\x80", 3},
                          Invalid{"\xF0\x80\x80\x80", 4}, Invalid{"\xF4\x90\x80\x80", 4},
                          Invalid{"\xF0\x9F\x99", 3}, Invalid{"\xFF", 1}, Invalid{"AB", 2}})
    assert(nativeChatCodepoint(item.data, item.bytes) == 0xFFFD);
  // Normalization owns no replacement/error policy; it preserves invalid data
  // without out-of-bounds reads and still obeys its non-expansion contract.
  normalize("\xF0\x9F\x99", "\xF0\x9F\x99");
  normalize("\xFF", "\xFF");
}
static void allUnicodeOracle(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<uint8_t> expected((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  assert(expected.size() == 0x110000);
  const size_t before = allocations;
  size_t decoded = 0, emoji = 0, removed = 0;
  for (uint32_t cp = 0; cp <= 0x10FFFF; ++cp) {
    const uint8_t flags = expected[cp];
    const bool presentation = flags & 1, modifier = flags & 2, tag = flags & 4;
    const bool hidden = flags & 8, math = flags & 16;
    assert(native_chat_symbols_detail::emojiPresentation(cp) == presentation);
    assert(native_chat_symbols_detail::emojiModifier(cp) == modifier);
    assert(native_chat_symbols_detail::emojiTag(cp) == tag);
    assert(native_chat_symbols_detail::emojiJoinerBase(cp) == bool(flags & 32));
    assert(native_chat_symbols_detail::hiddenEmojiComponent(cp) == hidden);
    assert((nativeChatSymbolLabel(cp) != nullptr) == (presentation && !hidden && !math));
    if (presentation) ++emoji;
    if (cp >= 0xD800 && cp <= 0xDFFF) continue;
    char guarded[8]; std::memset(guarded, 0x5A, sizeof(guarded));
    const size_t bytes = encode(cp, guarded + 1);
    assert(nativeChatCodepoint(guarded + 1, bytes) == cp);
    ++decoded;
    char original[4]; std::memcpy(original, guarded + 1, bytes);
    const size_t result = nativeChatNormalizeSymbols(guarded + 1, bytes);
    assert(result == (hidden ? 0 : bytes));
    assert(guarded[0] == 0x5A && guarded[bytes + 2] == 0x5A && guarded[result + 1] == 0);
    if (!hidden) assert(std::memcmp(guarded + 1, original, bytes) == 0);
    else ++removed;
  }
  assert(before == allocations);
  assert(!nativeChatSymbolLabel(0x110000) && !nativeChatSymbolLabel(0xFFFFFFFF));
  std::cout << "Unicode source oracle: " << decoded << " scalars / " << emoji
            << " Emoji_Presentation / " << removed << " hidden controls; zero heap passed\n";
}
int main(int argc, char** argv) {
  assert(argc == 2);
  actualCases(); malformed(); allUnicodeOracle(argv[1]);
  std::cout << "Chat symbols: Chinese labels/math/music/code preservation/UTF8 bounds/nonexpansion/idempotence/two-TU passed\n";
}
