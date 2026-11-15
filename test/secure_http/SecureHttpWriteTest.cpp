#include <SecureHttpClient.h>
#include <gtest/gtest.h>

#include <string>

// The body of a POST or PUT must reach the server whole. A socket may take a part of a write, or
// nothing for a while; the client has to offer the rest again, and say how far it got when the
// socket stops taking bytes. (A request that announces a Content-Length and sends less leaves a
// proxy waiting for the rest: it never reaches the server behind it.)

// TLS here is a fake over the same fake socket: like wolfSSL it takes a whole piece or nothing, and
// after "nothing for now" it must be called again with the same buffer and size.
namespace {
struct FakeTls {
  int wouldBlock = 0;   // answers "nothing for now" this many times before each piece
  bool broken = false;  // a TLS error: 0 without writeWouldBlock()
  int left = 0;
  const uint8_t* lastBuf = nullptr;
  size_t lastSize = 0;
  bool sameArgsOnRetry = true;
  int retries = 0;
} tls;
}  // namespace

namespace freeink {
SecureClient::~SecureClient() = default;
void SecureClient::setCACert(const char*) {}
void SecureClient::setInsecure() {}
int SecureClient::connect(IPAddress, uint16_t) { return 0; }
int SecureClient::connect(const char* host, uint16_t port) {
  _connected = _transport.connect(host, port) == 1;
  tls.left = tls.wouldBlock;
  return _connected ? 1 : 0;
}
size_t SecureClient::write(uint8_t b) { return write(&b, 1); }
size_t SecureClient::write(const uint8_t* buf, size_t size) {
  _writeWouldBlock = false;
  if (tls.broken) return 0;
  if (tls.lastBuf) {
    ++tls.retries;
    if (tls.lastBuf != buf || tls.lastSize != size) tls.sameArgsOnRetry = false;
  }
  tls.lastBuf = buf;
  tls.lastSize = size;
  if (tls.left > 0) {
    --tls.left;
    _writeWouldBlock = true;
    return 0;
  }
  // All of the piece or none of it: the socket below may take it in parts or make it wait.
  size_t done = 0;
  while (done < size) {
    const size_t n = _transport.write(buf + done, size - done);
    if (n == 0) {
      if (!_transport.connected()) return 0;
      delay(1);
      continue;
    }
    done += n;
  }
  tls.left = tls.wouldBlock;
  tls.lastBuf = nullptr;
  return size;
}
int SecureClient::available() { return _connected ? _transport.available() : 0; }
int SecureClient::read() { return _transport.read(); }
int SecureClient::read(uint8_t* buf, size_t size) { return _transport.read(buf, size); }
int SecureClient::peek() { return -1; }
void SecureClient::flush() {}
void SecureClient::stop() {
  _transport.stop();
  _connected = false;
}
uint8_t SecureClient::connected() { return _connected && _transport.connected(); }
bool SecureClient::tls13Available() { return false; }
}  // namespace freeink

namespace {

using freeink::SecureHttpClient;
using Failure = SecureHttpClient::Failure;

constexpr char OK_REPLY[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
constexpr char PLAIN[] = "http://couch.test/db/_all_docs";
constexpr char SECURE[] = "https://couch.test/db/_all_docs";

std::string bodyOf(size_t n) {
  std::string s(n, '\0');
  for (size_t i = 0; i < n; ++i) s[i] = static_cast<char>('a' + i % 23);
  return s;
}

struct Sent {
  std::string head, body;
  size_t contentLength = 0;
};

// What the fake server got, split at the blank line.
Sent sent() {
  const std::string& all = FakeSocket::get().received;
  Sent s;
  const size_t end = all.find("\r\n\r\n");
  if (end == std::string::npos) {
    s.head = all;
    return s;
  }
  s.head = all.substr(0, end + 4);
  s.body = all.substr(end + 4);
  const size_t at = s.head.find("Content-Length: ");
  if (at != std::string::npos) s.contentLength = std::stoul(s.head.substr(at + 16));
  return s;
}

void fresh() {
  FakeSocket::reset();
  FakeSocket::get().reply = OK_REPLY;
  tls = FakeTls{};
  fakeNow() = 0;
}

int post(SecureHttpClient& http, const char* url, const std::string& body, const std::function<void()>& onPoll = {}) {
  http.setTimeout(30000);
  EXPECT_TRUE(http.begin(url));
  http.addHeader("Content-Type", "application/json");
  return http.sendRequest(
      "POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(), [](const uint8_t*, size_t) { return true; },
      [&onPoll] {
        if (onPoll) onPoll();
        return false;
      });
}

TEST(SecureHttpWrite, SocketThatTakes512BytesAWriteGetsTheWholeBody) {
  for (const char* url : {PLAIN, SECURE}) {
    fresh();
    FakeSocket::get().perWrite = 512;
    const std::string body = bodyOf(10 * 1024 + 37);
    SecureHttpClient http;
    EXPECT_EQ(post(http, url, body), 200) << url;
    const Sent s = sent();
    EXPECT_EQ(s.contentLength, body.size()) << url;
    EXPECT_TRUE(s.body == body) << url << ": " << s.body.size() << " of " << body.size();
    EXPECT_EQ(http.failure(), Failure::None);
    EXPECT_EQ(http.requestBodySent(), body.size());
    EXPECT_EQ(http.requestBodyLength(), body.size());
    EXPECT_GT(FakeSocket::get().writes, 20);
  }
}

TEST(SecureHttpWrite, SocketThatIsBusyNowAndThenStillGetsTheWholeBody) {
  for (const char* url : {PLAIN, SECURE}) {
    fresh();
    FakeSocket::get().perWrite = 700;
    FakeSocket::get().busyRounds = 3;  // "nothing for now" three times before each piece
    tls.wouldBlock = 2;
    const std::string body = bodyOf(9000);
    SecureHttpClient http;
    int polls = 0;
    EXPECT_EQ(post(http, url, body, [&polls] { ++polls; }), 200) << url;
    const Sent s = sent();
    EXPECT_EQ(s.contentLength, body.size());
    EXPECT_TRUE(s.body == body) << url << ": " << s.body.size() << " of " << body.size();
    EXPECT_GT(polls, 3);  // the caller's callback (it feeds the watchdog) ran while waiting
    EXPECT_LT(millis(), 30000ul);
    if (url == SECURE) {
      EXPECT_GT(tls.retries, 4);
      EXPECT_TRUE(tls.sameArgsOnRetry);  // TLS was offered the same piece again, never a shifted one
    }
  }
}

TEST(SecureHttpWrite, StalledSocketFailsAfterTheTimeoutAndSaysHowFarItGot) {
  // The length of the request head, from a request that goes through.
  fresh();
  const std::string body = bodyOf(10 * 1024);
  size_t head = 0;
  {
    SecureHttpClient http;
    ASSERT_EQ(post(http, PLAIN, body), 200);
    head = sent().head.size();
  }
  fresh();
  FakeSocket::get().perWrite = 512;
  FakeSocket::get().budget = head + 1000;  // then the socket takes nothing more, and stays open
  SecureHttpClient http;
  int polls = 0;
  EXPECT_EQ(post(http, PLAIN, body, [&polls] { ++polls; }), -1);
  EXPECT_EQ(http.failure(), Failure::WriteBody);
  EXPECT_EQ(http.requestBodySent(), 1000u);
  EXPECT_EQ(http.requestBodyLength(), body.size());
  EXPECT_GE(millis(), 30000ul);  // it waited the timeout out,
  EXPECT_LT(millis(), 31000ul);  // once: a fresh connection is not asked again
  EXPECT_GT(polls, 100);
  EXPECT_FALSE(FakeSocket::get().open);  // the half-sent request does not stay on an open connection
  EXPECT_EQ(FakeSocket::get().connects, 1);
  EXPECT_EQ(sent().body.size(), 1000u);
}

TEST(SecureHttpWrite, StalledHeadIsItsOwnFailure) {
  fresh();
  FakeSocket::get().budget = 10;
  SecureHttpClient http;
  EXPECT_EQ(post(http, PLAIN, bodyOf(100)), -1);
  EXPECT_EQ(http.failure(), Failure::WriteHeaders);
  EXPECT_EQ(http.requestBodySent(), 0u);
  EXPECT_EQ(http.requestBodyLength(), 100u);
}

TEST(SecureHttpWrite, ClosedConnectionOrTlsErrorFailsAtOnce) {
  fresh();
  FakeSocket::get().budget = 300;
  FakeSocket::get().dropOnStall = true;  // the peer closed in the middle of the body
  {
    SecureHttpClient http;
    EXPECT_EQ(post(http, PLAIN, bodyOf(5000)), -1);
    EXPECT_EQ(http.failure(), Failure::WriteBody);
    EXPECT_LT(http.requestBodySent(), 300u);
    EXPECT_LT(millis(), 1000ul);
  }
  fresh();
  tls.broken = true;
  SecureHttpClient http;
  EXPECT_EQ(post(http, SECURE, bodyOf(5000)), -1);
  EXPECT_EQ(http.failure(), Failure::WriteHeaders);
  EXPECT_LT(millis(), 1000ul);
}

TEST(SecureHttpWrite, WholeRequestSentAndNoAnswerIsToldApart) {
  fresh();
  FakeSocket::get().reply.clear();  // the server never answers
  const std::string body = bodyOf(3000);
  SecureHttpClient http;
  EXPECT_EQ(post(http, PLAIN, body), -1);
  EXPECT_EQ(http.failure(), Failure::NoStatusLine);
  EXPECT_EQ(http.requestBodySent(), body.size());
  EXPECT_TRUE(sent().body == body);
}

TEST(SecureHttpWrite, KeptConnectionTheServerClosedIsStillTriedAgainOnce) {
  fresh();
  SecureHttpClient http;
  const std::string body = bodyOf(2000);
  ASSERT_EQ(post(http, PLAIN, body), 200);
  // The server has closed the kept connection; the next request finds out at its first write.
  auto& socket = FakeSocket::get();
  socket.received.clear();
  socket.budget = 0;
  socket.dropOnStall = true;
  EXPECT_EQ(post(http, PLAIN, body,
                 [&socket] {
                   if (!socket.open) socket.budget = static_cast<size_t>(-1);  // the new connection works
                 }),
            200);
  EXPECT_EQ(socket.connects, 2);
  EXPECT_TRUE(sent().body == body);
  EXPECT_EQ(http.failure(), Failure::None);
}

}  // namespace
