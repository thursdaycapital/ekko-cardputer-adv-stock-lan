#include "ChatPlainText.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

static bool forbidAllocation = false;
void* operator new(std::size_t size) {
  if (forbidAllocation) std::abort();
  if (void* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }

static size_t checks = 0;
static void expect(const char* source, const char* expected,
                   NativeChatPlainTextOptions options = NativeChatPlainTextOptions()) {
  char output[1025];
  const size_t length = std::strlen(source);
  forbidAllocation = true;
  const auto result = nativeChatPlainText(source, length, output, sizeof(output), options);
  forbidAllocation = false;
  if (!result.valid || result.truncated || std::strcmp(output, expected) != 0) {
    std::fprintf(stderr, "input: <%s>\nexpected: <%s>\nactual: <%s>\n", source, expected, output);
    std::abort();
  }
  assert(result.bytes == std::strlen(output));
  assert(result.bytes <= length);
  assert(native_chat::validUtf8(output, result.bytes));
  ++checks;
}

static void prose() {
  expect("你好，**重点**。", "你好，重点。");
  expect("这是**重点**说明", "这是重点说明");
  expect("**bold** *emphasis* __strong__ _em_ ~~old~~", "bold emphasis strong em old");
  expect("***both***", "both");
  expect("**bold *nested* text**", "bold nested text");
  expect("*outer **bold** tail*", "outer bold tail");
  expect("**outer _inner_**", "outer inner");
  expect("***bold**", "*bold");
  expect("**two\nlines**", "two\nlines");
  expect("**separate\n\nblocks**", "**separate\n\nblocks**");
  expect("a*b*c, a_b_c, 2**3**4, a * b * c", "a*b*c, a_b_c, 2**3**4, a * b * c");
  expect("α*β*γ α**β**γ", "α*β*γ α**β**γ");
  expect("*x* + *y* = 4", "x + y = 4");
  expect("abc_def_ghi file_name.txt foo__bar__baz", "abc_def_ghi file_name.txt foo__bar__baz");
  expect("*未完成 **也是 ~~未完成", "*未完成 **也是 ~~未完成");
  expect("中文*乘号*文字", "中文*乘号*文字");
  expect("a ~~b~~ c ~literal~", "a b c ~literal~");
  expect("\\*literal\\* \\_snake\\_ \\#tag \\normal", "*literal* _snake_ #tag \\normal");
  expect("\\**not bold**", "*not bold*");
  expect("emoji **😀你好**！", "emoji 😀你好！");
  expect("（**重点**）", "（重点）");
}

static void blocks() {
  expect("# 标题\n## 第二个 ##\n正文", "标题\n第二个\n正文");
  expect("#hashtag\n####### too many\n>10 is math", "#hashtag\n####### too many\n>10 is math");
  expect("> **引用**\n> > 二级\n* 第一条\n- 第二条\n+ 第三条", "引用\n二级\n第一条\n第二条\n第三条");
  expect("1. 第一条\n2. 第二条", "1. 第一条\n2. 第二条");
  expect("* * *\n---\n+ + +", "* * *\n---\n+ + +");
  expect("    a**b**c\n\t**code**", "    a**b**c\n\t**code**");
  expect("```python\na**b**c\n_x_\n```\n**结束**", "a**b**c\n_x_\n结束");
  expect("~~~txt\n# code\n> raw\n~~~~\ntext", "# code\n> raw\ntext");
  expect("```\n**unclosed fence body**", "**unclosed fence body**");
  expect("```", "```");
  expect("````\n```\n**code**\n````", "```\n**code**\n");
  expect("```\r\na*b\r\n```\r\nend", "a*b\r\nend");
  expect("> ```python\n> **literal**\n> _code_\n> ```\n**end**", "**literal**\n_code_\nend");
  expect("- ```\n  **literal**\n  ```\n**end**", "  **literal**\nend");
  expect("1. ```\n   a**b**c\n   ```", "   a**b**c\n");
  expect("> > ```\n> > **literal**\n> > ```", "**literal**\n");
  expect(">     **code**\n> **prose**", "    **code**\nprose");
}

static void codeAndLinks() {
  expect("`a**b** _c_` and **yes**", "a**b** _c_ and yes");
  expect("``a ` b``", "a ` b");
  expect("` x ` `   `", "x    ");
  expect("`a\r\nb`", "a b");
  expect("`a\nb`", "a b");
  expect("`unclosed **code**", "`unclosed **code**");
  expect("**code `a*b*c` works**", "code a*b*c works");
  expect("[文档](https://example.com/a)", "文档 (https://example.com/a)");
  expect("[**加粗标签**](https://example.com/a*b_c)", "加粗标签 (https://example.com/a*b_c)");
  expect("[nested [label]](https://x/a(b)c)", "nested [label] (https://x/a(b)c)");
  expect("[x](<https://x/a(b)> \"title (ok)\")", "x (<https://x/a(b)> \"title (ok)\")");
  expect("![头像](https://x/image.png)", "头像 (https://x/image.png)");
  expect("[https://example.com](https://example.com)", "https://example.com");
  expect("[](https://example.com)", "https://example.com");
  expect("[label]()", "label");
  expect("[unfinished](https://x/a", "[unfinished](https://x/a");
  expect("[label][ref]", "[label][ref]");
  expect("<b>HTML stays</b>", "<b>HTML stays</b>");
  expect("`[code](url)`", "[code](url)");
  NativeChatPlainTextOptions labels;
  labels.keepLinkUrls = false;
  expect("[label](https://example.com)", "label", labels);
}

static void windowsAndStreaming() {
  NativeChatPlainTextOptions options;
  options.truncatedBefore = true;
  expect("**could be code**\n# not necessarily a heading", "**could be code**\n# not necessarily a heading", options);
  char output[1025];
  const auto middle = nativeChatPlainText("**a**", 5, output, sizeof(output), options);
  assert(middle.valid && middle.contextAmbiguous);
  options.truncatedBefore = false;
  options.truncatedAfter = true;
  expect("# incomplete heading", "# incomplete heading", options);
  expect("# complete heading\n**complete** and *unfinished", "complete heading\ncomplete and *unfinished", options);
  expect("```\n**code**\n```", "**code**\n```", options);
  expect("text **complete** tail", "text complete tail", options);
  const char* sample = "## 流式\n**hello 😀** `a**b` [label](https://x) *tail*";
  const size_t length = std::strlen(sample);
  for (size_t i = 0; i <= length; ++i) {
    if (!native_chat::validUtf8(sample, i)) continue;
    forbidAllocation = true;
    const auto result = nativeChatPlainText(sample, i, output, sizeof(output), options);
    forbidAllocation = false;
    assert(result.valid && result.bytes <= i && !result.truncated);
    assert(native_chat::validUtf8(output, result.bytes));
    ++checks;
  }
}

static void boundsAndInPlace() {
  const char* text = "**你好😀**abc";
  const size_t n = std::strlen(text);
  for (size_t capacity = 1; capacity < 24; ++capacity) {
    char guarded[32];
    std::memset(guarded, 0x5A, sizeof(guarded));
    const auto result = nativeChatPlainText(text, n, guarded + 1, capacity);
    assert(result.valid && result.bytes < capacity);
    assert(guarded[0] == 0x5A && guarded[capacity + 1] == 0x5A);
    assert(guarded[result.bytes + 1] == 0);
    assert(native_chat::validUtf8(guarded + 1, result.bytes));
    ++checks;
  }
  char inPlace[1025] = "# title\n**hello** `[a](b)` [x](https://x)";
  const auto inPlaceResult = nativeChatPlainText(inPlace, std::strlen(inPlace), inPlace, sizeof(inPlace));
  assert(inPlaceResult.valid && std::strcmp(inPlace, "title\nhello [a](b) x (https://x)") == 0);
  std::string full(1024, 'x');
  char output[1025];
  auto result = nativeChatPlainText(full.data(), full.size(), output, sizeof(output));
  assert(result.valid && result.bytes == 1024 && !result.truncated);
  assert(std::memcmp(output, full.data(), full.size()) == 0);
  full.push_back('x');
  result = nativeChatPlainText(full.data(), full.size(), output, sizeof(output));
  assert(!result.valid && output[0] == 0);
  const char invalid[] = {'a', static_cast<char>(0xF0), static_cast<char>(0x9F)};
  assert(!nativeChatPlainText(invalid, sizeof(invalid), output, sizeof(output)).valid);
  const char nul[] = {'a', 0, 'b'};
  assert(!nativeChatPlainText(nul, sizeof(nul), output, sizeof(output)).valid);
  assert(nativeChatPlainText(nullptr, 0, output, sizeof(output)).valid && output[0] == 0);
  assert(!nativeChatPlainText("x", 1, nullptr, 0).valid);
  std::string brackets(1024, '[');
  result = nativeChatPlainText(brackets.data(), brackets.size(), output, sizeof(output));
  assert(result.valid && result.contextAmbiguous && result.bytes == brackets.size());
  assert(std::memcmp(output, brackets.data(), brackets.size()) == 0);
  std::string openers;
  for (size_t i = 0; i < 40; ++i) openers += "*a ";
  result = nativeChatPlainText(openers.data(), openers.size(), output, sizeof(output));
  assert(result.valid && result.contextAmbiguous && result.bytes == openers.size());
  ++checks;
}

static void randomBoundedInputs() {
  static const char* atoms[] = {"a", " ", "*", "_", "~", "`", "[", "]", "(", ")", "\\", "\n", "#", ">", "你", "😀"};
  uint32_t random = 0x917AB31;
  char output[1025];
  for (size_t trial = 0; trial < 5000; ++trial) {
    std::string input;
    const size_t target = trial % 1025;
    while (input.size() < target) {
      random = random * 1664525U + 1013904223U;
      const char* atom = atoms[(random >> 16) % (sizeof(atoms) / sizeof(atoms[0]))];
      if (input.size() + std::strlen(atom) <= target) input += atom;
      else input += 'x';
    }
    NativeChatPlainTextOptions options;
    options.truncatedBefore = trial % 11 == 0;
    options.truncatedAfter = trial % 3 == 0;
    options.keepLinkUrls = trial % 2 == 0;
    forbidAllocation = true;
    const auto result = nativeChatPlainText(input.data(), input.size(), output, sizeof(output), options);
    forbidAllocation = false;
    assert(result.valid && !result.truncated && result.bytes <= input.size());
    assert(output[result.bytes] == 0 && native_chat::validUtf8(output, result.bytes));
    ++checks;
  }
}

int main() {
  prose();
  blocks();
  codeAndLinks();
  windowsAndStreaming();
  boundsAndInPlace();
  randomBoundedInputs();
  std::printf("chat plain text: %zu checks passed; scratch=%zu bytes, no recursion/heap\n", checks,
    sizeof(native_chat_plain::Scratch));
}
