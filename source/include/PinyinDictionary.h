#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../assets/pinyin/PinyinDictionaryData.h"

constexpr size_t kNativePinyinPageSize = 5;
constexpr size_t kNativePinyinMaxInputBytes = 63;
constexpr size_t kNativePinyinCandidateBytes = 49;

struct NativePinyinCandidate {
  char text[kNativePinyinCandidateBytes]{};
  uint8_t consumed_bytes = 0;
  uint8_t syllables = 0;
  bool complete = false;
};

struct NativePinyinPage {
  NativePinyinCandidate candidates[kNativePinyinPageSize];
  size_t count = 0, total = 0, page = 0;
  bool has_next = false, valid = false;
};

namespace native_pinyin_detail {
struct Input {
  char letters[kNativePinyinMaxInputBytes + 1]{};
  // Number of original UTF-8 bytes to consume for each normalized prefix.
  uint8_t consumed[kNativePinyinMaxInputBytes + 1]{};
  uint64_t explicit_boundaries = 0;
  uint8_t length = 0;
};

inline bool normalize(const char* source, Input& result) {
  if (!source) return false;
  size_t bytes = 0;
  while (bytes <= kNativePinyinMaxInputBytes && source[bytes]) ++bytes;
  if (bytes > kNativePinyinMaxInputBytes) return false;
  for (size_t i = 0; i < bytes;) {
    uint8_t value = static_cast<uint8_t>(source[i]);
    if (value == ' ' || value == '\'') {
      if (result.length) result.explicit_boundaries |= uint64_t(1) << result.length;
      result.consumed[result.length] = static_cast<uint8_t>(++i);
      continue;
    }
    size_t width = 1;
    if (value == 0xC3 && i + 1 < bytes &&
        (static_cast<uint8_t>(source[i + 1]) == 0xBC || static_cast<uint8_t>(source[i + 1]) == 0x9C)) {
      value = 'v'; width = 2; // ü / Ü
    } else {
      if (value >= 'A' && value <= 'Z') value = static_cast<uint8_t>(value + ('a' - 'A'));
      if (value < 'a' || value > 'z') return false;
      if (value == 'u' && i + 1 < bytes && source[i + 1] == ':') { value = 'v'; width = 2; }
    }
    result.letters[result.length++] = static_cast<char>(value);
    i += width;
    result.consumed[result.length] = static_cast<uint8_t>(i);
  }
  // The source table writes ü after j/q/x/y as u. n/l üe aliases are added
  // per real syllable by the generator: never rewrite across nv + er etc.
  for (size_t i = 1; i < result.length; ++i) {
    if (result.letters[i] != 'v' || (result.explicit_boundaries & (uint64_t(1) << i))) continue;
    const char before = result.letters[i - 1];
    if (before == 'j' || before == 'q' || before == 'x' || before == 'y') result.letters[i] = 'u';
  }
  return true;
}

inline uint32_t read24(const char* data) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16);
}
inline uint16_t read16(const char* data) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
}
inline int compare(const char* key, const char* input, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const unsigned char a = static_cast<unsigned char>(key[i]);
    const unsigned char b = static_cast<unsigned char>(input[i]);
    if (a != b) return a < b ? -1 : 1;
    if (!a) return -1;
  }
  return key[length] ? 1 : 0;
}
inline const char* groupFor(const char* key, size_t length) {
  size_t lower = 0, upper = native_pinyin_data::kGroupCount;
  while (lower < upper) {
    const size_t middle = lower + (upper - lower) / 2;
    const char* group = native_pinyin_data::kGroups + middle * 8;
    const char* stored = native_pinyin_data::kKeys + read24(group);
    if (compare(stored, key, length) < 0) lower = middle + 1;
    else upper = middle;
  }
  if (lower == native_pinyin_data::kGroupCount) return nullptr;
  const char* group = native_pinyin_data::kGroups + lower * 8;
  return compare(native_pinyin_data::kKeys + read24(group), key, length) == 0 ? group : nullptr;
}
inline bool boundariesMatch(uint16_t packed, const Input& input, size_t length, uint8_t& syllables) {
  uint64_t stored = 0;
  syllables = 1;
  for (size_t i = 0; i < 3; ++i) {
    const uint8_t position = static_cast<uint8_t>((packed >> (5 * i)) & 31);
    if (position) { stored |= uint64_t(1) << position; ++syllables; }
  }
  const uint64_t required = input.explicit_boundaries & ((uint64_t(1) << length) - 1);
  return (required & ~stored) == 0;
}
}  // namespace native_pinyin_detail

class NativePinyinDictionary {
 public:
  // Page is zero-based. Return value means valid input, not "has candidates".
  // Empty input is valid. No allocation, mutable cache, network or filesystem.
  // Full-input matches precede longest known word/character prefixes. Within
  // a matching key, original word frequency orders the candidates. Prefix
  // selection deliberately leaves an unfinished/unknown suffix editable.
  static bool lookup(const char* input, size_t page, NativePinyinPage& result) {
    result = NativePinyinPage();
    result.page = page;
    native_pinyin_detail::Input normalized;
    if (!native_pinyin_detail::normalize(input, normalized)) return false;
    result.valid = true;
    if (!normalized.length) return true;
    const bool pageFits = page <= size_t(-1) / kNativePinyinPageSize;
    const size_t first = pageFits ? page * kNativePinyinPageSize : size_t(-1);
    for (size_t length = normalized.length; length > 0; --length) {
      const char* group = native_pinyin_detail::groupFor(normalized.letters, length);
      if (!group) continue;
      size_t offset = native_pinyin_detail::read24(group + 3);
      const size_t count = native_pinyin_detail::read16(group + 6);
      for (size_t i = 0; i < count; ++i) {
        if (offset + 3 > sizeof(native_pinyin_data::kCandidates) - 1) return false;
        const auto* record = reinterpret_cast<const uint8_t*>(native_pinyin_data::kCandidates + offset);
        const size_t bytes = record[0];
        if (!bytes || bytes >= kNativePinyinCandidateBytes || offset + 3 + bytes > sizeof(native_pinyin_data::kCandidates) - 1)
          return false;
        uint8_t syllables;
        const bool matches = native_pinyin_detail::boundariesMatch(
            native_pinyin_detail::read16(reinterpret_cast<const char*>(record + 1)), normalized, length, syllables);
        if (matches) {
          if (pageFits && result.total >= first && result.count < kNativePinyinPageSize) {
            auto& candidate = result.candidates[result.count++];
            std::memcpy(candidate.text, record + 3, bytes);
            candidate.text[bytes] = 0;
            candidate.consumed_bytes = normalized.consumed[length];
            candidate.syllables = syllables;
            candidate.complete = length == normalized.length;
          }
          ++result.total;
        }
        offset += 3 + bytes;
      }
    }
    result.has_next = pageFits && first < result.total && result.total - first > result.count;
    return true;
  }
};
