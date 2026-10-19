#include "WebSocketFrameReader.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Sink : studio_ws::Listener {
  uint64_t bytes = 0, hash = 1469598103934665603ULL;
  unsigned begins = 0, ends = 0, controls = 0, calls = 0, cancelAt = 0;
  uint8_t messageOpcode = 0, controlOpcode = 0;
  std::vector<uint8_t> controlBytes;
  bool accept() { return ++calls != cancelAt; }
  bool messageBegin(uint8_t opcode) override { messageOpcode = opcode; ++begins; return accept(); }
  bool messageByte(uint8_t byte) override { ++bytes; hash = (hash ^ byte) * 1099511628211ULL; return accept(); }
  bool messageEnd() override { ++ends; return accept(); }
  bool control(uint8_t opcode, const uint8_t* data, size_t size) override {
    ++controls; controlOpcode = opcode; controlBytes.assign(data, data + size); return accept();
  }
};

std::vector<uint8_t> frame(uint8_t opcode, const std::vector<uint8_t>& body) {
  const size_t size = body.size();
  std::vector<uint8_t> result{opcode};
  if (size < 126) result.push_back(static_cast<uint8_t>(size));
  else if (size <= 65535) {
    result.push_back(126); result.push_back(static_cast<uint8_t>(size >> 8)); result.push_back(static_cast<uint8_t>(size));
  } else {
    result.push_back(127);
    for (int shift = 56; shift >= 0; shift -= 8) result.push_back(static_cast<uint8_t>(uint64_t(size) >> shift));
  }
  result.insert(result.end(), body.begin(), body.end());
  return result;
}

void append(std::vector<uint8_t>& target, const std::vector<uint8_t>& other) {
  target.insert(target.end(), other.begin(), other.end());
}

void feed(studio_ws::Reader& reader, const std::vector<uint8_t>& bytes, size_t step) {
  for (size_t at = 0; at < bytes.size(); at += step)
    assert(reader.feed(bytes.data() + at, std::min(step, bytes.size() - at)));
}

void reject(const std::vector<uint8_t>& bytes, studio_ws::Error expected) {
  for (size_t step : {size_t(1), size_t(3), bytes.size() + 1}) {
    Sink sink; studio_ws::Reader reader(sink);
    bool valid = true;
    for (size_t at = 0; at < bytes.size() && valid; at += step)
      valid = reader.feed(bytes.data() + at, std::min(step, bytes.size() - at));
    if (valid || reader.error() != expected) {
      std::fprintf(stderr, "Frame accepted/wrong error: %u expected %u\n", unsigned(reader.error()), unsigned(expected));
      assert(false);
    }
    const unsigned calls = sink.calls;
    const uint8_t emptyText[]{0x81, 0};
    assert(!reader.feed(emptyText, sizeof(emptyText)) && sink.calls == calls && reader.error() == expected);
    reader.reset();
    assert(reader.error() == studio_ws::Error::None && reader.feed(emptyText, sizeof(emptyText)) && reader.idle());
  }
}

void sizesAndSplits() {
  static_assert(sizeof(studio_ws::Reader) < 256, "Frame reader must not retain arbitrary payloads");
  for (size_t length : {size_t(0), size_t(1), size_t(125), size_t(126), size_t(127), size_t(65535), size_t(65536), size_t(1048587)}) {
    std::vector<uint8_t> body(length);
    uint64_t expectedHash = 1469598103934665603ULL;
    for (size_t at = 0; at < length; ++at) {
      body[at] = static_cast<uint8_t>((at * 17 + at / 7) & 255);
      expectedHash = (expectedHash ^ body[at]) * 1099511628211ULL;
    }
    const auto encoded = frame(0x82, body);
    for (size_t step : {size_t(1), size_t(7), size_t(1024), encoded.size() + 1}) {
      Sink sink; studio_ws::Reader reader(sink);
      assert(reader.feed(nullptr, 0));
      feed(reader, encoded, step);
      assert(reader.idle() && reader.error() == studio_ws::Error::None);
      assert(sink.bytes == length && sink.hash == expectedHash && sink.begins == 1 && sink.ends == 1 && sink.messageOpcode == 2);
    }
    const size_t header = encoded.size() - body.size();
    for (size_t split = 0; split <= std::min(encoded.size(), header + 1); ++split) {
      Sink sink; studio_ws::Reader reader(sink);
      assert(reader.feed(encoded.data(), split));
      assert(reader.feed(encoded.data() + split, encoded.size() - split));
      assert(reader.idle() && sink.bytes == length && sink.hash == expectedHash);
    }
  }
}

void fragmentationAndControl() {
  std::vector<uint8_t> encoded = frame(0x01, {'a', 0xe4});
  append(encoded, frame(0x89, {'p', 0, 'g'}));
  append(encoded, frame(0x00, {}));
  append(encoded, frame(0x8a, {}));
  append(encoded, frame(0x80, {0xb8, 0xad, 'z'}));
  append(encoded, frame(0x81, {}));
  const std::vector<uint8_t> expected{'a', 0xe4, 0xb8, 0xad, 'z'};
  uint64_t hash = 1469598103934665603ULL;
  for (uint8_t byte : expected) hash = (hash ^ byte) * 1099511628211ULL;
  for (size_t split = 0; split <= encoded.size(); ++split) {
    Sink sink; studio_ws::Reader reader(sink);
    assert(reader.feed(encoded.data(), split));
    assert(reader.feed(encoded.data() + split, encoded.size() - split));
    assert(reader.idle() && sink.bytes == expected.size() && sink.hash == hash);
    assert(sink.begins == 2 && sink.ends == 2 && sink.controls == 2 && sink.controlOpcode == 10 && sink.controlBytes.empty());
  }
  const std::vector<uint8_t> part(524301, 'x');
  encoded = frame(0x01, part);
  append(encoded, frame(0x89, std::vector<uint8_t>(125, 'p')));
  append(encoded, frame(0x80, part));
  for (size_t step : {size_t(1), size_t(1023)}) {
    Sink sink; studio_ws::Reader reader(sink); feed(reader, encoded, step);
    assert(reader.idle() && sink.bytes == part.size() * 2 && sink.begins == 1 && sink.ends == 1);
    assert(sink.controls == 1 && sink.controlBytes == std::vector<uint8_t>(125, 'p'));
  }
}

void invalidFrames() {
  using studio_ws::Error;
  reject({0x81,0x80}, Error::MaskedServerFrame);
  for (uint8_t first : {uint8_t(0x91), uint8_t(0xa1), uint8_t(0xc1)}) reject({first,0}, Error::ReservedBits);
  for (uint8_t first : {uint8_t(0x83), uint8_t(0x87), uint8_t(0x8b), uint8_t(0x8f)}) reject({first,0}, Error::InvalidOpcode);
  reject({0x80,0}, Error::UnexpectedContinuation);
  reject({0x09,0}, Error::FragmentedControl);
  reject({0x08,0}, Error::FragmentedControl);
  reject({0x89,126}, Error::InvalidLength);
  reject({0x8a,127}, Error::InvalidLength);
  reject({0x81,126,0,125}, Error::InvalidLength);
  reject({0x81,127,0,0,0,0,0,0,0xff,0xff}, Error::InvalidLength);
  reject({0x81,127,0x80}, Error::InvalidLength);
  reject({0x01,0,0x81,0}, Error::InterruptedMessage);
  reject({0x01,0,0x82,0}, Error::InterruptedMessage);
  reject({0x88,1,'x'}, Error::InvalidClose);
  // EOF is owned by the connection: non-idle means incomplete header/body or
  // a message whose last continuation has not arrived. No fake messageEnd.
  for (const std::vector<uint8_t>& prefix : {std::vector<uint8_t>{0x81}, {0x81,126,1}, {0x81,2,'x'}, {0x01,0}}) {
    Sink sink; studio_ws::Reader reader(sink);
    assert(reader.feed(prefix.data(), prefix.size()) && !reader.idle() && sink.ends == 0);
  }
}

std::vector<uint8_t> close(uint16_t code, const std::vector<uint8_t>& reason = {}) {
  std::vector<uint8_t> body{static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code)};
  append(body, reason); return frame(0x88, body);
}

void closeValidation() {
  for (uint16_t code : {uint16_t(1000), uint16_t(1001), uint16_t(1002), uint16_t(1003), uint16_t(1007), uint16_t(1008), uint16_t(1009),
      uint16_t(1010), uint16_t(1011), uint16_t(1012), uint16_t(1013), uint16_t(1014), uint16_t(3000), uint16_t(4999)}) {
    const auto bytes = close(code, {'a', 0, 0xe4, 0xb8, 0xad, 0xf0, 0x9f, 0x98, 0x80});
    for (size_t step : {size_t(1), bytes.size()}) {
      Sink sink; studio_ws::Reader reader(sink); feed(reader, bytes, step);
      assert(reader.idle() && sink.controls == 1 && sink.controlOpcode == 8 && sink.controlBytes.size() == 11);
    }
  }
  { Sink sink; studio_ws::Reader reader(sink); feed(reader, frame(0x88, {}), 1); assert(reader.idle() && sink.controls == 1); }
  { Sink sink; studio_ws::Reader reader(sink); feed(reader, close(1000, std::vector<uint8_t>(123, 'x')), 1);
    assert(reader.idle() && sink.controls == 1 && sink.controlBytes.size() == 125); }
  { Sink sink; studio_ws::Reader reader(sink); feed(reader, close(1000, {0xc2,0x80,0xe0,0xa0,0x80,0xf0,0x90,0x80,0x80,0xf4,0x8f,0xbf,0xbf}), 1);
    assert(reader.idle() && sink.controls == 1); }
  for (uint16_t code : {uint16_t(0), uint16_t(999), uint16_t(1004), uint16_t(1005), uint16_t(1006), uint16_t(1015),
      uint16_t(1016), uint16_t(2999), uint16_t(5000), uint16_t(65535)}) reject(close(code), studio_ws::Error::InvalidClose);
  for (const std::vector<uint8_t>& reason : {
      std::vector<uint8_t>{0x80}, {0xc0,0x80}, {0xe0,0x80,0x80}, {0xed,0xa0,0x80},
      {0xf0,0x80,0x80,0x80}, {0xf4,0x90,0x80,0x80}, {0xf5,0x80,0x80,0x80},
      {0xe4,0xb8}, {0xf0,0x9f,0x98}, {0xe4,'x',0xad}}) reject(close(1000, reason), studio_ws::Error::InvalidClose);
}

void cancellations() {
  std::vector<uint8_t> encoded = frame(0x01, {'x','y'});
  append(encoded, frame(0x89, {'p'}));
  append(encoded, frame(0x80, {'z'}));
  Sink baseline; studio_ws::Reader complete(baseline); feed(complete, encoded, 1);
  for (unsigned cancel = 1; cancel <= baseline.calls; ++cancel) {
    Sink sink; sink.cancelAt = cancel; studio_ws::Reader reader(sink);
    assert(!reader.feed(encoded.data(), encoded.size()));
    assert(reader.error() == studio_ws::Error::Cancelled && sink.calls == cancel);
    assert(!reader.feed(encoded.data(), encoded.size()) && sink.calls == cancel);
  }
}
} // namespace

int main() {
  sizesAndSplits();
  fragmentationAndControl();
  invalidFrames();
  closeValidation();
  cancellations();
}
