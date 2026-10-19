#include "BoundedJsonReader.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

// The production reader must never allocate; test listeners may own results.
static size_t allocationCount = 0;
void* operator new(size_t bytes) {
  ++allocationCount;
  if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
#if __cplusplus >= 201402L
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }
#endif

using namespace bounded_json;

namespace {

bool validUtf8(const char* bytes, size_t length) {
  size_t position = 0;
  while (position < length) {
    uint8_t first = static_cast<uint8_t>(bytes[position++]);
    if (first < 0x80) continue;
    size_t count;
    uint32_t scalar, minimum;
    if (first >= 0xc2 && first <= 0xdf) { count = 1; scalar = first & 0x1f; minimum = 0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count = 2; scalar = first & 0x0f; minimum = 0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count = 3; scalar = first & 0x07; minimum = 0x10000; }
    else return false;
    if (count > length - position) return false;
    while (count--) {
      const uint8_t next = static_cast<uint8_t>(bytes[position++]);
      if ((next & 0xc0) != 0x80) return false;
      scalar = (scalar << 6) | (next & 0x3f);
    }
    if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
  }
  return true;
}

std::string path(const Event& event) {
  std::string result = "$";
  for (size_t i = 0; i < event.path.size; ++i) {
    const auto& segment = event.path.segments[i];
    if (segment.kind == SegmentKind::Key) {
      result += '.';
      result.append(segment.key, segment.keyBytes);
    } else {
      char index[32];
      std::snprintf(index, sizeof(index), "[%llu]", static_cast<unsigned long long>(segment.index));
      result += index;
    }
  }
  return result;
}

struct Record {
  Type type;
  std::string path, data;
  bool boolean;
  bool operator==(const Record& right) const {
    return type == right.type && path == right.path && data == right.data && boolean == right.boolean;
  }
};

struct Capture : Listener {
  std::vector<Record> records;
  bool onEvent(const Event& event) override {
    if (event.type == Type::StringChunk || event.type == Type::Key) assert(validUtf8(event.data, event.size));
    Record record{event.type, path(event), event.data ? std::string(event.data, event.size) : std::string(), event.booleanValue};
    records.push_back(record);
    return true;
  }
  std::string text(const std::string& target, Type type = Type::StringChunk) const {
    std::string result;
    for (const auto& record : records) if (record.type == type && record.path == target) result += record.data;
    return result;
  }
  size_t count(Type type, const std::string& target) const {
    size_t result = 0;
    for (const auto& record : records) if (record.type == type && record.path == target) ++result;
    return result;
  }
};

bool parse(const std::string& json, Capture& capture, size_t step = 1) {
  Reader<> reader(capture);
  for (size_t start = 0; start < json.size();) {
    const size_t length = step < json.size() - start ? step : json.size() - start;
    if (!reader.feed(json.data() + start, length)) return false;
    start += length;
  }
  return reader.finish();
}

void structurePathsAndEveryBoundary() {
  const std::string json = R"(["message.delta",{"session_id":"s","delta":"汉字😀\n\t\b\f\r\/\\\"\u0041\uD83D\uDE00","messages":[{"content":"old","n":-0.123e+4,"b":true},null,false],"empty":{},"emptyArr":[],"":""}])";
  Capture baseline;
  assert(parse(json, baseline, json.size()));
  assert(baseline.text("$[0]") == "message.delta");
  assert(baseline.text("$[1].session_id") == "s");
  assert(baseline.text("$[1].delta") == std::string("汉字😀\n\t\b\f\r/\\\"A😀"));
  assert(baseline.text("$[1].messages[0].content") == "old");
  assert(baseline.text("$[1].messages[0].n", Type::NumberChunk) == "-0.123e+4");
  assert(baseline.count(Type::Null, "$[1].messages[1]") == 1);
  assert(baseline.count(Type::Boolean, "$[1].messages[2]") == 1);
  for (const auto& record : baseline.records) {
    if (record.type == Type::Boolean && record.path == "$[1].messages[0].b") assert(record.boolean);
    if (record.type == Type::Boolean && record.path == "$[1].messages[2]") assert(!record.boolean);
  }
  for (const auto* location : {"$", "$[1].messages", "$[1].emptyArr"}) {
    assert(baseline.count(Type::ArrayBegin, location) == baseline.count(Type::ArrayEnd, location));
  }
  for (const auto* location : {"$[1]", "$[1].messages[0]", "$[1].empty"}) {
    assert(baseline.count(Type::ObjectBegin, location) == 1 && baseline.count(Type::ObjectEnd, location) == 1);
  }
  assert(baseline.count(Type::StringBegin, "$[1].") == 1 && baseline.count(Type::StringEnd, "$[1].") == 1);
  for (size_t split = 0; split <= json.size(); ++split) {
    Capture capture;
    Reader<> reader(capture);
    assert(reader.feed(json.data(), split));
    assert(reader.feed(json.data() + split, json.size() - split));
    assert(reader.finish() && reader.complete());
    assert(reader.offset() == json.size());
    assert(capture.records == baseline.records);
    assert(reader.feed(" \n\r\t", 4) && reader.finish() && reader.finish());
    assert(reader.complete());
  }
  for (size_t step = 1; step < 19; ++step) {
    Capture capture;
    assert(parse(json, capture, step) && capture.records == baseline.records);
  }
  // Every proper prefix of this root array is incomplete, even when an inner
  // primitive is complete. EOF must not synthesize a StringEnd/container End.
  for (size_t length = 0; length < json.size(); ++length) {
    Capture capture;
    Reader<> reader(capture);
    assert(reader.feed(json.data(), length));
    const auto before = capture.records;
    assert(!reader.finish() && reader.error() == Error::UnexpectedEnd && !reader.complete());
    for (size_t i = before.size(); i < capture.records.size(); ++i) {
      assert(capture.records[i].type == Type::NumberChunk || capture.records[i].type == Type::NumberEnd);
    }
  }
}

void stringsUnicodeAndNul() {
  const std::string raw = std::string("\"\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf\"");
  Capture scalarBoundaries;
  assert(parse(raw, scalarBoundaries));
  assert(scalarBoundaries.text("$") == raw.substr(1, raw.size() - 2));
  Capture escaped;
  assert(parse(R"({"a\u0000b":"x\u0000y","escape":"\u0000\u007f\u0080\u07ff\u0800\uD7FF\uE000\uFFFF\uD800\uDC00\uDBFF\uDFFF"})", escaped));
  const std::string nulPath = std::string("$.a\0b", 5);
  assert(escaped.text(nulPath) == std::string("x\0y", 3));
  assert(escaped.count(Type::Key, nulPath) == 1);
  const std::string expected = std::string("\0", 1) + raw.substr(1, raw.size() - 2);
  assert(escaped.text("$.escape") == expected);
  const std::string manyScalars = R"("a😀字汉😀字汉😀z")";
  Capture smallChunks;
  Reader<32, 64, 4> reader(smallChunks);
  for (const char c : manyScalars) assert(reader.feed(&c, 1));
  assert(reader.finish());
  assert(smallChunks.text("$") == manyScalars.substr(1, manyScalars.size() - 2));
  for (const auto& record : smallChunks.records) if (record.type == Type::StringChunk) assert(record.data.size() <= 4);
  Capture duplicates;
  assert(parse(R"({"key":"first","key":"second"})", duplicates));
  assert(duplicates.count(Type::Key, "$.key") == 2);  // Consumers choose a duplicate-key policy.
}

void numbersAndScalars() {
  for (const char* valid : {"0", "-0", "1", "-1", "10", "-99", "0.0", "-0.01", "1e0", "1E+01", "1e-100000", "123.450e+999999", "true", "false", "null", "[]", "{}", "\"\""}) {
    Capture capture;
    assert(parse(valid, capture));
  }
  Capture number;
  Reader<> reader(number);
  assert(reader.feed("1", 1) && !reader.complete());
  assert(reader.feed("e", 1) && !reader.complete());
  assert(reader.feed("+3", 2) && !reader.complete());
  assert(reader.finish() && reader.complete());
  assert(number.text("$", Type::NumberChunk) == "1e+3");
  assert(number.count(Type::NumberEnd, "$") == 1);
  assert(reader.feed(" \t\n\r", 4));
  const auto count = number.records.size();
  assert(!reader.feed("2", 1) && reader.error() == Error::TrailingData);
  assert(number.records.size() == count);
}

void rejectInvalidSyntaxAndUnicode() {
  const std::vector<std::string> invalid = {
    "", " \r\n\t", "[", "{", "[1", "{\"a\"", "{\"a\":", "true false", "[]{}", "null0", "truex",
    "[1,]", "{\"a\":1,}", "{,}", "{:1}", "{a:1}", "{'a':1}", "{\"a\" 1}", "{\"a\":}",
    "{\"a\":1 \"b\":2}", "[1 2]", "[truefalse]", "[1:2]", "{\"a\":1]", "[{}", "[}",
    "+1", ".1", "01", "-01", "--1", "1.", "1e", "1e+", "1e-", "1e--2", "00e1", "-00", "1.e2", "1.e+2",
    "NaN", "Infinity", "-Infinity", "0x1", "1a", "0_0", "1/*comment*/", "[//comment\n1]",
    "t", "tru", "False", "nulL", "nul", "falsx", "undefined", "\xef\xbb\xbf{}", "\xc2\xa0{}",
    R"("\x00")", R"("\v")", R"("\a")", R"("\u0x00")", R"("\u123")", R"("\uDE00")",
    R"("\uD800")", R"("\uD800\n")", R"("\uD800\u0000")", R"("\uDBFF\uDBFF")", R"("\uD800uDC00")",
    std::string("\"\0\"", 3), "\"\n\"", "\"\t\"", "\"\x1f\"", "\"\x80\"", "\"\xc0\x80\"", "\"\xc1\xbf\"",
    "\"\xe0\x80\x80\"", "\"\xed\xa0\x80\"", "\"\xf0\x80\x80\x80\"", "\"\xf4\x90\x80\x80\"", "\"\xf5\x80\x80\x80\"",
    "\"\xff\"", "\"\xc2" "A\"", "\"\xe1\x80\"", "\"\xf0\x9f\x92", std::string("[0\0]", 4)
  };
  for (const auto& json : invalid) {
    for (const size_t step : {size_t(1), size_t(7), size_t(97)}) {
      Capture capture;
      Reader<> reader(capture);
      bool valid = true;
      for (size_t position = 0; position < json.size() && valid;) {
        const size_t size = step < json.size() - position ? step : json.size() - position;
        valid = reader.feed(json.data() + position, size);
        position += size;
      }
      if (valid) valid = reader.finish();
      if (valid) std::fprintf(stderr, "Unexpectedly accepted invalid JSON (%zu bytes)\n", json.size());
      assert(!valid && reader.error() != Error::None && !reader.complete());
      const auto error = reader.error();
      const auto errorOffset = reader.errorOffset();
      const auto count = capture.records.size();
      assert(!reader.feed("{}", 2) && !reader.finish());
      assert(reader.error() == error && reader.errorOffset() == errorOffset && capture.records.size() == count);
    }
  }
}

void fixedLimits() {
  Capture capture;
  Reader<32, 64, 4> reader(capture);
  const std::string exact = std::string(32, '[') + "0" + std::string(32, ']');
  assert(reader.feed(exact.data(), exact.size()) && reader.finish());
  size_t maxDepth = 0;
  for (const auto& event : capture.records) if (event.type == Type::NumberBegin) {
    for (const char c : event.path) if (c == '[') ++maxDepth;
  }
  assert(maxDepth == 32);
  assert(reader.reset());
  const std::string tooDeep = std::string(33, '[') + "0" + std::string(33, ']');
  assert(!reader.feed(tooDeep.data(), tooDeep.size()) && reader.error() == Error::DepthLimit);
  for (const auto& pair : std::vector<std::pair<std::string, bool>>{
      {"\"abcd\"", true}, {R"("\u0061bcd")", true}, {R"("a\u0000bc")", true},
      {"\"😀\"", true}, {R"("\uD83D\uDE00")", true}, {"\"abcde\"", false}, {"\"汉字\"", false}}) {
    Capture keys;
    Reader<1, 4, 4> limited(keys);
    const std::string json = "{" + pair.first + ":1}";
    const bool fed = limited.feed(json.data(), json.size());
    assert((fed && limited.finish()) == pair.second);
    if (!pair.second) assert(limited.error() == Error::KeyLimit);
  }
  Capture indices;
  Reader<32, 64, 64, 2> indexLimited(indices);
  assert(indexLimited.feed("[0,1,2]", 7) && indexLimited.finish());
  assert(indexLimited.reset());
  assert(!indexLimited.feed("[0,1,2,3]", 9) && indexLimited.error() == Error::IndexOverflow);
  Capture nullInput;
  Reader<> nullReader(nullInput);
  assert(nullReader.feed(static_cast<const uint8_t*>(nullptr), 0));
  assert(!nullReader.feed(static_cast<const uint8_t*>(nullptr), 1) && nullReader.error() == Error::NullInput);
  assert(nullReader.reset() && nullReader.feed("true", 4) && nullReader.finish());
}

void cancellationAndReentry() {
  const std::string json = R"([{"a":"long enough to flush some chunks","b":12345678901234567890,"c":true,"n":null},[]])";
  Capture all;
  assert(parse(json, all));
  for (size_t cancelAt = 1; cancelAt <= all.records.size(); ++cancelAt) {
    struct Cancel : Listener {
      size_t calls = 0, cancelAt;
      explicit Cancel(size_t at) : cancelAt(at) {}
      bool onEvent(const Event&) override { return ++calls != cancelAt; }
    } listener(cancelAt);
    Reader<> reader(listener);
    assert(!reader.feed(json.data(), json.size()) && reader.error() == Error::Cancelled);
    assert(listener.calls == cancelAt && !reader.complete());
    assert(!reader.finish() && !reader.feed(" ", 1) && listener.calls == cancelAt);
  }
  struct Reenter : Listener {
    Reader<>* reader = nullptr;
    size_t calls = 0;
    bool onEvent(const Event&) override {
      ++calls;
      assert(!reader->feed(" ", 1));
      assert(!reader->reset());
      assert(!reader->finish());
      return true;
    }
  } listener;
  Reader<> reader(listener);
  listener.reader = &reader;
  assert(!reader.feed("{}", 2) && reader.error() == Error::Reentrant && listener.calls == 1);
}

struct Monitor : Listener {
  size_t stringBytes = 0, numberBytes = 0, chunks = 0, maximumChunk = 0;
  uint64_t hash = UINT64_C(14695981039346656037);
  bool onEvent(const Event& event) override {
    if (event.type == Type::StringChunk || event.type == Type::NumberChunk) {
      ++chunks;
      if (event.size > maximumChunk) maximumChunk = event.size;
      assert(event.size <= 64);
      if (event.type == Type::StringChunk) {
        assert(validUtf8(event.data, event.size));
        stringBytes += event.size;
      } else numberBytes += event.size;
      for (size_t i = 0; i < event.size; ++i) {
        hash ^= static_cast<uint8_t>(event.data[i]);
        hash *= UINT64_C(1099511628211);
      }
    }
    return true;
  }
};

void megabyteValuesWithoutHeap() {
  Monitor monitor;
  Reader<> reader(monitor);
  char block[1021];
  std::memset(block, 'x', sizeof(block));
  const size_t repeats = 1200;
  const size_t before = allocationCount;
  assert(reader.feed("{\"output\":\"", 11));
  for (size_t i = 0; i < repeats; ++i) assert(reader.feed(block, sizeof(block)));
  constexpr const char suffix[] = "\",\"meta\":{\"n\":-1e+30}}";
  assert(reader.feed(suffix, sizeof(suffix) - 1));
  assert(reader.finish());
  assert(allocationCount == before);
  assert(monitor.stringBytes == sizeof(block) * repeats && monitor.stringBytes > 1024 * 1024);
  assert(monitor.numberBytes == 6 && monitor.maximumChunk <= 64);
  assert(sizeof(reader) < 6000);  // Includes 32 fixed-depth key/path frames.
  Monitor number;
  Reader<> numberReader(number);
  std::memset(block, '9', sizeof(block));
  const size_t numberBefore = allocationCount;
  assert(numberReader.feed("[1", 2));
  for (size_t i = 0; i < repeats; ++i) assert(numberReader.feed(block, sizeof(block)));
  assert(numberReader.feed("]", 1) && numberReader.finish());
  assert(allocationCount == numberBefore);
  assert(number.numberBytes == 1 + sizeof(block) * repeats && number.stringBytes == 0);
  assert(number.maximumChunk <= 64);
}

}  // namespace

int main() {
  structurePathsAndEveryBoundary();
  stringsUnicodeAndNul();
  numbersAndScalars();
  rejectInvalidSyntaxAndUnicode();
  fixedLimits();
  cancellationAndReentry();
  megabyteValuesWithoutHeap();
  std::puts("Bounded JSON SAX: exact paths, every split, Unicode, syntax, 32-depth/key/index limits, cancellation and >1MiB strings/numbers without heap passed");
}
