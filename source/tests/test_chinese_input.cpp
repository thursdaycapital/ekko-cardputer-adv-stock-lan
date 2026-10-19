#include "ChineseInput.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>

static bool countAllocations = false;
static size_t allocations = 0;
void* operator new(size_t size) {
  if (countAllocations) ++allocations;
  if (void* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }

static NativeChineseInputResult key(NativeChineseInput& ime, NativeChatInput& draft,
                                    NativeChatKeyKind kind, char character = 0) {
  NativeChatKey event; event.kind = kind; event.character = character;
  return ime.handle(event, draft);
}
static NativeChineseInputResult character(NativeChineseInput& ime, NativeChatInput& draft, char value) {
  return key(ime, draft, NativeChatKeyKind::Insert, value);
}
static void type(NativeChineseInput& ime, NativeChatInput& draft, const char* text) {
  for (const char* value = text; *value; ++value) {
    const auto result = character(ime, draft, *value);
    assert(result.handled && result.edit == NativeChatEditResult::Ok);
  }
}
static void sameText(const char* actual, const char* expected) { assert(std::strcmp(actual, expected) == 0); }

static void normalConversation() {
  NativeChineseInput ime; NativeChatInput draft;
  assert(ime.enabled() && !ime.composing() && ime.page() == 0 && ime.candidates().count == 0);
  assert(sizeof(ime) <= 384);
  type(ime, draft, "nihao");
  assert(draft.empty() && ime.composing());
  sameText(ime.preedit(), "nihao");
  sameText(ime.candidates().candidates[0].text, "你好");
  const auto confirmation = key(ime, draft, NativeChatKeyKind::Send);
  assert(confirmation.handled && confirmation.edit == NativeChatEditResult::Ok);
  assert(!ime.composing()); sameText(draft.data(), "你好");
  // Only a second, distinct Enter reaches main's send action.
  assert(!key(ime, draft, NativeChatKeyKind::Send).handled);
  type(ime, draft, "ni'hao");
  assert(character(ime, draft, ' ').handled);
  sameText(draft.data(), "你好你好");
  type(ime, draft, "nihao");
  assert(key(ime, draft, NativeChatKeyKind::Newline).handled);
  assert(!ime.composing() && !key(ime, draft, NativeChatKeyKind::Newline).handled);
  sameText(draft.data(), "你好你好你好");
}

static void pagesAndPrefix() {
  NativeChineseInput ime; NativeChatInput draft;
  type(ime, draft, "ni");
  assert(ime.candidates().count == kNativePinyinPageSize && ime.candidates().has_next);
  char first[kNativePinyinCandidateBytes]{};
  std::strcpy(first, ime.candidates().candidates[0].text);
  assert(character(ime, draft, ']').handled && ime.page() == 1);
  char chosen[kNativePinyinCandidateBytes]{};
  std::strcpy(chosen, ime.candidates().candidates[0].text);
  assert(std::strcmp(first, chosen) != 0);
  assert(key(ime, draft, NativeChatKeyKind::Left).handled && ime.page() == 0);
  assert(key(ime, draft, NativeChatKeyKind::Right).handled && ime.page() == 1);
  assert(character(ime, draft, '1').handled);
  sameText(draft.data(), chosen); assert(!ime.composing());
  assert(!character(ime, draft, '1').handled);
  assert(!character(ime, draft, '[').handled && !character(ime, draft, ']').handled);

  // Number keys select their displayed row, including non-first choices.
  for (size_t number = 1; number <= kNativePinyinPageSize; ++number) {
    ime.reset(); draft.clear(); type(ime, draft, "ni");
    std::strcpy(chosen, ime.candidates().candidates[number - 1].text);
    assert(character(ime, draft, static_cast<char>('0' + number)).handled);
    sameText(draft.data(), chosen); assert(!ime.composing());
  }

  ime.reset(); draft.clear(); type(ime, draft, "ni");
  assert(character(ime, draft, '[').handled && ime.page() == 0);
  assert(ime.feedback() == NativeChineseInputFeedback::NoPreviousPage);
  while (ime.candidates().has_next) character(ime, draft, ']');
  const size_t lastPage = ime.page();
  assert(character(ime, draft, ']').handled && ime.page() == lastPage);
  assert(ime.feedback() == NativeChineseInputFeedback::NoNextPage && ime.composing() && draft.empty());
  if (ime.candidates().count < kNativePinyinPageSize) {
    assert(character(ime, draft, static_cast<char>('1' + ime.candidates().count)).handled);
    assert(ime.feedback() == NativeChineseInputFeedback::NoCandidate && draft.empty());
  }
  assert(character(ime, draft, 'h').handled && ime.page() == 0);
  assert(ime.feedback() == NativeChineseInputFeedback::None);

  ime.reset(); draft.clear(); type(ime, draft, "nihaozzz");
  const auto& prefix = ime.candidates().candidates[0];
  assert(ime.candidates().count && prefix.consumed_bytes == 5 && !prefix.complete);
  sameText(prefix.text, "你好");
  assert(character(ime, draft, ' ').handled);
  sameText(draft.data(), "你好"); sameText(ime.preedit(), "zzz");
  assert(ime.composing() && ime.page() == 0 && !ime.candidates().count);
  assert(key(ime, draft, NativeChatKeyKind::Send).handled);
  sameText(draft.data(), "你好zzz"); assert(!ime.composing());
}

static void noCandidateAndBoundaries() {
  NativeChineseInput ime; NativeChatInput draft;
  type(ime, draft, "zzzzzz");
  assert(!ime.candidates().count);
  const uint32_t before = ime.revision();
  assert(character(ime, draft, ' ').handled);
  assert(ime.feedback() == NativeChineseInputFeedback::NoCandidate && ime.revision() != before);
  assert(draft.empty()); sameText(ime.preedit(), "zzzzzz");
  assert(character(ime, draft, '5').handled);
  assert(draft.empty()); sameText(ime.preedit(), "zzzzzz");
  assert(key(ime, draft, NativeChatKeyKind::Send).handled);
  sameText(draft.data(), "zzzzzz"); assert(!ime.composing());

  // Incomplete spelling must survive a failed raw Enter just like a word.
  ime.reset(); draft.set(std::string(kNativeChatDraftBytes - 2, 'x').c_str());
  type(ime, draft, "zzz");
  assert(key(ime, draft, NativeChatKeyKind::Send).edit == NativeChatEditResult::Full);
  assert(draft.size() == kNativeChatDraftBytes - 2 && ime.composing());
  sameText(ime.preedit(), "zzz");
  draft.backspace();
  assert(key(ime, draft, NativeChatKeyKind::Send).edit == NativeChatEditResult::Ok);
  assert(!ime.composing() && draft.size() == kNativeChatDraftBytes);
  sameText(draft.data() + draft.size() - 3, "zzz");

  ime.reset(); draft.clear();
  for (size_t i = 0; i < kNativePinyinMaxInputBytes; ++i) assert(character(ime, draft, 'z').handled);
  const std::string full(ime.preedit());
  const auto overflow = character(ime, draft, 'a');
  assert(overflow.handled && overflow.edit == NativeChatEditResult::Full);
  assert(ime.feedback() == NativeChineseInputFeedback::PreeditFull);
  sameText(ime.preedit(), full.c_str()); assert(draft.empty());
  assert(key(ime, draft, NativeChatKeyKind::Backspace).handled);
  assert(std::strlen(ime.preedit()) == kNativePinyinMaxInputBytes - 1);
  assert(character(ime, draft, 'a').edit == NativeChatEditResult::Ok);
  assert(key(ime, draft, NativeChatKeyKind::Send).handled && !ime.composing());
  assert(draft.size() == kNativePinyinMaxInputBytes);
}

static void atomicCapacity() {
  NativeChineseInput ime; NativeChatInput draft;
  const std::string nearlyFull(kNativeChatDraftBytes - 5, 'x');
  draft.set(nearlyFull.c_str()); type(ime, draft, "nihao");
  const uint32_t draftRevision = draft.revision();
  const size_t cursor = draft.cursor();
  const auto failed = character(ime, draft, ' ');
  assert(failed.handled && failed.edit == NativeChatEditResult::Full);
  assert(ime.feedback() == NativeChineseInputFeedback::DraftFull);
  sameText(draft.data(), nearlyFull.c_str()); sameText(ime.preedit(), "nihao");
  assert(draft.revision() == draftRevision && draft.cursor() == cursor);
  draft.backspace();
  assert(character(ime, draft, ' ').edit == NativeChatEditResult::Ok);
  assert(draft.size() == kNativeChatDraftBytes && !ime.composing());
  sameText(draft.data() + draft.size() - 6, "你好");

  ime.reset(); draft.set(std::string(kNativeChatDraftBytes - 4, 'x').c_str());
  type(ime, draft, "NiHao");
  assert(key(ime, draft, NativeChatKeyKind::ToggleChinese).edit == NativeChatEditResult::Full);
  assert(ime.enabled() && ime.composing()); sameText(ime.preedit(), "NiHao");
  draft.backspace();
  assert(key(ime, draft, NativeChatKeyKind::ToggleChinese).edit == NativeChatEditResult::Ok);
  assert(!ime.enabled() && !ime.composing() && draft.size() == kNativeChatDraftBytes);
  sameText(draft.data() + draft.size() - 5, "NiHao");

  ime.reset(); draft.set(std::string(kNativeChatDraftBytes - 6, 'x').c_str());
  type(ime, draft, "nihao");
  assert(character(ime, draft, '!').edit == NativeChatEditResult::Full);
  assert(draft.size() == kNativeChatDraftBytes - 6); sameText(ime.preedit(), "nihao");
  draft.backspace();
  assert(character(ime, draft, '!').edit == NativeChatEditResult::Ok);
  assert(!ime.composing() && draft.size() == kNativeChatDraftBytes);
  sameText(draft.data() + draft.size() - 7, "你好!");

  ime.reset(); draft.set("A中B"); draft.setCursor(1); type(ime, draft, "nihao");
  assert(key(ime, draft, NativeChatKeyKind::Send).edit == NativeChatEditResult::Ok);
  sameText(draft.data(), "A你好中B"); assert(draft.cursor() == 7);
}

static void contextEditingAndEnglish() {
  std::array<NativeChineseInput, 16> inputs{};
  std::array<NativeChatInput, 16> drafts{};
  for (size_t i = 0; i < inputs.size(); ++i) type(inputs[i], drafts[i], i % 2 ? "NiHao" : "ni");
  assert(key(inputs[3], drafts[3], NativeChatKeyKind::Send).handled);
  assert(!inputs[3].composing()); sameText(drafts[3].data(), "你好");
  for (size_t i = 0; i < inputs.size(); ++i) {
    if (i == 3) continue;
    assert(inputs[i].composing() && drafts[i].empty());
    sameText(inputs[i].preedit(), i % 2 ? "NiHao" : "ni");
  }
  auto& ime = inputs[0]; auto& draft = drafts[0];
  assert(!key(ime, draft, NativeChatKeyKind::ToggleHistory).handled);
  sameText(ime.preedit(), "ni");
  assert(key(ime, draft, NativeChatKeyKind::Delete).handled);
  sameText(ime.preedit(), "ni");
  assert(key(ime, draft, NativeChatKeyKind::Backspace).handled); sameText(ime.preedit(), "n");
  assert(key(ime, draft, NativeChatKeyKind::Backspace).handled && !ime.composing());
  assert(!key(ime, draft, NativeChatKeyKind::Backspace).handled);
  assert(key(ime, draft, NativeChatKeyKind::ToggleChinese).handled && !ime.enabled());
  const uint32_t revision = ime.revision();
  assert(!character(ime, draft, 'a').handled && !key(ime, draft, NativeChatKeyKind::Send).handled);
  assert(ime.revision() == revision && draft.empty());
  ime.reset(); assert(ime.enabled() && !ime.composing());
  type(ime, draft, "ni");
  assert(!key(ime, draft, NativeChatKeyKind::Home).handled);
  assert(!ime.composing()); sameText(draft.data(), "ni");
}

static void zeroHeap() {
  NativeChineseInput ime; NativeChatInput draft;
  allocations = 0; countAllocations = true;
  for (unsigned repeat = 0; repeat < 24; ++repeat) {
    ime.reset(); draft.clear(); type(ime, draft, "ni");
    character(ime, draft, ']'); character(ime, draft, '[');
    character(ime, draft, '1');
    type(ime, draft, "nihaozzz"); character(ime, draft, ' ');
    key(ime, draft, NativeChatKeyKind::ToggleChinese);
    key(ime, draft, NativeChatKeyKind::ToggleChinese);
    type(ime, draft, "nihao"); character(ime, draft, '!');
  }
  countAllocations = false;
  assert(allocations == 0);
}

int main() {
  normalConversation(); pagesAndPrefix(); noCandidateAndBoundaries(); atomicCapacity();
  contextEditingAndEnglish(); zeroHeap();
  std::cout << "Chinese input: real dictionary/selection/prefix/pages/Enter/atomic capacity/contexts/zero heap passed; sizeof="
            << sizeof(NativeChineseInput) << '\n';
}
