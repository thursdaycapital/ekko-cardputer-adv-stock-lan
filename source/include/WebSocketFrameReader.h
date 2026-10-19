#pragma once
#include <cstddef>
#include <cstdint>

namespace studio_ws {

enum class Error : uint8_t {
  None, ReservedBits, MaskedServerFrame, InvalidOpcode, InvalidLength,
  UnexpectedContinuation, InterruptedMessage, FragmentedControl, InvalidClose,
  Cancelled, CounterOverflow
};

// A server-to-client RFC6455 frame decoder. Payloads are borrowed one byte at a
// time: no frame-sized allocation, no assumption that TCP reads align to frames.
// Control frames may interrupt a fragmented data message. The application owns
// JSON/text-message UTF-8 validation and responds to Ping / Close through its
// listener. Close status/reason are validated before reaching the listener.
class Listener {
 public:
  virtual ~Listener() = default;
  virtual bool messageBegin(uint8_t opcode) = 0;
  virtual bool messageByte(uint8_t byte) = 0;
  virtual bool messageEnd() = 0;
  virtual bool control(uint8_t opcode, const uint8_t* bytes, size_t length) = 0;
};

class Reader {
 public:
  explicit Reader(Listener& listener) : listener_(listener) {}
  void reset() {
    state_ = State::First; error_ = Error::None; fragmented_ = false;
    remaining_ = length_ = 0; extLeft_ = extBytes_ = 0; controlBytes_ = 0;
  }
  Error error() const { return error_; }
  bool idle() const { return state_ == State::First && !fragmented_; }
  bool feed(const uint8_t* bytes, size_t length) {
    if (error_ != Error::None || (!bytes && length)) return false;
    for (size_t i = 0; i < length; ++i) if (!feedByte(bytes[i])) return false;
    return true;
  }

 private:
  enum class State : uint8_t { First, Second, Extended, Payload };
  Listener& listener_;
  State state_ = State::First;
  Error error_ = Error::None;
  uint8_t opcode_ = 0, extLeft_ = 0, extBytes_ = 0;
  bool final_ = false, fragmented_ = false;
  uint64_t length_ = 0, remaining_ = 0;
  uint8_t control_[125]{};
  size_t controlBytes_ = 0;

  bool fail(Error error) { error_ = error; return false; }
  bool accepted(bool ok) { return ok || fail(Error::Cancelled); }
  static bool validCloseCode(uint16_t code) {
    // RFC6455 section 7.4: reserved pseudo-codes cannot occur on the wire;
    // 1012--1014 are registered service restart / retry / gateway statuses.
    return (code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) ||
        (code >= 3000 && code <= 4999);
  }
  static bool validUtf8(const uint8_t* bytes, size_t length) {
    size_t at = 0;
    while (at < length) {
      const uint8_t first = bytes[at++];
      if (first < 0x80) continue;
      uint8_t remaining;
      uint32_t scalar, minimum;
      if (first >= 0xc2 && first <= 0xdf) { remaining = 1; scalar = first & 0x1f; minimum = 0x80; }
      else if (first >= 0xe0 && first <= 0xef) { remaining = 2; scalar = first & 0x0f; minimum = 0x800; }
      else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; scalar = first & 7; minimum = 0x10000; }
      else return false;
      if (remaining > length - at) return false;
      while (remaining--) {
        const uint8_t byte = bytes[at++];
        if ((byte & 0xc0) != 0x80) return false;
        scalar = (scalar << 6) | (byte & 0x3f);
      }
      if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
    }
    return true;
  }
  bool beginPayload() {
    if ((opcode_ & 8) && length_ > 125) return fail(Error::InvalidLength);
    remaining_ = length_; controlBytes_ = 0;
    if (!(opcode_ & 8)) {
      if (opcode_ == 0) {
        if (!fragmented_) return fail(Error::UnexpectedContinuation);
      } else {
        if (fragmented_) return fail(Error::InterruptedMessage);
        if (!accepted(listener_.messageBegin(opcode_))) return false;
        fragmented_ = true;
      }
    }
    state_ = State::Payload;
    return remaining_ ? true : endPayload();
  }
  bool endPayload() {
    state_ = State::First;
    if (opcode_ & 8) {
      if (opcode_ == 8 && (controlBytes_ == 1 || (controlBytes_ >= 2 &&
          (!validCloseCode(static_cast<uint16_t>((uint16_t(control_[0]) << 8) | control_[1])) ||
           !validUtf8(control_ + 2, controlBytes_ - 2))))) return fail(Error::InvalidClose);
      return accepted(listener_.control(opcode_, control_, controlBytes_));
    }
    if (!final_) return true;
    fragmented_ = false;
    return accepted(listener_.messageEnd());
  }
  bool feedByte(uint8_t byte) {
    switch (state_) {
      case State::First:
        if (byte & 0x70) return fail(Error::ReservedBits);
        opcode_ = byte & 15; final_ = (byte & 0x80) != 0;
        if (opcode_ != 0 && opcode_ != 1 && opcode_ != 2 && opcode_ != 8 && opcode_ != 9 && opcode_ != 10)
          return fail(Error::InvalidOpcode);
        if ((opcode_ & 8) && !final_) return fail(Error::FragmentedControl);
        state_ = State::Second; return true;
      case State::Second:
        if (byte & 0x80) return fail(Error::MaskedServerFrame);
        length_ = byte & 127;
        if ((opcode_ & 8) && length_ > 125) return fail(Error::InvalidLength);
        if (length_ < 126) return beginPayload();
        extBytes_ = extLeft_ = length_ == 126 ? 2 : 8;
        length_ = 0; state_ = State::Extended; return true;
      case State::Extended:
        if (extLeft_ == 8 && (byte & 0x80)) return fail(Error::InvalidLength);
        length_ = (length_ << 8) | byte;
        if (--extLeft_) return true;
        if ((extBytes_ == 2 && length_ < 126) || (extBytes_ == 8 && length_ <= 65535))
          return fail(Error::InvalidLength);
        return beginPayload();
      case State::Payload:
        if (!remaining_) return fail(Error::CounterOverflow);
        if (opcode_ & 8) control_[controlBytes_++] = byte;
        else if (!accepted(listener_.messageByte(byte))) return false;
        return --remaining_ ? true : endPayload();
    }
    return fail(Error::InvalidOpcode);
  }
};
} // namespace studio_ws
