#pragma once
#include "ChatUtf8.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

struct NativeChatPlainTextOptions {
  bool truncatedBefore = false;
  bool truncatedAfter = false;
  bool keepLinkUrls = true;
};

struct NativeChatPlainTextResult {
  size_t bytes;
  bool valid;
  bool truncated;
  bool contextAmbiguous;
};

// A conservative, display-only Markdown subset. The original message and its
// UTF-8 offsets must remain unchanged. Unknown syntax is kept, not discarded.
namespace native_chat_plain {
static const size_t kMaxBytes = 1024;
static const size_t kMaxDelimiters = 32;
enum Action : uint8_t { Keep = 0, Drop = 1, Space = 2 };

struct Delimiter {
  uint16_t position;
  uint16_t remaining;
  uint16_t original;
  uint8_t marker;
  bool canClose;
};

struct Scratch {
  uint8_t actions[kMaxBytes / 4];
  uint8_t literal[kMaxBytes / 8];
  uint8_t boundary[kMaxBytes / 8];
  Delimiter delimiters[kMaxDelimiters];
};
static_assert(sizeof(Delimiter) == 8, "bounded delimiter storage");
static_assert(sizeof(Scratch) == 768, "formatter scratch must remain bounded");

inline bool white(uint32_t c) {
  return c == 0 || c == ' ' || (c >= '\t' && c <= '\r') || c == 0x85 ||
    c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
    c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
inline bool punctuation(uint32_t c) {
  return (c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
    (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E) ||
    (c >= 0x2000 && c <= 0x206F) || (c >= 0x3001 && c <= 0x303F) ||
    (c >= 0xFF01 && c <= 0xFF0F) || (c >= 0xFF1A && c <= 0xFF20) ||
    (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65);
}
inline bool asciiWord(uint32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
    (c >= '0' && c <= '9') || c == '_';
}
inline bool word(uint32_t c) {
  return asciiWord(c) || (c >= 0x80 && !white(c) && !punctuation(c));
}
inline bool cjkWord(uint32_t c) {
  return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x9FFF) ||
    (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
    (c >= 0x20000 && c <= 0x323AF);
}
inline uint32_t point(const char* s, size_t n, size_t i) {
  if (i >= n) return 0;
  const uint8_t c = static_cast<uint8_t>(s[i]);
  const size_t width = native_chat::codepointBytes(c);
  uint32_t value = width == 1 ? c : c & (0x7F >> width);
  for (size_t j = 1; j < width; ++j)
    value = (value << 6) | (static_cast<uint8_t>(s[i + j]) & 0x3F);
  return value;
}
inline uint32_t previous(const char* s, size_t n, size_t i) {
  if (!i) return 0;
  --i;
  while (i && (static_cast<uint8_t>(s[i]) & 0xC0) == 0x80) --i;
  return point(s, n, i);
}

struct Parser {
  const char* source;
  size_t length;
  const NativeChatPlainTextOptions& options;
  Scratch scratch;
  size_t depth;
  size_t fuel;
  bool exhausted;

  Parser(const char* s, size_t n, const NativeChatPlainTextOptions& o)
    : source(s), length(n), options(o), depth(0), fuel(n * 8 + 1), exhausted(false) {
    std::memset(&scratch, 0, sizeof(scratch));
  }
  Action action(size_t i) const {
    return static_cast<Action>((scratch.actions[i / 4] >> ((i % 4) * 2)) & 3);
  }
  void set(size_t i, Action a) {
    const unsigned shift = static_cast<unsigned>((i % 4) * 2);
    scratch.actions[i / 4] = static_cast<uint8_t>(
      (scratch.actions[i / 4] & ~(3U << shift)) | (static_cast<unsigned>(a) << shift));
  }
  void drop(size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) set(i, Drop);
  }
  bool literal(size_t i) const {
    return (scratch.literal[i / 8] & (1U << (i % 8))) != 0;
  }
  void protect(size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i)
      scratch.literal[i / 8] |= static_cast<uint8_t>(1U << (i % 8));
  }
  void boundary(size_t i) {
    if (i < length) scratch.boundary[i / 8] |= static_cast<uint8_t>(1U << (i % 8));
  }
  bool isBoundary(size_t i) const {
    return (scratch.boundary[i / 8] & (1U << (i % 8))) != 0;
  }
  bool spend() {
    if (fuel) { --fuel; return true; }
    exhausted = true;
    return false;
  }
  size_t run(size_t i, char marker, size_t end) const {
    size_t j = i;
    while (j < end && source[j] == marker) ++j;
    return j - i;
  }
  bool onlySpace(size_t begin, size_t end) const {
    for (size_t i = begin; i < end; ++i)
      if (source[i] != ' ' && source[i] != '\t' && source[i] != '\r') return false;
    return true;
  }
  size_t quotePrefix(size_t begin, size_t end, size_t limit, size_t& levels) const {
    size_t i = begin;
    levels = 0;
    while (levels < limit && i + 1 < end && source[i] == '>' &&
           (source[i + 1] == ' ' || source[i + 1] == '\t')) {
      i += 2;
      ++levels;
      size_t next = i;
      while (next < end && next - i < 3 && source[next] == ' ') ++next;
      if (levels < limit && next + 1 < end && source[next] == '>' &&
          (source[next + 1] == ' ' || source[next + 1] == '\t')) i = next;
    }
    return i;
  }
  size_t fencePrefix(size_t begin, size_t end, size_t& quotes) const {
    size_t i = quotePrefix(begin, end, kMaxBytes, quotes);
    size_t spaces = 0;
    while (i < end && spaces < 4 && source[i] == ' ') { ++i; ++spaces; }
    if (spaces == 4) return end;
    size_t markerEnd = i;
    if (i < end && (source[i] == '*' || source[i] == '-' || source[i] == '+')) {
      markerEnd = i + 1;
    } else {
      while (markerEnd < end && markerEnd - i < 9 && source[markerEnd] >= '0' && source[markerEnd] <= '9') ++markerEnd;
      if (markerEnd == i || markerEnd >= end || (source[markerEnd] != '.' && source[markerEnd] != ')')) markerEnd = i;
      else ++markerEnd;
    }
    if (markerEnd > i && markerEnd < end && (source[markerEnd] == ' ' || source[markerEnd] == '\t')) {
      i = markerEnd + 1;
      spaces = 0;
      while (i < end && spaces < 4 && source[i] == ' ') { ++i; ++spaces; }
      if (spaces == 4) return end;
    }
    return i;
  }

  void blocks() {
    char fence = 0;
    size_t fenceWidth = 0;
    size_t fenceQuotes = 0;
    for (size_t start = 0; start < length;) {
      size_t end = start;
      while (end < length && source[end] != '\n') ++end;
      const size_t next = end < length ? end + 1 : end;
      const bool complete = end < length || !options.truncatedAfter;
      size_t contentEnd = end;
      if (contentEnd > start && source[contentEnd - 1] == '\r') --contentEnd;
      size_t i = start;
      while (i < contentEnd && i - start < 4 && source[i] == ' ') ++i;
      if (fence) {
        size_t quotes = 0;
        const size_t body = quotePrefix(i, contentEnd, fenceQuotes, quotes);
        size_t marker = body;
        while (marker < contentEnd && marker - body < 4 && source[marker] == ' ') ++marker;
        const bool container = quotes == fenceQuotes && i - start <= 3 && marker - body <= 3;
        const size_t width = container ? run(marker, fence, contentEnd) : 0;
        if (complete && width >= fenceWidth && onlySpace(marker + width, contentEnd)) {
          drop(start, next);
          fence = 0;
        } else {
          protect(start, next);
          if (quotes == fenceQuotes && fenceQuotes) drop(start, body);
        }
        boundary(start);
        boundary(next);
        start = next;
        continue;
      }
      if (onlySpace(start, contentEnd)) {
        boundary(start);
        boundary(next);
        start = next;
        continue;
      }
      // Indented code is deliberately kept verbatim, including indentation.
      if (i - start == 4 || (i < contentEnd && source[i] == '\t')) {
        protect(start, next);
        boundary(start);
        boundary(next);
        start = next;
        continue;
      }
      size_t quotes = 0;
      const size_t fenceStart = fencePrefix(i, contentEnd, quotes);
      if (complete && fenceStart < contentEnd && (source[fenceStart] == '`' || source[fenceStart] == '~')) {
        const char marker = source[fenceStart];
        const size_t width = run(fenceStart, marker, contentEnd);
        bool infoValid = true;
        if (marker == '`') {
          for (size_t j = fenceStart + width; j < contentEnd; ++j)
            if (source[j] == '`') infoValid = false;
        }
        // A newline confirms that this is a fence, rather than an isolated
        // unfinished inline-code marker in a streamed tail.
        if (width >= 3 && infoValid && end < length) {
          fence = marker;
          fenceWidth = width;
          fenceQuotes = quotes;
          drop(start, next);
          boundary(start);
          boundary(next);
          start = next;
          continue;
        }
      }
      if (!complete) { start = next; continue; }
      size_t levels = 0;
      size_t prefix = quotePrefix(i, contentEnd, kMaxBytes, levels);
      if (levels) {
        drop(start, prefix);
        size_t body = prefix;
        while (body < contentEnd && body - prefix < 4 && source[body] == ' ') ++body;
        if (body - prefix == 4 || (body < contentEnd && source[body] == '\t')) {
          protect(prefix, next);
          boundary(start);
          boundary(next);
          start = next;
          continue;
        }
        drop(prefix, body);
        prefix = body;
        boundary(start);
        boundary(next);
      }
      const size_t hashes = run(prefix, '#', contentEnd);
      if (hashes && hashes <= 6 && prefix + hashes < contentEnd &&
          (source[prefix + hashes] == ' ' || source[prefix + hashes] == '\t')) {
        size_t body = prefix + hashes;
        while (body < contentEnd && (source[body] == ' ' || source[body] == '\t')) ++body;
        drop(start, body);
        size_t tail = contentEnd;
        while (tail > body && (source[tail - 1] == ' ' || source[tail - 1] == '\t')) --tail;
        const size_t hashEnd = tail;
        while (tail > body && source[tail - 1] == '#') --tail;
        if (tail < hashEnd && tail > body && white(static_cast<uint8_t>(source[tail - 1]))) {
          while (tail > body && (source[tail - 1] == ' ' || source[tail - 1] == '\t')) --tail;
          drop(tail, contentEnd);
        }
        boundary(start);
        boundary(next);
      } else if (prefix + 2 < contentEnd &&
                 (source[prefix] == '*' || source[prefix] == '-' || source[prefix] == '+') &&
                 (source[prefix + 1] == ' ' || source[prefix + 1] == '\t')) {
        size_t body = prefix + 2;
        while (body < contentEnd && (source[body] == ' ' || source[body] == '\t')) ++body;
        bool separator = true;
        for (size_t j = body; j < contentEnd; ++j)
          if (source[j] != source[prefix] && source[j] != ' ' && source[j] != '\t') separator = false;
        if (body < contentEnd && !separator) {
          drop(start, body);
          boundary(start);
          boundary(next);
        }
      }
      start = next;
    }
  }

  void codeAndEscapes() {
    for (size_t i = 0; i < length;) {
      if (literal(i) || action(i) != Keep) { ++i; continue; }
      if (source[i] == '\\' && i + 1 < length &&
          static_cast<uint8_t>(source[i + 1]) < 0x80 && punctuation(source[i + 1])) {
        set(i, Drop);
        protect(i + 1, i + 2);
        i += 2;
        continue;
      }
      if (source[i] != '`') { ++i; continue; }
      const size_t width = run(i, '`', length);
      size_t end = i + width;
      bool matched = false;
      while (end < length && !isBoundary(end)) {
        if (literal(end) || action(end) != Keep || source[end] != '`') { ++end; continue; }
        const size_t closing = run(end, '`', length);
        if (closing == width) { matched = true; break; }
        end += closing;
      }
      if (!matched) {
        // An unfinished code span may become complete in the next stream chunk.
        protect(i, end);
        i = end;
        continue;
      }
      drop(i, i + width);
      drop(end, end + width);
      const size_t begin = i + width;
      protect(begin, end);
      bool nonSpace = false;
      for (size_t j = begin; j < end; ++j) {
        if (source[j] == '\r' || source[j] == '\n') {
          set(j, Space);
          if (source[j] == '\r' && j + 1 < end && source[j + 1] == '\n') set(++j, Drop);
        } else if (source[j] != ' ') nonSpace = true;
      }
      if (nonSpace && begin < end && source[begin] == ' ' && source[end - 1] == ' ') {
        set(begin, Drop);
        set(end - 1, Drop);
      }
      i = end + width;
    }
  }

  void links() {
    for (size_t i = 0; i < length && !exhausted; ++i) {
      if (literal(i) || action(i) != Keep || source[i] != '[') continue;
      size_t close = i + 1;
      size_t brackets = 1;
      for (; close < length && !isBoundary(close); ++close) {
        if (!spend()) return;
        if (literal(close) || action(close) != Keep) continue;
        if (source[close] == '[') ++brackets;
        if (source[close] == ']' && --brackets == 0) break;
      }
      if (close + 1 >= length || source[close] != ']' || source[close + 1] != '(') continue;
      size_t end = close + 2;
      size_t parens = 1;
      char quote = 0;
      bool angle = false;
      bool destinationStart = true;
      for (; end < length && !isBoundary(end); ++end) {
        if (!spend()) return;
        const char c = source[end];
        if (c == '\\' && end + 1 < length) { destinationStart = false; ++end; continue; }
        if (quote) { if (c == quote) quote = 0; continue; }
        if (angle) { if (c == '>') angle = false; continue; }
        if (c == '<' && destinationStart) { angle = true; destinationStart = false; continue; }
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') destinationStart = false;
        if ((c == '"' || c == '\'') && end > close + 2 && white(source[end - 1])) {
          quote = c;
          continue;
        }
        if (c == '(') ++parens;
        if (c == ')' && --parens == 0) break;
      }
      if (end >= length || source[end] != ')' || parens || quote || angle) continue;
      const size_t body = i + 1;
      size_t target = close + 2;
      size_t targetEnd = end;
      while (target < targetEnd && (source[target] == ' ' || source[target] == '\t')) ++target;
      while (targetEnd > target && (source[targetEnd - 1] == ' ' || source[targetEnd - 1] == '\t')) --targetEnd;
      const bool same = close - body == targetEnd - target &&
        std::memcmp(source + body, source + target, close - body) == 0;
      if (i && source[i - 1] == '!' && action(i - 1) == Keep && !literal(i - 1)) set(i - 1, Drop);
      set(i, Drop);
      if (!options.keepLinkUrls || target == targetEnd || same) {
        drop(close, end + 1);
      } else if (body == close) {
        drop(close, close + 2);
        set(end, Drop);
        if (source[target] == '<' && source[targetEnd - 1] == '>') {
          set(target, Drop);
          set(targetEnd - 1, Drop);
        }
      } else {
        set(close, Space);
      }
      protect(close + 1, end + 1);
      i = end;
    }
  }

  void emphasis() {
    for (size_t i = 0; i < length && !exhausted;) {
      if (isBoundary(i)) depth = 0;
      if (literal(i) || action(i) != Keep ||
          (source[i] != '*' && source[i] != '_' && source[i] != '~')) { ++i; continue; }
      const char marker = source[i];
      const size_t width = run(i, marker, length);
      const uint32_t before = previous(source, length, i);
      const uint32_t after = point(source, length, i + width);
      const bool left = !white(after) && (!punctuation(after) || white(before) || punctuation(before));
      const bool right = !white(before) && (!punctuation(before) || white(after) || punctuation(after));
      bool canOpen = left && (marker != '_' || !right || punctuation(before));
      bool canClose = right && (marker != '_' || !left || punctuation(after));
      // Deliberately more conservative than CommonMark's intraword asterisk:
      // multiplication and identifiers must not silently change on this display.
      if (word(before) && word(after) &&
          (marker == '_' || width == 1 || (!cjkWord(before) && !cjkWord(after))))
        canOpen = canClose = false;
      if (marker == '~' && width != 2) canOpen = canClose = false;
      size_t consumed = 0;
      while (canClose && consumed < width) {
        size_t match = depth;
        for (size_t j = depth; j > 0; --j) {
          const Delimiter& candidate = scratch.delimiters[j - 1];
          if (candidate.marker != static_cast<uint8_t>(marker)) continue;
          if (marker != '~' && (candidate.canClose || canOpen) &&
              (candidate.original + width) % 3 == 0 &&
              (candidate.original % 3 != 0 || width % 3 != 0)) continue;
          match = j - 1;
          break;
        }
        if (match == depth) break;
        Delimiter& opener = scratch.delimiters[match];
        const size_t remaining = width - consumed;
        const size_t use = marker == '~' ? 2 : (remaining >= 2 && opener.remaining >= 2 ? 2 : 1);
        if (use > remaining || use > opener.remaining) break;
        drop(opener.position + opener.remaining - use, opener.position + opener.remaining);
        drop(i + consumed, i + consumed + use);
        opener.remaining = static_cast<uint16_t>(opener.remaining - use);
        consumed += use;
        depth = match + (opener.remaining ? 1 : 0);
      }
      if (canOpen && consumed < width) {
        if (depth == kMaxDelimiters) { exhausted = true; return; }
        Delimiter& opener = scratch.delimiters[depth++];
        opener.position = static_cast<uint16_t>(i + consumed);
        opener.remaining = static_cast<uint16_t>(width - consumed);
        opener.original = static_cast<uint16_t>(width);
        opener.marker = static_cast<uint8_t>(marker);
        opener.canClose = canClose;
      }
      i += width;
    }
  }

  NativeChatPlainTextResult emit(char* output, size_t capacity, bool raw) const {
    size_t count = 0;
    bool truncated = false;
    for (size_t i = 0; i < length;) {
      const Action a = raw ? Keep : action(i);
      const size_t width = native_chat::codepointBytes(static_cast<uint8_t>(source[i]));
      if (a == Drop) { i += width; continue; }
      const size_t needed = a == Space ? 1 : width;
      if (needed >= capacity - count) { truncated = true; break; }
      if (a == Space) output[count++] = ' ';
      else { for (size_t j = 0; j < width; ++j) output[count++] = source[i + j]; }
      i += width;
    }
    output[count] = '\0';
    return NativeChatPlainTextResult{count, true, truncated, raw};
  }
};
} // namespace native_chat_plain

// output may equal source: every supported transformation only shrinks text.
// Input beyond the native 1024-byte window or invalid UTF-8 fails explicitly.
// A middle window has unknown fence/inline state, so truncatedBefore keeps the
// entire source. Likewise, exceeding 32 delimiters or 8*n link lookahead steps
// conservatively keeps the entire source and sets contextAmbiguous. No heap,
// recursion or second text buffer; scratch is 768 bytes, work is O(32*n).
inline NativeChatPlainTextResult nativeChatPlainText(
    const char* source, size_t length, char* output, size_t capacity,
    const NativeChatPlainTextOptions& options = NativeChatPlainTextOptions()) {
  if (!output || !capacity)
    return NativeChatPlainTextResult{0, false, length != 0, false};
  if (length > native_chat_plain::kMaxBytes || !native_chat::validUtf8(source, length)) {
    output[0] = '\0';
    return NativeChatPlainTextResult{0, false, false, false};
  }
  native_chat_plain::Parser parser(source, length, options);
  if (!options.truncatedBefore) {
    parser.blocks();
    parser.codeAndEscapes();
    parser.links();
    if (!parser.exhausted) parser.emphasis();
  }
  return parser.emit(output, capacity, options.truncatedBefore || parser.exhausted);
}
