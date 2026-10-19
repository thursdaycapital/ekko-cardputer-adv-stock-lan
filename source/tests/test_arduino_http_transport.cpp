#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

// Only the SDK boundary is doubled. The runner includes the production
// DeadlineClient, SinkStream, getWithClient and ArduinoTransport verbatim.
namespace sdk {
uint32_t now = 1000;
unsigned clients = 0, live = 0, maximumLive = 0, secure = 0, trusted = 0, ended = 0;
bool beginOk = true, bodyOk = true, acceptBegin = true, cancelAtGet = false;
int status = 200;
uint32_t beforeBodyMs = 0, duringSinkMs = 0, connectTimeout = 0, readTimeout = 0, handshake = 0;
std::string retry = "5", encoding = "identity", url, authorization;
std::set<const void*> liveClients;
bool* cancelFlag = nullptr;
void reset() {
  assert(!live);
  now = 1000; clients = live = maximumLive = secure = trusted = ended = 0;
  beginOk = bodyOk = acceptBegin = true; cancelAtGet = false;
  status = 200; beforeBodyMs = duringSinkMs = connectTimeout = readTimeout = handshake = 0;
  retry = "5"; encoding = "identity"; url.clear(); authorization.clear(); cancelFlag = nullptr;
  assert(liveClients.empty());
}
}

uint32_t millis() { return sdk::now; }
class String {
 public:
  String(const char* value = "") : value_(value ? value : "") {}
  explicit String(std::string value) : value_(std::move(value)) {}
  size_t length() const { return value_.size(); }
  const char* c_str() const { return value_.c_str(); }
  char operator[](size_t position) const { return value_[position]; }
  bool startsWith(const char* prefix) const { return value_.compare(0, std::strlen(prefix), prefix) == 0; }
  bool endsWith(const char* suffix) const {
    const size_t length = std::strlen(suffix);
    return value_.size() >= length && value_.compare(value_.size() - length, length, suffix) == 0;
  }
  void remove(size_t position) { value_.erase(position); }
  friend String operator+(const String& left, const char* right) { return String(left.value_ + right); }
 private:
  std::string value_;
};

class Stream {
 public:
  virtual ~Stream() = default;
  virtual size_t write(uint8_t byte) = 0;
  virtual size_t write(const uint8_t* bytes, size_t length) {
    for (size_t n = 0; n < length; ++n) if (!write(bytes[n])) return n;
    return length;
  }
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int read(uint8_t*, size_t) { return -1; }
  virtual int peek() = 0;
  virtual void flush() = 0;
  void setTimeout(uint32_t) {}
};
class WiFiClient : public Stream {
 public:
  WiFiClient() {
    ++sdk::clients; ++sdk::live; sdk::maximumLive = std::max(sdk::maximumLive, sdk::live);
    sdk::liveClients.insert(this);
  }
  ~WiFiClient() override { --sdk::live; sdk::liveClients.erase(this); }
  virtual int connect(const char*, uint16_t, int32_t) { return 1; }
  virtual uint8_t connected() { return stopped_ ? 0 : 1; }
  virtual void stop() { stopped_ = true; }
  int available() override { return 1; }
  int read() override { return 0; }
  int read(uint8_t*, size_t) override { return 0; }
  int peek() override { return 0; }
  void flush() override {}
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t length) override { return length; }
 private:
  bool stopped_ = false;
};
class WiFiClientSecure : public WiFiClient {
 public:
  WiFiClientSecure() { ++sdk::secure; }
  void setHandshakeTimeout(uint32_t seconds) { sdk::handshake = seconds; }
};
namespace native_tls {
void configureTrust(WiFiClientSecure&) { ++sdk::trusted; }
}
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS = 0;
class HTTPClient {
 public:
  ~HTTPClient() { assert(!client_ || sdk::liveClients.count(client_)); }
  void setReuse(bool reuse) { assert(!reuse); }
  void setFollowRedirects(int follow) { assert(follow == HTTPC_DISABLE_FOLLOW_REDIRECTS); }
  void setConnectTimeout(uint32_t timeout) { sdk::connectTimeout = timeout; }
  void setTimeout(uint32_t timeout) { sdk::readTimeout = timeout; }
  bool begin(WiFiClient& client, const String& url) {
    client_ = &client; sdk::url = url.c_str(); return sdk::beginOk;
  }
  void collectHeaders(const char* const*, size_t length) { assert(length == 2); }
  void addHeader(const char* key, const String& value) {
    if (!std::strcmp(key, "Authorization")) sdk::authorization = value.c_str();
    if (!std::strcmp(key, "Accept")) assert(!std::strcmp(value.c_str(), "application/json"));
    if (!std::strcmp(key, "Accept-Encoding")) assert(!std::strcmp(value.c_str(), "identity"));
  }
  int GET() {
    assert(client_ && sdk::liveClients.count(client_));
    if (sdk::cancelAtGet && sdk::cancelFlag) *sdk::cancelFlag = true;
    if (!client_->connect("fixture", 80, sdk::connectTimeout)) return -1;
    return sdk::status;
  }
  String header(const char* name) const {
    if (!std::strcmp(name, "Retry-After")) return String(sdk::retry);
    if (!std::strcmp(name, "Content-Encoding")) return String(sdk::encoding);
    assert(!std::strcmp(name, "Content-Type")); return String("application/json");
  }
  int getSize() const { return 2; }
  int writeToStream(Stream* stream) {
    sdk::now += sdk::beforeBodyMs;
    if (!client_->connected()) return -1;
    const uint8_t body[] = {'{', '}'};
    return stream->write(body, sizeof(body)) == sizeof(body) ? 2 : -1;
  }
  void end() { ++sdk::ended; }
 private:
  WiFiClient* client_ = nullptr;
};

#if TEST_SELECTION_TRANSPORT
#include "../src/OfficialStudioSelection.cpp"
namespace official_selection {
#include "arduino_transport_under_test.inc"
}
namespace tested = official_selection;
#else
#include "../src/OfficialStudioHistory.cpp"
namespace official_history {
#include "arduino_transport_under_test.inc"
}
namespace tested = official_history;
#endif

class Cancel final : public tested::Cancellation {
 public:
  bool value = false;
  bool cancelled() const override { return value; }
};
class Sink final : public tested::ResponseSink {
 public:
  int status = 0; unsigned begins = 0, writes = 0;
  bool begin(const tested::Headers& headers) override {
    ++begins; status = headers.status;
    assert(!std::strcmp(headers.contentType, "application/json") && headers.contentLength == 2);
#if TEST_SELECTION_TRANSPORT
    assert(headers.retryAfterMs == (sdk::retry == "5" ? 5000U : 0U));
#else
    assert(!std::strcmp(headers.contentEncoding, sdk::encoding.c_str()));
#endif
    return sdk::acceptBegin && status == 200;
  }
  bool write(const uint8_t* bytes, size_t length) override {
    ++writes; assert(length == 2 && bytes[0] == '{' && bytes[1] == '}');
    sdk::now += sdk::duringSinkMs;
    return sdk::bodyOk;
  }
};
static bool request(bool secure, Cancel& cancel, Sink& sink) {
  tested::Input input;
  std::strcpy(input.baseUrl.data(), secure ? "https://fixture.test/" : "http://fixture.test/");
  std::strcpy(input.userJwt.data(), "fixture.jwt.token");
  tested::Request query; query.input = &input; query.path = "/api/fixture"; query.timeoutMs = 100;
  tested::ArduinoTransport transport(&cancel);
  sdk::cancelFlag = &cancel.value;
  const bool result = transport.get(query, sink);
  assert(sdk::live == 0 && sdk::clients == 1 && sdk::maximumLive == 1);
  assert(sdk::secure == unsigned(secure) && sdk::trusted == unsigned(secure));
  assert(sdk::handshake == unsigned(secure));
  return result;
}

int main() {
  for (const bool secure : {false, true}) {
    sdk::reset(); Cancel cancel; Sink sink;
    assert(request(secure, cancel, sink));
    assert(sink.status == 200 && sink.begins == 1 && sink.writes == 1 && sdk::ended == 1);
    assert(sdk::url == (secure ? "https://fixture.test/api/fixture" : "http://fixture.test/api/fixture"));
    assert(sdk::authorization == "Bearer fixture.jwt.token");
    assert(sdk::connectTimeout == 100 && sdk::readTimeout == 100);
    for (const int code : {401, 403, 404, 429, 503}) {
      sdk::reset(); Cancel denied; Sink response; sdk::status = code;
      assert(!request(secure, denied, response));
      assert(response.status == code && response.begins == 1 && !response.writes && sdk::ended == 1);
    }
    sdk::reset(); Cancel openFailure; Sink unopened; sdk::beginOk = false;
    assert(!request(secure, openFailure, unopened) && !unopened.begins);
    sdk::reset(); Cancel rejected; Sink rejectedBody; sdk::bodyOk = false;
    assert(!request(secure, rejected, rejectedBody) && rejectedBody.writes == 1);
    sdk::reset(); Cancel expired; Sink expiredBody; sdk::beforeBodyMs = 100;
    assert(!request(secure, expired, expiredBody) && !expiredBody.writes);
    sdk::reset(); Cancel late; Sink lateBody; sdk::duringSinkMs = 100;
    assert(!request(secure, late, lateBody) && lateBody.writes == 1);
    sdk::reset(); Cancel cancelled; Sink cancelledBody; sdk::cancelAtGet = true;
    assert(!request(secure, cancelled, cancelledBody) && cancelledBody.status == -1 && !cancelledBody.writes);
    sdk::reset(); Cancel wrapped; Sink wrappedBody; sdk::now = UINT32_MAX - 50; sdk::beforeBodyMs = 90;
    assert(request(secure, wrapped, wrappedBody));
    sdk::reset(); Cancel retry; Sink retryBody; sdk::retry = "not-seconds";
    assert(request(secure, retry, retryBody));
  }
  std::puts(TEST_SELECTION_TRANSPORT ?
      "Selection Arduino HTTP transport: one protocol client, borrowed lifetime, status, cancellation/deadline/wrap passed" :
      "History Arduino HTTP transport: one protocol client, borrowed lifetime, status, cancellation/deadline/wrap passed");
}
