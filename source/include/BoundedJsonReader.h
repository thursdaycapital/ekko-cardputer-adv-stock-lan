#pragma once

#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include <limits>

// Incremental RFC 8259 JSON syntax/Unicode reader. Memory is fixed by template
// limits; string and number VALUES have no length limit and are never retained.
// Object keys are decoded UTF-8 and limited in BYTES. Duplicate keys are legal
// JSON and are delivered individually; a security-sensitive consumer must reject
// duplicates itself rather than treating a partial document as authorized.
namespace bounded_json {

enum class Type : uint8_t {
  ObjectBegin, ObjectEnd, ArrayBegin, ArrayEnd, Key,
  StringBegin, StringChunk, StringEnd, NumberBegin, NumberChunk, NumberEnd,
  Boolean, Null
};

enum class Error : uint8_t {
  None, NullInput, UnexpectedCharacter, UnexpectedEnd, TrailingData,
  ExpectedKey, ExpectedColon, ExpectedCommaOrEnd, InvalidNumber, InvalidLiteral,
  InvalidEscape, InvalidUnicode, InvalidUtf8, UnescapedControl,
  DepthLimit, KeyLimit, IndexOverflow, OffsetOverflow, Cancelled, Reentrant
};

enum class SegmentKind : uint8_t { Key, Index };

struct PathSegment {
  SegmentKind kind = SegmentKind::Key;
  const char* key = nullptr;
  size_t keyBytes = 0;
  uint64_t index = 0;
};

struct PathView {
  const PathSegment* segments = nullptr;
  size_t size = 0;
};

struct Event {
  Type type = Type::Null;
  PathView path;
  const char* data = nullptr;
  size_t size = 0;
  bool booleanValue = false;
};

class Listener {
 public:
  virtual ~Listener() = default;
  // Views are borrowed ONLY until this callback returns. data/key may contain
  // NUL; use the explicit byte lengths. String chunks end at UTF-8 scalar
  // boundaries; number chunks are ASCII lexemes, never double conversions.
  // A callback must not reenter/reset this reader. Returning false cancels it.
  // Events are tentative until finish() succeeds; never perform privileged
  // actions based solely on a prefix of an otherwise unvalidated document.
  virtual bool onEvent(const Event& event) = 0;
};

template <size_t MaxDepth = 32, size_t MaxKeyBytes = 64, size_t ChunkBytes = 64,
          uint64_t MaxArrayIndex = (std::numeric_limits<uint64_t>::max)()>
class Reader {
  static_assert(MaxDepth > 0, "JSON depth limit must be positive");
  static_assert(MaxKeyBytes > 0, "JSON key byte limit must be positive");
  static_assert(ChunkBytes >= 4, "A string chunk must fit one UTF-8 scalar");

 public:
  explicit Reader(Listener& listener) : listener_(listener) { reset(); }
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  bool feed(const uint8_t* bytes, size_t length) {
    if (busy_) return fail(Error::Reentrant);
    if (error_ != Error::None) return false;
    if (!bytes && length) return fail(Error::NullInput);
    busy_ = true;
    while (length && error_ == Error::None) {
      if (offset_ == (std::numeric_limits<uint64_t>::max)()) {
        fail(Error::OffsetOverflow);
        break;
      }
      consume(*bytes++);
      --length;
      ++offset_;
    }
    busy_ = false;
    return error_ == Error::None;
  }

  bool feed(const char* bytes, size_t length) {
    return feed(reinterpret_cast<const uint8_t*>(bytes), length);
  }

  // EOF is meaningful for a number: feed("1") must not finish it before an
  // exponent or fractional part can arrive. finish never invents closing
  // quotes/containers and never resets. Repeated finish and trailing JSON
  // whitespace after completion are harmless; a second root value is rejected.
  bool finish() {
    if (busy_) return fail(Error::Reentrant);
    if (error_ != Error::None) return false;
    busy_ = true;
    if (lex_ == Lex::Number) {
      if (!numberTerminal()) fail(Error::UnexpectedEnd);
      else finishNumber();
    }
    if (error_ == Error::None && !complete()) fail(Error::UnexpectedEnd);
    busy_ = false;
    return error_ == Error::None;
  }

  bool reset() {
    if (busy_) return fail(Error::Reentrant);
    depth_ = 0;
    lex_ = Lex::None;
    stringState_ = StringState::Normal;
    numberState_ = NumberState::Zero;
    literal_ = nullptr;
    literalPosition_ = 0;
    rootStarted_ = rootDone_ = keyString_ = false;
    chunkUsed_ = utf8Used_ = utf8Remaining_ = hexDigits_ = 0;
    scalar_ = utf8Minimum_ = hexValue_ = highSurrogate_ = 0;
    error_ = Error::None;
    offset_ = errorOffset_ = 0;
    std::memset(chunk_, 0, sizeof(chunk_));
    std::memset(utf8_, 0, sizeof(utf8_));
    for (size_t i = 0; i < MaxDepth; ++i) {
      frames_[i] = Frame{};
      path_[i] = PathSegment{};
    }
    return true;
  }

  Error error() const { return error_; }
  uint64_t offset() const { return offset_; }
  uint64_t errorOffset() const { return errorOffset_; }
  bool complete() const {
    return error_ == Error::None && rootDone_ && depth_ == 0 && lex_ == Lex::None;
  }

 private:
  enum class Kind : uint8_t { Object, Array };
  enum class Phase : uint8_t {
    ObjectKeyOrEnd, ObjectKey, Colon, ObjectValue, ObjectCommaOrEnd,
    ArrayValueOrEnd, ArrayValue, ArrayCommaOrEnd
  };
  enum class Lex : uint8_t { None, String, Number, Literal };
  enum class StringState : uint8_t { Normal, Escape, Hex, LowSlash, LowU, LowHex };
  enum class NumberState : uint8_t { Minus, Zero, Integer, Dot, Fraction, Exponent, ExponentSign, ExponentDigits };

  struct Frame {
    Kind kind = Kind::Object;
    Phase phase = Phase::ObjectKeyOrEnd;
    uint64_t index = 0;
    size_t keyBytes = 0;
    char key[MaxKeyBytes + 1] = {};
  };

  static bool whitespace(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
  static bool digit(uint8_t c) { return c >= '0' && c <= '9'; }
  static bool delimiter(uint8_t c) { return whitespace(c) || c == ',' || c == ']' || c == '}'; }
  static int hexDigit(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool fail(Error error) {
    if (error_ == Error::None) { error_ = error; errorOffset_ = offset_; }
    return false;
  }

  bool emit(Type type, const char* data = nullptr, size_t size = 0, bool boolean = false) {
    if (error_ != Error::None) return false;
    for (size_t i = 0; i < depth_; ++i) {
      const auto& frame = frames_[i];
      auto& segment = path_[i];
      segment.kind = frame.kind == Kind::Object ? SegmentKind::Key : SegmentKind::Index;
      segment.key = frame.kind == Kind::Object ? frame.key : nullptr;
      segment.keyBytes = frame.kind == Kind::Object ? frame.keyBytes : 0;
      segment.index = frame.kind == Kind::Array ? frame.index : 0;
    }
    Event event;
    event.type = type;
    event.path.segments = path_;
    event.path.size = depth_;
    event.data = data;
    event.size = size;
    event.booleanValue = boolean;
    if (!listener_.onEvent(event)) return fail(Error::Cancelled);
    return error_ == Error::None;  // A callback may have attempted reentry.
  }

  bool flush(Type type) {
    if (!chunkUsed_) return true;
    const size_t bytes = chunkUsed_;
    chunkUsed_ = 0;
    return emit(type, chunk_, bytes);
  }

  bool append(const char* bytes, size_t length, Type chunkType) {
    if (keyString_) {
      auto& frame = frames_[depth_ - 1];
      if (length > MaxKeyBytes - frame.keyBytes) return fail(Error::KeyLimit);
      std::memcpy(frame.key + frame.keyBytes, bytes, length);
      frame.keyBytes += length;
      frame.key[frame.keyBytes] = 0;
      return true;
    }
    if (length > ChunkBytes - chunkUsed_ && !flush(chunkType)) return false;
    std::memcpy(chunk_ + chunkUsed_, bytes, length);
    chunkUsed_ += length;
    chunk_[chunkUsed_] = 0;
    return chunkUsed_ != ChunkBytes || flush(chunkType);
  }

  bool appendScalar(uint32_t value) {
    char bytes[4];
    size_t length;
    if (value <= 0x7f) { bytes[0] = static_cast<char>(value); length = 1; }
    else if (value <= 0x7ff) {
      bytes[0] = static_cast<char>(0xc0 | (value >> 6));
      bytes[1] = static_cast<char>(0x80 | (value & 0x3f)); length = 2;
    } else if (value <= 0xffff) {
      bytes[0] = static_cast<char>(0xe0 | (value >> 12));
      bytes[1] = static_cast<char>(0x80 | ((value >> 6) & 0x3f));
      bytes[2] = static_cast<char>(0x80 | (value & 0x3f)); length = 3;
    } else {
      bytes[0] = static_cast<char>(0xf0 | (value >> 18));
      bytes[1] = static_cast<char>(0x80 | ((value >> 12) & 0x3f));
      bytes[2] = static_cast<char>(0x80 | ((value >> 6) & 0x3f));
      bytes[3] = static_cast<char>(0x80 | (value & 0x3f)); length = 4;
    }
    return append(bytes, length, Type::StringChunk);
  }

  bool valueDone() {
    if (!depth_) { rootDone_ = true; return true; }
    auto& frame = frames_[depth_ - 1];
    if (frame.phase == Phase::ObjectValue) frame.phase = Phase::ObjectCommaOrEnd;
    else if (frame.phase == Phase::ArrayValueOrEnd || frame.phase == Phase::ArrayValue) frame.phase = Phase::ArrayCommaOrEnd;
    else return fail(Error::UnexpectedCharacter);
    return true;
  }

  bool beginContainer(Kind kind) {
    if (depth_ == MaxDepth) return fail(Error::DepthLimit);
    if (!emit(kind == Kind::Object ? Type::ObjectBegin : Type::ArrayBegin)) return false;
    auto& frame = frames_[depth_++];
    frame = Frame{};
    frame.kind = kind;
    frame.phase = kind == Kind::Object ? Phase::ObjectKeyOrEnd : Phase::ArrayValueOrEnd;
    return true;
  }

  bool endContainer() {
    const auto kind = frames_[depth_ - 1].kind;
    --depth_;
    return emit(kind == Kind::Object ? Type::ObjectEnd : Type::ArrayEnd) && valueDone();
  }

  bool beginString(bool key) {
    lex_ = Lex::String;
    keyString_ = key;
    stringState_ = StringState::Normal;
    chunkUsed_ = utf8Used_ = utf8Remaining_ = hexDigits_ = 0;
    scalar_ = utf8Minimum_ = hexValue_ = highSurrogate_ = 0;
    if (key) {
      frames_[depth_ - 1].keyBytes = 0;
      frames_[depth_ - 1].key[0] = 0;
      return true;
    }
    return emit(Type::StringBegin);
  }

  bool finishString() {
    lex_ = Lex::None;
    if (keyString_) {
      auto& frame = frames_[depth_ - 1];
      frame.phase = Phase::Colon;
      keyString_ = false;
      return emit(Type::Key, frame.key, frame.keyBytes);
    }
    return flush(Type::StringChunk) && emit(Type::StringEnd) && valueDone();
  }

  bool consumeString(uint8_t c) {
    if (utf8Remaining_) {
      if ((c & 0xc0) != 0x80) return fail(Error::InvalidUtf8);
      utf8_[utf8Used_++] = static_cast<char>(c);
      scalar_ = (scalar_ << 6) | (c & 0x3f);
      if (--utf8Remaining_) return true;
      if (scalar_ < utf8Minimum_ || scalar_ > 0x10ffff || (scalar_ >= 0xd800 && scalar_ <= 0xdfff)) return fail(Error::InvalidUtf8);
      return append(utf8_, utf8Used_, Type::StringChunk);
    }
    if (stringState_ == StringState::LowSlash) {
      if (c != '\\') return fail(Error::InvalidUnicode);
      stringState_ = StringState::LowU; return true;
    }
    if (stringState_ == StringState::LowU) {
      if (c != 'u') return fail(Error::InvalidUnicode);
      stringState_ = StringState::LowHex; hexValue_ = 0; hexDigits_ = 0; return true;
    }
    if (stringState_ == StringState::Hex || stringState_ == StringState::LowHex) {
      const int digitValue = hexDigit(c);
      if (digitValue < 0) return fail(Error::InvalidUnicode);
      hexValue_ = (hexValue_ << 4) | static_cast<uint32_t>(digitValue);
      if (++hexDigits_ != 4) return true;
      const uint32_t value = hexValue_;
      if (stringState_ == StringState::LowHex) {
        if (value < 0xdc00 || value > 0xdfff) return fail(Error::InvalidUnicode);
        stringState_ = StringState::Normal;
        return appendScalar(0x10000 + ((highSurrogate_ - 0xd800) << 10) + value - 0xdc00);
      }
      if (value >= 0xd800 && value <= 0xdbff) {
        highSurrogate_ = value; stringState_ = StringState::LowSlash; return true;
      }
      if (value >= 0xdc00 && value <= 0xdfff) return fail(Error::InvalidUnicode);
      stringState_ = StringState::Normal;
      return appendScalar(value);
    }
    if (stringState_ == StringState::Escape) {
      stringState_ = StringState::Normal;
      switch (c) {
        case '"': case '\\': case '/': return appendScalar(c);
        case 'b': return appendScalar('\b');
        case 'f': return appendScalar('\f');
        case 'n': return appendScalar('\n');
        case 'r': return appendScalar('\r');
        case 't': return appendScalar('\t');
        case 'u': stringState_ = StringState::Hex; hexValue_ = 0; hexDigits_ = 0; return true;
        default: return fail(Error::InvalidEscape);
      }
    }
    if (c == '"') return finishString();
    if (c == '\\') { stringState_ = StringState::Escape; return true; }
    if (c < 0x20) return fail(Error::UnescapedControl);
    if (c < 0x80) return appendScalar(c);
    utf8_[0] = static_cast<char>(c); utf8Used_ = 1;
    if (c >= 0xc2 && c <= 0xdf) { utf8Remaining_ = 1; scalar_ = c & 0x1f; utf8Minimum_ = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { utf8Remaining_ = 2; scalar_ = c & 0x0f; utf8Minimum_ = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { utf8Remaining_ = 3; scalar_ = c & 0x07; utf8Minimum_ = 0x10000; }
    else return fail(Error::InvalidUtf8);
    return true;
  }

  bool numberTerminal() const {
    return numberState_ == NumberState::Zero || numberState_ == NumberState::Integer ||
        numberState_ == NumberState::Fraction || numberState_ == NumberState::ExponentDigits;
  }

  bool finishNumber() {
    lex_ = Lex::None;
    return flush(Type::NumberChunk) && emit(Type::NumberEnd) && valueDone();
  }

  // A delimiter ends a number but belongs to the surrounding grammar. The
  // caller reprocesses that SAME byte without changing input offset twice.
  bool consumeNumber(uint8_t c, bool& consumed) {
    consumed = true;
    bool accepted = false;
    switch (numberState_) {
      case NumberState::Minus:
        if (c == '0') { numberState_ = NumberState::Zero; accepted = true; }
        else if (c >= '1' && c <= '9') { numberState_ = NumberState::Integer; accepted = true; }
        break;
      case NumberState::Zero:
      case NumberState::Integer:
        if (digit(c) && numberState_ == NumberState::Integer) accepted = true;
        else if (c == '.') { numberState_ = NumberState::Dot; accepted = true; }
        else if (c == 'e' || c == 'E') { numberState_ = NumberState::Exponent; accepted = true; }
        break;
      case NumberState::Dot:
        if (digit(c)) { numberState_ = NumberState::Fraction; accepted = true; }
        break;
      case NumberState::Fraction:
        if (digit(c)) accepted = true;
        else if (c == 'e' || c == 'E') { numberState_ = NumberState::Exponent; accepted = true; }
        break;
      case NumberState::Exponent:
        if (c == '+' || c == '-') { numberState_ = NumberState::ExponentSign; accepted = true; }
        else if (digit(c)) { numberState_ = NumberState::ExponentDigits; accepted = true; }
        break;
      case NumberState::ExponentSign:
        if (digit(c)) { numberState_ = NumberState::ExponentDigits; accepted = true; }
        break;
      case NumberState::ExponentDigits:
        accepted = digit(c); break;
    }
    if (accepted) {
      const char value = static_cast<char>(c);
      return append(&value, 1, Type::NumberChunk);
    }
    if (!numberTerminal() || !delimiter(c)) return fail(Error::InvalidNumber);
    consumed = false;
    return finishNumber();
  }

  bool beginValue(uint8_t c) {
    if (!depth_) rootStarted_ = true;
    if (c == '{') return beginContainer(Kind::Object);
    if (c == '[') return beginContainer(Kind::Array);
    if (c == '"') return beginString(false);
    if (c == '-' || digit(c)) {
      lex_ = Lex::Number;
      keyString_ = false; chunkUsed_ = 0;
      numberState_ = c == '-' ? NumberState::Minus : c == '0' ? NumberState::Zero : NumberState::Integer;
      if (!emit(Type::NumberBegin)) return false;
      const char value = static_cast<char>(c);
      return append(&value, 1, Type::NumberChunk);
    }
    if (c == 't' || c == 'f' || c == 'n') {
      lex_ = Lex::Literal;
      literal_ = c == 't' ? "true" : c == 'f' ? "false" : "null";
      literalPosition_ = 1;
      return true;
    }
    return fail(Error::UnexpectedCharacter);
  }

  bool consume(uint8_t c) {
    if (lex_ == Lex::String) return consumeString(c);
    if (lex_ == Lex::Literal) {
      if (c != static_cast<uint8_t>(literal_[literalPosition_])) return fail(Error::InvalidLiteral);
      if (literal_[++literalPosition_]) return true;
      lex_ = Lex::None;
      return emit(literal_[0] == 'n' ? Type::Null : Type::Boolean, nullptr, 0, literal_[0] == 't') && valueDone();
    }
    if (lex_ == Lex::Number) {
      bool consumed;
      if (!consumeNumber(c, consumed)) return false;
      if (consumed) return true;
    }
    if (whitespace(c)) return true;
    if (!depth_) {
      if (rootDone_) return fail(Error::TrailingData);
      if (rootStarted_) return fail(Error::UnexpectedCharacter);
      return beginValue(c);
    }
    auto& frame = frames_[depth_ - 1];
    switch (frame.phase) {
      case Phase::ObjectKeyOrEnd:
        if (c == '}') return endContainer();
        // fall through: nonempty objects require a quoted key.
      case Phase::ObjectKey:
        return c == '"' ? beginString(true) : fail(Error::ExpectedKey);
      case Phase::Colon:
        if (c != ':') return fail(Error::ExpectedColon);
        frame.phase = Phase::ObjectValue; return true;
      case Phase::ObjectValue: return beginValue(c);
      case Phase::ObjectCommaOrEnd:
        if (c == '}') return endContainer();
        if (c != ',') return fail(Error::ExpectedCommaOrEnd);
        frame.phase = Phase::ObjectKey; return true;
      case Phase::ArrayValueOrEnd:
        if (c == ']') return endContainer();
        return beginValue(c);
      case Phase::ArrayValue: return beginValue(c);
      case Phase::ArrayCommaOrEnd:
        if (c == ']') return endContainer();
        if (c != ',') return fail(Error::ExpectedCommaOrEnd);
        if (frame.index == MaxArrayIndex) return fail(Error::IndexOverflow);
        ++frame.index;
        frame.phase = Phase::ArrayValue; return true;
    }
    return fail(Error::UnexpectedCharacter);
  }

  Listener& listener_;
  Frame frames_[MaxDepth];
  PathSegment path_[MaxDepth];
  size_t depth_ = 0;
  Lex lex_ = Lex::None;
  StringState stringState_ = StringState::Normal;
  NumberState numberState_ = NumberState::Zero;
  const char* literal_ = nullptr;
  size_t literalPosition_ = 0;
  bool rootStarted_ = false, rootDone_ = false, keyString_ = false, busy_ = false;
  char chunk_[ChunkBytes + 1] = {};
  size_t chunkUsed_ = 0;
  char utf8_[4] = {};
  size_t utf8Used_ = 0, utf8Remaining_ = 0, hexDigits_ = 0;
  uint32_t scalar_ = 0, utf8Minimum_ = 0, hexValue_ = 0, highSurrogate_ = 0;
  Error error_ = Error::None;
  uint64_t offset_ = 0, errorOffset_ = 0;
};

}  // namespace bounded_json
