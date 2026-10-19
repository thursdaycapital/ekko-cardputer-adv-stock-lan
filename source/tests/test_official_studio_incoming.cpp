#include "OfficialStudioIncoming.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using official_incoming::Kind;
using official_incoming::Snapshot;

template<size_t Capacity>
bool parse(Snapshot<Capacity>& snapshot, const std::string& json, size_t step = 1) {
  bounded_json::Reader<> reader(snapshot);
  for (size_t at = 0; at < json.size(); at += step)
    if (!reader.feed(json.data() + at, std::min(step, json.size() - at))) return false;
  return reader.finish() && reader.complete();
}

std::string quote(const std::string& text) {
  std::string result = "\"";
  for (unsigned char byte : text) {
    if (byte == '"' || byte == '\\') { result += '\\'; result += char(byte); }
    else if (byte < 0x20) {
      static const char hex[] = "0123456789abcdef";
      result += "\\u00"; result += hex[byte >> 4]; result += hex[byte & 15];
    } else result += char(byte);
  }
  return result + '"';
}

std::string history(const std::string& text) {
  return "{\"session\":{\"id\":\"session-1\"},\"messages\":[{\"id\":1234567890123456789,\"role\":\"assistant\",\"content\":" +
      quote(text) + "}],\"total\":1,\"offset\":0,\"hasMore\":false}";
}

void reject(const std::string& json, Kind kind = Kind::SocketEvent) {
  for (size_t step : {size_t(1), size_t(7), json.size() + 1}) {
    Snapshot<12> snapshot(kind);
    if (parse(snapshot, json, step)) {
      std::fprintf(stderr, "Unexpected accepted input: %.300s\n", json.c_str());
      assert(false);
    }
  }
}

void socketEvents() {
  // Stock Hermes carries both incremental delta and cumulative output. Only
  // the field selected by the outer Socket.IO event belongs to this update.
  const std::string delta = "[\"message.delta\",{\"event\":\"message.delta\",\"session_id\":\"s\",\"run_id\":\"r\",\"queue_id\":\"q\",\"message_id\":42,\"output\":\"previous你好\",\"delta\":\"你好\"}]";
  const std::string completed = "[\"run.completed\",{\"event\":\"run.completed\",\"session_id\":\"s\",\"run_id\":\"r\",\"delta\":\"ignored\",\"output\":\"final你好\"}]";
  for (size_t step = 1; step <= delta.size() + 1; ++step) {
    Snapshot<0> snapshot(Kind::SocketEvent);
    assert(parse(snapshot, delta, step));
    assert(std::strcmp(snapshot.event.data(), "message.delta") == 0);
    assert(std::strcmp(snapshot.session.data(), "s") == 0 && std::strcmp(snapshot.run.data(), "r") == 0);
    assert(std::strcmp(snapshot.queue.data(), "q") == 0 && std::strcmp(snapshot.messageId.data(), "42") == 0);
    assert(snapshot.text.closed && snapshot.text.total == 6 && snapshot.text.bytes == 6);
    assert(std::strcmp(snapshot.text.text.data(), "你好") == 0 && snapshot.count == 0);
  }
  for (size_t step : {size_t(1), size_t(2), size_t(17), completed.size()}) {
    Snapshot<0> snapshot(Kind::SocketEvent);
    assert(parse(snapshot, completed, step));
    assert(std::strcmp(snapshot.text.text.data(), "final你好") == 0 && snapshot.text.total == 11);
  }
  for (const char* json : {
      "[\"run.started\",{\"session_id\":\"s\",\"run_id\":\"r\"}]",
      "[\"run.failed\",{\"session_id\":\"s\",\"error\":\"Coding agent session closed\"}]",
      "[\"run.completed\",{\"session_id\":\"s\",\"run_id\":\"r\",\"output\":null}]"}) {
    Snapshot<0> snapshot(Kind::SocketEvent); assert(parse(snapshot, json));
    assert(std::strcmp(snapshot.session.data(), "s") == 0);
  }
  Snapshot<0> escaped(Kind::SocketEvent);
  assert(parse(escaped, "[\"message.delta\",{\"delta\":\"\\u4f60\\u597d\\ud83d\\ude00\"}]", 1));
  assert(escaped.text.total == 10 && std::strcmp(escaped.text.text.data(), "你好😀") == 0);
  reject("[\"message.delta\",{\"event\":\"run.completed\",\"delta\":\"x\"}]");
  reject("[\"run.failed\",{\"session_id\":\"s\\u0000other\",\"error\":\"x\"}]");
  const std::string oversized(225, 's');
  reject("[\"run.started\",{\"session_id\":" + quote(oversized) + "}]");

  std::string longError;
  for (size_t i = 0; i < 300; ++i) longError += "错";
  Snapshot<0> error(Kind::SocketEvent);
  assert(parse(error, "[\"run.failed\",{\"session_id\":\"s\",\"error\":" + quote(longError) + "}]", 7));
  assert(std::strlen(error.error.data()) == 510);
  assert(native_chat::validUtf8(error.error.data(), 510));
  Snapshot<0> clippedError(Kind::SocketEvent);
  assert(parse(clippedError, "[\"run.failed\",{\"error\":" + quote(longError + std::string(200, 'a')) + "}]", 1));
  // After the first scalar that cannot fit, later smaller scalars must not be
  // attached across the discarded gap: a clipped error remains a true prefix.
  assert(std::strlen(clippedError.error.data()) == 510);
  assert(std::string(clippedError.error.data()) == longError.substr(0, 510));
  Snapshot<0> objectError(Kind::SocketEvent);
  assert(parse(objectError, "[\"run.failed\",{\"session_id\":\"s\",\"error\":{\"code\":502,\"message\":\"reason\"}}]", 1));
  assert(std::strcmp(objectError.error.data(), "reason") == 0);
  Snapshot<0> command(Kind::SocketEvent);
  assert(parse(command,"[\"session.command\",{\"session_id\":\"s\",\"command\":\"status\",\"ok\":true,\"terminal\":false,\"message\":\"工作中\"}]",1));
  assert(!std::strcmp(command.command.data(),"status") && command.hasCommandOk && command.commandOk);
  assert(command.hasCommandTerminal && !command.commandTerminal && command.commandMessage.closed);
  assert(!std::strcmp(command.commandMessage.text.data(),"工作中"));
  reject("[\"session.command\",{\"ok\":true,\"ok\":false}]");
  reject("[\"session.command\",{\"terminal\":\"true\"}]");
  reject("[\"session.command\",{\"command\":\"status\",\"command\":null}]");
}

void historyAndFiltering() {
  std::string json = "{\"session\":{\"id\":\"s\",\"profile\":\"default\"},\"messages\":[";
  size_t rawIndex = 0;
  std::vector<size_t> expectedIndices;
  for (size_t i = 0; i < 20; ++i) {
    if (rawIndex++) json += ',';
    json += "{\"id\":" + std::to_string(i) + ",\"role\":\"assistant\",\"content\":\"text-" + std::to_string(i) + "\"}";
    expectedIndices.push_back(rawIndex - 1);
    if (i % 3 == 0) {
      json += ",{\"id\":\"tool\",\"role\":\"tool\",\"content\":\"private tool result\"}";
      ++rawIndex;
    }
  }
  json += ",{\"id\":\"sys\",\"role\":\"system\",\"content\":\"system\"},{\"id\":\"array\",\"role\":\"user\",\"content\":[]},null],\"total\":400,\"offset\":30,\"hasMore\":true,\"hasMoreBefore\":true,\"isWorking\":false}";
  for (size_t step : {size_t(1), size_t(13), size_t(1024), json.size()}) {
    Snapshot<12> snapshot(Kind::History); assert(parse(snapshot, json, step));
    assert(std::strcmp(snapshot.session.data(), "s") == 0 && snapshot.count == 12);
    assert(snapshot.total == 400 && snapshot.hasTotal() && snapshot.offset == 30);
    assert(snapshot.hasMore && snapshot.hasMoreBefore && snapshot.hasWorking && !snapshot.working);
    assert(snapshot.rows == rawIndex + 2);
    for (size_t n = 0; n < 12; ++n) {
      const auto& message = snapshot.messages[n];
      const std::string id = std::to_string(n + 8);
      assert(std::strcmp(message.id.data(), id.c_str()) == 0);
      assert(std::strcmp(message.content.text.data(), ("text-" + id).c_str()) == 0);
      assert(message.index == expectedIndices[n + 8]);
    }
  }
  Snapshot<0> metadataOnly(Kind::SocketEvent);
  assert(parse(metadataOnly, "[\"resumed\",{\"session_id\":\"s\",\"messages\":[{\"id\":1,\"role\":\"assistant\",\"content\":\"ignored\"}],\"messageTotal\":400,\"isWorking\":true,\"hasMoreBefore\":true}]", 3));
  assert(metadataOnly.count == 0 && metadataOnly.total == 400 && metadataOnly.working && metadataOnly.hasMoreBefore);
  Snapshot<12> ids(Kind::History);
  assert(parse(ids, "{\"session\":{\"id\":\"s\"},\"messages\":[{\"id\":\"str-1\",\"role\":\"user\",\"content\":\"\"},{\"id\":9007199254740993,\"role\":\"assistant\",\"content\":\"x\"}]}"));
  assert(ids.count == 2 && std::strcmp(ids.messages[0].id.data(), "str-1") == 0);
  assert(std::strcmp(ids.messages[1].id.data(), "9007199254740993") == 0);
}

void windows() {
  std::string original;
  for (size_t i = 0; i < 1600; ++i) original += "a你好😀";
  original.insert(1019, 1, '\0');
  std::string displayed = original;
  std::replace(displayed.begin(), displayed.end(), '\0', ' ');
  const std::string json = history(original);
  std::string collected;
  uint32_t next = 0;
  while (next < original.size()) {
    Snapshot<12> snapshot(Kind::History, next);
    assert(parse(snapshot, json, (next % 17) + 1) && snapshot.count == 1);
    const auto& window = snapshot.messages[0].content;
    assert(window.start == next && window.total == original.size() && window.bytes > 0 && window.bytes <= 1024);
    assert(window.closed && native_chat::validUtf8(window.text.data(), window.bytes));
    collected.append(window.text.data(), window.bytes);
    next = window.start + window.bytes;
  }
  assert(collected == displayed);

  // Every position inside a scalar must round down, never skip its first byte.
  const std::string shortText = "a中😀éz";
  for (uint32_t requested = 0; requested <= shortText.size() + 3; ++requested) {
    Snapshot<12> snapshot(Kind::History, requested); assert(parse(snapshot, history(shortText), 1));
    const auto& window = snapshot.messages[0].content;
    const uint32_t floor = requested < 1 ? 0 : requested < 4 ? 1 : requested < 8 ? 4 : requested < 10 ? 8 : requested < 11 ? 10 : 11;
    assert(window.start == floor && window.total == shortText.size());
    assert(std::string(window.text.data(), window.bytes) == shortText.substr(floor));
  }

  std::string huge;
  huge.reserve(1050000);
  while (huge.size() <= 1048576) huge += "A中😀";
  const std::string hugeJson = history(huge);
  for (uint32_t request : {uint32_t(0), uint32_t(1023), uint32_t(524289), uint32_t(huge.size() - 100), uint32_t(huge.size() + 99)}) {
    Snapshot<12> snapshot(Kind::History, request);
    assert(parse(snapshot, hugeJson, 1023) && snapshot.count == 1);
    const auto& window = snapshot.messages[0].content;
    assert(window.total == huge.size() && window.bytes <= 1024 && window.closed);
    assert(native_chat::validUtf8(window.text.data(), window.bytes));
    assert(std::string(window.text.data(), window.bytes) == huge.substr(window.start, window.bytes));
    assert(window.start <= std::min<size_t>(request, huge.size()));
  }
  Snapshot<0> delta(Kind::SocketEvent);
  assert(parse(delta, "[\"message.delta\",{\"session_id\":\"s\",\"delta\":" + quote(huge) + "}]", 127));
  assert(delta.text.total == huge.size() && delta.text.bytes <= 1024 && delta.text.capped && delta.text.closed);
}

void malformedAndDuplicateKeys() {
  for (const std::string& pair : {
      "\"session_id\":\"s\",\"session_id\":null", "\"run_id\":\"r\",\"run_id\":7",
      "\"queue_id\":\"q\",\"queue_id\":{}", "\"message_id\":1,\"message_id\":null",
      "\"error\":\"e\",\"error\":false", "\"isWorking\":false,\"isWorking\":\"x\"",
      "\"messages\":[],\"messages\":null", "\"messageTotal\":1,\"total\":2",
      "\"offset\":0,\"offset\":false", "\"hasMore\":true,\"hasMore\":null",
      "\"hasMoreBefore\":false,\"hasMoreBefore\":0", "\"delta\":\"a\",\"delta\":false",
      "\"output\":\"a\",\"output\":null", "\"event\":\"message.delta\",\"event\":null"})
    reject("[\"message.delta\",{" + pair + "}]");
  for (const std::string& pair : {
      "\"id\":1,\"id\":null", "\"role\":\"user\",\"role\":false",
      "\"content\":\"a\",\"content\":{}"})
    reject("{\"session\":{\"id\":\"s\"},\"messages\":[{" + pair + "}]}", Kind::History);
  for (const std::string& field : {
      "\"total\":-1", "\"total\":1.5", "\"total\":1e2", "\"total\":4294967296",
      "\"total\":\"1\"", "\"offset\":null", "\"isWorking\":\"false\"", "\"messages\":{}"})
    reject("{" + field + "}", Kind::History);
  reject("{\"session\":{\"id\":\"s\",\"id\":null},\"messages\":[]}", Kind::History);
  reject("{\"session\":{\"id\":\"s\"},\"session\":null,\"messages\":[]}", Kind::History);
  reject("[\"run.failed\",{\"session_id\":\"s\",\"error\":{\"message\":\"first\",\"message\":null}}]");
  reject("[\"run.failed\",{\"session_id\":\"s\",\"error\":{\"message\":null,\"message\":\"second\"}}]");

  // Consumers stage the mutable Snapshot and apply only after parser.finish.
  // A valid prefix must never replace the previously committed UI state.
  std::string committed = "old authoritative view";
  const std::string valid = history("new tentative text");
  for (const std::string& suffix : {std::string(" trailing"), std::string("{}"), std::string("\0", 1)}) {
    Snapshot<12> staged(Kind::History);
    if (parse(staged, valid + suffix, 7)) committed = staged.messages[0].content.text.data();
    assert(committed == "old authoritative view");
  }
  for (const std::string& bad : {std::string("{\"messages\":[{\"id\":1,\"role\":\"user\",\"content\":\"x\"}"),
      std::string("[\"message.delta\",{\"session_id\":\"s\",\"delta\":\"bad\\uD800\"}]"),
      std::string("[\"message.delta\",{\"delta\":\"bad") + char(0xc0) + char(0x80) + "\"}]"})
    reject(bad, bad[0] == '{' ? Kind::History : Kind::SocketEvent);
}
} // namespace

int main() {
  socketEvents();
  historyAndFiltering();
  windows();
  malformedAndDuplicateKeys();
}
