#include <SecureHttpClient.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "FakeCouch.h"
#include "LiveSyncHttp.h"

// A sync over the real HTTP client (freeink-sdk) and fake sockets: what happens to the connection
// when an answer is not read to its end (over the size cap, no memory, a body that stops). The
// server goes on sending the rest of that body, so the connection must be closed and the next
// request must go out on a new one; and every connection that was opened must be closed again.

// TLS here is a pass-through over the fake socket that counts its sessions.
namespace freeink {
SecureClient::~SecureClient() { SecureClient::stop(); }
void SecureClient::setCACert(const char*) {}
void SecureClient::setInsecure() {}
int SecureClient::connect(IPAddress, uint16_t) { return 0; }
int SecureClient::connect(const char* host, uint16_t port) {
  stop();
  _reachedServer = false;
  _connected = _transport.connect(host, port) == 1;
  _reachedServer = _connected;
  if (_connected && FakeNet::get().failTls) {
    _transport.stop();
    _connected = false;
  }
  if (_connected) {
    _ssl = this;  // a session is held until stop()
    ++FakeNet::get().tlsOpens;
  }
  return _connected ? 1 : 0;
}
size_t SecureClient::write(uint8_t b) { return write(&b, 1); }
size_t SecureClient::write(const uint8_t* buf, size_t size) { return _connected ? _transport.write(buf, size) : 0; }
int SecureClient::available() { return _connected ? _transport.available() : 0; }
int SecureClient::read() { return _transport.read(); }
int SecureClient::read(uint8_t* buf, size_t size) { return _transport.read(buf, size); }
int SecureClient::peek() { return -1; }
void SecureClient::flush() {}
void SecureClient::stop() {
  if (_ssl) {
    _ssl = nullptr;
    ++FakeNet::get().tlsCloses;
  }
  _transport.stop();
  _connected = false;
}
uint8_t SecureClient::connected() { return _connected && _transport.connected(); }
bool SecureClient::tls13Available() { return false; }
}  // namespace freeink

namespace {

using freeink::SecureHttpClient;

// How the fake server frames an answer.
struct Framing {
  bool chunked = true;       // else Content-Length
  bool closeHeader = false;  // "Connection: close"
  size_t stopAfter = 0;      // with Content-Length: send only this many body bytes, then nothing
};

std::string httpAnswer(const int status, const std::string& body, const Framing& framing) {
  std::string out = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Type: application/json\r\n";
  if (framing.closeHeader) out += "Connection: close\r\n";
  if (!framing.chunked) {
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    return out + (framing.stopAfter ? body.substr(0, framing.stopAfter) : body);
  }
  out += "Transfer-Encoding: chunked\r\n\r\n";
  char size[16];
  for (size_t at = 0; at < body.size(); at += 8000) {
    const size_t n = std::min<size_t>(8000, body.size() - at);
    snprintf(size, sizeof(size), "%zx\r\n", n);
    out += size;
    out.append(body, at, n);
    out += "\r\n";
  }
  return out + "0\r\n\r\n";
}

// The device's transport (LiveSyncDevice.cpp) with the fake CouchDB behind the fake sockets.
struct Wire {
  Fixture f;
  std::unique_ptr<SecureHttpClient> http{new SecureHttpClient()};
  size_t cap = 2 * 1024 * 1024;
  Framing framing;
  livesync::ResponseInfo info;
  int unfinished = 0;  // "connection closed after an unfinished answer"
  int tooBig = 0;
  size_t biggestAnswer = 0;
  std::vector<size_t> keysAsked;  // per chunk request for a file made here

  Wire() {
    FakeNet::reset();
    fakeNow() = 0;
    FakeNet::get().server = [this](const std::string& request) {
      const size_t methodEnd = request.find(' ');
      const size_t pathEnd = request.find(' ', methodEnd + 1);
      const std::string method = request.substr(0, methodEnd);
      const std::string path = request.substr(methodEnd + 1 + 4, pathEnd - methodEnd - 1 - 4);  // after "/db/"
      const std::string body = request.substr(request.find("\r\n\r\n") + 4);
      if (body.find("h:made") != std::string::npos) {
        JsonDocument req;
        deserializeJson(req, body);
        keysAsked.push_back(req["keys"].as<JsonArrayConst>().size());
      }
      std::string answer;
      const int status = f.couch.answer(method.c_str(), path, body, answer);
      biggestAnswer = std::max(biggestAnswer, answer.size());
      return httpAnswer(status, answer, framing);
    };
    f.net.request = [this](const char* method, const std::string& path, const std::string& body, std::string& out) {
      return request(method, path, body, out);
    };
    f.net.info = [this] { return info; };
  }

  int request(const char* method, const std::string& path, const std::string& body, std::string& out) {
    http->setTimeout(30000);
    if (!http->begin("https://couch.test/db/" + path)) return -1;
    http->addHeader("Accept", "application/json");
    livesync::CappedRequest limits;
    limits.cap = cap;
    livesync::CappedResult result;
    const int status = livesync::requestCapped(*http, method, body, out, limits, info, result);
    unfinished += result.closedUnfinished;
    tooBig += result.tooBig;
    return status;
  }

  // The end of a sync on the device (endSession): nothing may be left open after it.
  void expectNothingLeftOpen() {
    http->end();
    http.reset();
    const auto& net = FakeNet::get();
    EXPECT_EQ(net.openNow(), 0);
    EXPECT_EQ(net.opens, net.closes);
    EXPECT_EQ(net.tlsOpens, net.tlsCloses);
    EXPECT_EQ(net.tlsOpens, net.opens);
    EXPECT_EQ(net.requestsBehindUnreadAnswer, 0);
  }
};

const std::string ALL_DOCS = "_all_docs?include_docs=true";

std::string keysOf(const std::vector<std::string>& ids) {
  JsonDocument req;
  JsonArray keys = req["keys"].to<JsonArray>();
  for (const auto& id : ids) keys.add(id);
  std::string body;
  serializeJson(req, body);
  return body;
}

}  // namespace

// The fake server itself: a request written behind an answer that was not read whole gets nothing.
TEST(LiveSyncConnection, FakeServerDoesNotAnswerBehindAnUnreadBody) {
  FakeNet::reset();
  FakeNet::get().server = [](const std::string&) { return httpAnswer(200, std::string(50000, 'x'), Framing{}); };
  WiFiClient socket;
  ASSERT_EQ(socket.connect("couch.test", 443), 1);
  const std::string get = "GET /db/a HTTP/1.1\r\n\r\n";
  socket.write(reinterpret_cast<const uint8_t*>(get.data()), get.size());
  uint8_t some[1000];
  ASSERT_EQ(socket.read(some, sizeof(some)), 1000);
  const int left = socket.available();
  socket.write(reinterpret_cast<const uint8_t*>(get.data()), get.size());
  EXPECT_EQ(socket.available(), left);  // only the old body
  EXPECT_EQ(FakeNet::get().requestsBehindUnreadAnswer, 1);
  EXPECT_EQ(FakeNet::get().conns[0].requests, 1);
}

TEST(LiveSyncConnection, WholeAnswersShareOneConnection) {
  for (const bool chunked : {true, false}) {
    Wire w;
    w.framing.chunked = chunked;
    std::string out;
    for (int i = 0; i < 4; ++i) {
      ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
      EXPECT_TRUE(w.info.complete);
      EXPECT_EQ(out, w.f.couch.version);
    }
    EXPECT_EQ(FakeNet::get().opens, 1);
    EXPECT_EQ(FakeNet::get().closes, 0);
    EXPECT_EQ(w.unfinished, 0);
    w.expectNothingLeftOpen();
  }
}

TEST(LiveSyncConnection, ServerThatSaysCloseGetsANewConnectionEachTime) {
  Wire w;
  w.framing.closeHeader = true;
  std::string out;
  for (int i = 0; i < 3; ++i) ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
  EXPECT_EQ(FakeNet::get().opens, 3);
  EXPECT_EQ(FakeNet::get().requestConn, (std::vector<int>{0, 1, 2}));
  EXPECT_EQ(w.unfinished, 0);  // each answer was whole
  w.expectNothingLeftOpen();
}

// The end of a chunked body ("0", a blank line) comes later than the JSON before it. The answer is
// whole only when that end was read: the device waits for it, and the next request on the same
// connection is answered. Tried with the end split at several places.
TEST(LiveSyncConnection, LateEndOfAChunkedBodyIsWaitedFor) {
  for (const size_t held : {1u, 2u, 3u, 4u, 5u, 7u}) {  // "0\r\n\r\n" is 5; 7 holds the last chunk's CRLF too
    Wire w;
    FakeNet::get().holdBack = held;
    FakeNet::get().holdMs = 3000;
    std::string out;
    for (int i = 0; i < 3; ++i) {
      const unsigned long before = millis();
      ASSERT_EQ(w.request("GET", "_changes?since=0&limit=100", "", out), 200) << held;
      EXPECT_TRUE(w.info.complete);
      EXPECT_GE(millis() - before, 3000u);  // it did wait for the end
      JsonDocument page;
      EXPECT_TRUE(deserializeJson(page, out) == DeserializationError::Ok);
      ASSERT_EQ(w.request("POST", ALL_DOCS, keysOf({"obsydian_livesync_version"}), out), 200) << held;
      EXPECT_TRUE(w.info.complete);
      EXPECT_NE(out.find("\"rows\""), std::string::npos);
    }
    EXPECT_EQ(FakeNet::get().opens, 1) << held;
    EXPECT_EQ(w.unfinished, 0);
    w.expectNothingLeftOpen();
  }
}

// The end never comes (the JSON before it is all there and would parse): the answer is not whole,
// its connection is closed, and the next request goes out on a new one.
TEST(LiveSyncConnection, MissingEndOfAChunkedBodyReplacesTheConnection) {
  for (const size_t held : {1u, 5u, 7u}) {
    Wire w;
    FakeNet::get().holdBack = held;
    FakeNet::get().holdMs = 1000000;
    std::string out;
    EXPECT_EQ(w.request("GET", "_changes?since=0&limit=100", "", out), 200);
    EXPECT_FALSE(w.info.complete);
    JsonDocument page;
    EXPECT_TRUE(deserializeJson(page, out) == DeserializationError::Ok);  // parses, and still is not used
    EXPECT_EQ(w.unfinished, 1);
    EXPECT_EQ(FakeNet::get().openNow(), 0);
    FakeNet::get().holdBack = 0;
    ASSERT_EQ(w.request("POST", ALL_DOCS, keysOf({"obsydian_livesync_version"}), out), 200);
    EXPECT_TRUE(w.info.complete);
    EXPECT_EQ(FakeNet::get().requestConn, (std::vector<int>{0, 1}));
    w.expectNothingLeftOpen();
  }
}

// The X4 Pro case: a chunked answer past the cap. The device stops reading; the server still has
// the rest of the body to send on that connection.
TEST(LiveSyncConnection, AnswerOverTheCapClosesItsConnectionAndTheNextRequestOpensANewOne) {
  Wire w;
  w.cap = 64 * 1024;
  const auto chunks = w.f.couch.addFile(w.f.master, "big.png", noise(200 * 1024, 2), 100 * 1024);
  std::string out;
  ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
  ASSERT_EQ(w.request("POST", ALL_DOCS, keysOf(chunks), out), livesync::STATUS_TOO_BIG);
  EXPECT_FALSE(w.info.complete);
  EXPECT_TRUE(w.info.chunked);
  EXPECT_EQ(w.unfinished, 1);
  const auto& net = FakeNet::get();
  ASSERT_EQ(net.conns.size(), 1u);
  EXPECT_FALSE(net.conns[0].open);
  EXPECT_GT(net.conns[0].unreadAtClose, 100u * 1024u);  // the server was still sending
  EXPECT_EQ(net.closes, 1);
  EXPECT_EQ(net.tlsCloses, 1);

  // The next requests: a new connection, answered, and kept.
  ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
  EXPECT_EQ(out, w.f.couch.version);
  ASSERT_EQ(w.request("POST", ALL_DOCS, keysOf({chunks[0]}), out), livesync::STATUS_TOO_BIG);
  ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
  EXPECT_EQ(net.requestConn, (std::vector<int>{0, 0, 1, 1, 2}));
  EXPECT_EQ(net.opens, 3);
  EXPECT_EQ(w.unfinished, 2);
  w.expectNothingLeftOpen();
}

TEST(LiveSyncConnection, AnnouncedLengthOverTheCapAndNoMemoryCloseTheConnectionToo) {
  Wire w;
  w.cap = 64 * 1024;
  w.framing.chunked = false;
  const auto chunks = w.f.couch.addFile(w.f.master, "big.png", noise(100 * 1024, 2), 100 * 1024);
  std::string out;
  ASSERT_EQ(w.request("POST", ALL_DOCS, keysOf(chunks), out), livesync::STATUS_TOO_BIG);
  EXPECT_EQ(w.info.received, 2048u);  // stopped at the first piece: the length said enough
  EXPECT_FALSE(FakeNet::get().conns[0].open);

  // No block big enough for the answer: the same, with the other status.
  w.cap = 2 * 1024 * 1024;
  ASSERT_TRUE(w.http->begin("https://couch.test/db/" + ALL_DOCS));
  livesync::CappedRequest limits;
  limits.cap = w.cap;
  limits.largestFreeBlock = [] { return size_t{100 * 1024}; };
  livesync::CappedResult result;
  EXPECT_EQ(livesync::requestCapped(*w.http, "POST", keysOf(chunks), out, limits, w.info, result),
            livesync::STATUS_NO_MEMORY);
  EXPECT_TRUE(result.closedUnfinished);
  EXPECT_FALSE(FakeNet::get().conns[1].open);
  ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
  EXPECT_EQ(FakeNet::get().requestConn, (std::vector<int>{0, 1, 2}));
  w.expectNothingLeftOpen();
}

// A body that stops before its Content-Length (the read waits out the timeout), and a server that
// never answers: neither leaves a connection behind.
TEST(LiveSyncConnection, BodyThatStopsAndRequestWithNoAnswer) {
  Wire w;
  w.framing.chunked = false;
  w.framing.stopAfter = 10;
  std::string out;
  EXPECT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
  EXPECT_FALSE(w.info.complete);
  EXPECT_EQ(w.unfinished, 1);
  EXPECT_EQ(FakeNet::get().openNow(), 0);

  w.framing = Framing{};
  ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);  // kept, on connection 1
  const auto answering = FakeNet::get().server;
  FakeNet::get().server = [](const std::string&) { return std::string(); };
  EXPECT_EQ(w.request("GET", "obsydian_livesync_version", "", out), -1);
  EXPECT_EQ(w.http->failure(), SecureHttpClient::Failure::NoStatusLine);
  EXPECT_EQ(w.unfinished, 1);  // no answer had started: the request's own log line says so
  EXPECT_EQ(FakeNet::get().openNow(), 0);
  // Tried on the kept connection, then once more on a new one.
  EXPECT_EQ(FakeNet::get().requestConn, (std::vector<int>{0, 1, 1, 2}));

  FakeNet::get().server = answering;
  ASSERT_EQ(w.request("GET", "obsydian_livesync_version", "", out), 200);
  EXPECT_EQ(FakeNet::get().requestConn.back(), 3);
  w.expectNothingLeftOpen();
}

// A whole sync with batches too big for the device: each is asked again in halves, on a new
// connection every time, and the file comes out whole.
TEST(LiveSyncConnection, SyncAsksATooBigBatchAgainOnANewConnection) {
  Wire w;
  w.cap = 200 * 1024;
  const std::string image = noise(16 * 20 * 1024, 4);
  w.f.couch.addFile(w.f.master, "Board/a.png", image, 20 * 1024);  // 16 chunks, 8 a request
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(w.f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 14);
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(w.f.sd.files["/Vaults/Demo/Board/a.png"] == image);
  EXPECT_NE(w.f.logged("asking again, 4 per request"), "");
  ASSERT_GE(w.keysAsked.size(), 2u);
  EXPECT_EQ(w.keysAsked[0], 8u);
  EXPECT_EQ(w.keysAsked[1], 4u);
  EXPECT_EQ(w.tooBig, 1);
  EXPECT_EQ(w.unfinished, 1);
  const auto& net = FakeNet::get();
  EXPECT_EQ(net.opens, 2);  // one more than there were unfinished answers
  EXPECT_GT(net.conns[0].unreadAtClose, 0u);
  EXPECT_EQ(net.openNow(), 1);  // the second one is kept for the rest of the sync
  w.expectNothingLeftOpen();
}

// A picture in pieces of about 1 MB, one piece per request, under the device's 2 MB cap.
TEST(LiveSyncConnection, FileInOneMegabyteChunksCompletes) {
  Wire w;
  const std::string image = noise(3 * 1000 * 1000 + 5000, 9);
  w.f.couch.addFile(w.f.master, "Board/Images/Image 13.png", image, 1000 * 1000);
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(w.f.sync(stats, error)) << error;
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(w.f.sd.files["/Vaults/Demo/Board/Images/Image 13.png"] == image);
  EXPECT_EQ(w.keysAsked, (std::vector<size_t>{1, 1, 1, 1}));
  EXPECT_GT(w.biggestAnswer, 1300u * 1024u);
  EXPECT_LT(w.biggestAnswer, w.cap);
  EXPECT_EQ(w.tooBig, 0);
  EXPECT_EQ(FakeNet::get().opens, 1);
  EXPECT_TRUE(w.f.log.empty());
  w.expectNothingLeftOpen();
}

// What the X4 Pro log showed: ONE chunk whose answer passes the cap. There is nothing to halve, so
// that file is left out (and listed); the files after it still arrive, on new connections.
TEST(LiveSyncConnection, SingleChunkOverTheCapSkipsOnlyThatFile) {
  Wire w;
  const std::string big = noise(2 * 1300 * 1000, 5), small = noise(40 * 1024, 6);
  w.f.couch.addFile(w.f.master, "Board/Images/Image 13 - Kara.png", big, 1300 * 1000);
  w.f.couch.addFile(w.f.master, "Board/after.png", small, 20 * 1024);
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(w.f.sync(stats, error)) << error;
  EXPECT_GT(w.biggestAnswer, w.cap);
  EXPECT_EQ(stats.skipped, 1);
  EXPECT_EQ(stats.written, 14);
  EXPECT_NE(w.f.sd.files[w.f.skipList].find("Image 13 - Kara.png\tthe server's answer was too big"), std::string::npos);
  EXPECT_EQ(w.f.logged("asking again"), "");
  EXPECT_FALSE(w.f.sd.files.count("/Vaults/Demo/Board/Images/Image 13 - Kara.png"));
  EXPECT_TRUE(w.f.sd.files["/Vaults/Demo/Board/after.png"] == small);
  EXPECT_EQ(w.tooBig, 1);
  EXPECT_EQ(w.unfinished, 1);
  EXPECT_EQ(FakeNet::get().opens, 2);
  w.expectNothingLeftOpen();
}

// The message on the result screen follows the cause: the certificate hint only for a TLS
// handshake that fails before anything was answered, while the certificate is checked.
TEST(LiveSyncConnection, FailureCauseFollowsWhereTheRequestStopped) {
  using livesync::FailureCause;
  using Failure = SecureHttpClient::Failure;
  const auto causeOf = [](Wire& w, const int status, const bool checked, const bool workedBefore) {
    return livesync::failureCause(status, w.info.complete, w.http->failure() == Failure::Connect,
                                  w.http->failure() == Failure::Handshake, checked, workedBefore);
  };
  std::string out;
  {
    Wire w;  // a certificate that is not accepted
    FakeNet::get().failTls = true;
    const int status = w.request("GET", "obsydian_livesync_version", "", out);
    EXPECT_EQ(status, -1);
    EXPECT_EQ(w.http->failure(), Failure::Handshake);
    EXPECT_EQ(causeOf(w, status, true, false), FailureCause::Certificate);
    EXPECT_EQ(causeOf(w, status, false, false), FailureCause::Unreachable);  // not checked: not about it
    EXPECT_EQ(causeOf(w, status, true, true), FailureCause::Unreachable);    // it was accepted before
    EXPECT_EQ(FakeNet::get().openNow(), 0);
    EXPECT_EQ(FakeNet::get().opens, FakeNet::get().closes);
  }
  {
    Wire w;  // no network, a wrong address
    FakeNet::get().refuseTcp = true;
    const int status = w.request("GET", "obsydian_livesync_version", "", out);
    EXPECT_EQ(status, -1);
    EXPECT_EQ(w.http->failure(), Failure::Connect);
    EXPECT_EQ(causeOf(w, status, true, false), FailureCause::Unreachable);
    EXPECT_EQ(FakeNet::get().opens, 0);
  }
  {
    Wire w;  // the reader's case: requests were answered, then one gets nothing
    int status = w.request("GET", "obsydian_livesync_version", "", out);
    EXPECT_EQ(causeOf(w, status, true, false), FailureCause::None);
    FakeNet::get().server = [](const std::string&) { return std::string(); };
    status = w.request("POST", ALL_DOCS, keysOf({"a"}), out);
    EXPECT_EQ(status, -1);
    EXPECT_EQ(causeOf(w, status, true, true), FailureCause::NoAnswer);
    EXPECT_EQ(causeOf(w, status, true, false), FailureCause::NoAnswer);
    // And then the server cannot be reached any more.
    FakeNet::get().refuseTcp = true;
    status = w.request("GET", "obsydian_livesync_version", "", out);
    EXPECT_EQ(causeOf(w, status, true, true), FailureCause::Unreachable);
    w.expectNothingLeftOpen();
  }
  {
    Wire w;  // an answer that stops half way
    w.framing.chunked = false;
    w.framing.stopAfter = 10;
    const int status = w.request("GET", "obsydian_livesync_version", "", out);
    EXPECT_EQ(causeOf(w, status, true, false), FailureCause::NoAnswer);
  }
  EXPECT_EQ(livesync::failureCause(livesync::STATUS_TOO_BIG, false, false, false, true, false), FailureCause::None);
  EXPECT_EQ(livesync::failureCause(404, true, false, false, true, false), FailureCause::None);
}

// A change list whose end never came is not applied, even though its JSON parses: the checkpoint
// stays, and the next sync (a new connection) brings everything.
TEST(LiveSyncConnection, ChangeListWithoutItsEndIsNotUsed) {
  Wire w;
  const auto answering = FakeNet::get().server;
  FakeNet::get().server = [answering](const std::string& request) {
    FakeNet::get().holdBack = request.find("_changes") != std::string::npos ? 5 : 0;
    return answering(request);
  };
  FakeNet::get().holdMs = 1000000;
  livesync::SyncStats stats;
  std::string error;
  EXPECT_FALSE(w.f.sync(stats, error));
  EXPECT_EQ(error, "The change list was cut short.");
  EXPECT_NE(w.f.logged("changes: HTTP 200").find("body ended early"), std::string::npos);
  EXPECT_EQ(stats.written, 0);
  EXPECT_EQ(w.f.checkpoint(), "");
  EXPECT_EQ(w.unfinished, 1);
  EXPECT_EQ(FakeNet::get().openNow(), 0);

  FakeNet::get().server = answering;
  FakeNet::get().holdBack = 0;
  ASSERT_TRUE(w.f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 13);
  w.expectNothingLeftOpen();
}
