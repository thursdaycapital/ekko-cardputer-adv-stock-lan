#include "PinyinDictionary.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
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

extern bool pinyinSecondTranslationUnit();

static NativePinyinPage lookup(const char* input, size_t page = 0) {
  NativePinyinPage result;
  assert(NativePinyinDictionary::lookup(input, page, result));
  assert(result.valid && result.page == page && result.count <= 5);
  return result;
}
static void equivalent(const char* first, const char* second) {
  std::vector<std::string> a, b;
  for (const char* input : {first, second}) {
    auto& words = input == first ? a : b;
    for (size_t page = 0;; ++page) {
      auto result = lookup(input, page);
      for (size_t i = 0; i < result.count; ++i) {
        const auto& candidate = result.candidates[i];
        if (candidate.complete) {
          words.emplace_back(candidate.text);
          assert(candidate.consumed_bytes == std::strlen(input));
        }
      }
      if (!result.has_next) break;
    }
  }
  assert(!a.empty() && a == b);
}
static void behavior() {
  auto full = lookup("nihao");
  assert(!std::strcmp(full.candidates[0].text, "你好"));
  assert(full.candidates[0].complete && full.candidates[0].syllables == 2 && full.candidates[0].consumed_bytes == 5);
  auto chinese = lookup("zhongwen");
  assert(!std::strcmp(chinese.candidates[0].text, "中文"));
  auto prefix = lookup("nihaozzz");
  assert(!std::strcmp(prefix.candidates[0].text, "你好"));
  assert(!prefix.candidates[0].complete && prefix.candidates[0].consumed_bytes == 5);
  auto partial = lookup("zhongw");
  assert(partial.count && !partial.candidates[0].complete && partial.candidates[0].consumed_bytes == 5);
  equivalent("nihao", "NiHao");
  equivalent("lv", "lü");
  equivalent("lv", "LÜ");
  equivalent("lv", "lu:");
  equivalent("lue", "lve");
  equivalent("nue", "nve");
  equivalent("ju", "jv");
  auto umlautWord = lookup("nver");
  assert(umlautWord.candidates[0].complete && !std::strcmp(umlautWord.candidates[0].text, "女儿"));
  equivalent("nver", "nv'er");
  auto separated = lookup(" ni'hao  ");
  assert(!std::strcmp(separated.candidates[0].text, "你好"));
  assert(separated.candidates[0].consumed_bytes == 9);
  auto separatedPrefix = lookup("ni'hao ma");
  assert(!std::strcmp(separatedPrefix.candidates[0].text, "你好"));
  assert(separatedPrefix.candidates[0].consumed_bytes == 7);
  auto ambiguity = lookup("xi'an");
  bool found = false;
  for (size_t page = 0;; ++page) {
    auto candidates = lookup("xi'an", page);
    for (size_t i = 0; i < candidates.count; ++i) {
      if (candidates.candidates[i].complete) assert(candidates.candidates[i].syllables == 2);
      if (!std::strcmp(candidates.candidates[i].text, "西安")) found = true;
    }
    if (!candidates.has_next) break;
  }
  assert(ambiguity.count && found);
  auto first = lookup("ni"), next = lookup("ni", 1);
  assert(first.count == 5 && first.has_next && next.count == 5 && first.total == next.total);
  assert(std::strcmp(first.candidates[0].text, next.candidates[0].text));
  auto huge = lookup("ni", std::numeric_limits<size_t>::max());
  assert(!huge.count && !huge.has_next && huge.total == first.total);
  NativePinyinPage result;
  assert(!NativePinyinDictionary::lookup(nullptr, 0, result) && !result.valid);
  for (const char* invalid : {"ni3hao", "你", "n\t", "x-ian", "\xC3", "\xC3\xFF", "\xFF", ":", "u::"}) {
    result = full;
    assert(!NativePinyinDictionary::lookup(invalid, 0, result) && !result.valid && !result.count && !result.total);
  }
  char longInput[65];
  std::memset(longInput, 'a', 64); longInput[64] = 0;
  assert(!NativePinyinDictionary::lookup(longInput, 0, result));
  longInput[63] = 0;
  assert(NativePinyinDictionary::lookup(longInput, 0, result));
  assert(lookup("").count == 0 && lookup("  ''").count == 0);
  assert(pinyinSecondTranslationUnit());
}

static void fullSourceOracle(const char* path) {
  std::ifstream stream(path);
  assert(stream);
  std::string line;
  size_t groups = 0, entries = 0;
  while (std::getline(stream, line)) {
    std::istringstream fields(line);
    std::string key, expected, syllables;
    assert(std::getline(fields, key, '\t'));
    std::vector<std::pair<std::string, unsigned>> words;
    while (std::getline(fields, expected, '\t')) {
      assert(std::getline(fields, syllables, '\t'));
      words.emplace_back(expected, static_cast<unsigned>(std::stoul(syllables)));
    }
    size_t seen = 0;
    for (size_t page = 0; seen < words.size(); ++page) {
      auto result = lookup(key.c_str(), page);
      assert(result.count && page < 500);
      for (size_t i = 0; i < result.count && seen < words.size(); ++i) {
        const auto& candidate = result.candidates[i];
        assert(candidate.complete);
        assert(candidate.text == words[seen].first && candidate.syllables == words[seen].second);
        assert(candidate.consumed_bytes == key.size());
        ++seen; ++entries;
      }
    }
    ++groups;
  }
  assert(groups == native_pinyin_data::kGroupCount && entries == native_pinyin_data::kEntryCount);
  std::cout << "Full original-source oracle: " << groups << " key groups / " << entries << " candidate readings passed\n";
}
static void zeroHeap() {
  const size_t before = allocations;
  NativePinyinPage page;
  for (size_t i = 0; i < 300; ++i) {
    assert(NativePinyinDictionary::lookup("nihaozhongwen", i % 4, page));
    assert(NativePinyinDictionary::lookup("lü", 0, page));
  }
  assert(before == allocations);
}
int main(int argc, char** argv) {
  assert(argc == 2);
  behavior(); fullSourceOracle(argv[1]); zeroHeap();
  assert(native_pinyin_data::kDataBytes <= 760 * 1024);
  std::cout << "Pinyin: UTF8/explicit syllables/v-aliases/pagination/prefix/bounds/two-TU/zero-heap passed; page="
            << sizeof(NativePinyinPage) << " bytes; dictionary=" << native_pinyin_data::kDataBytes << " bytes\n";
}
