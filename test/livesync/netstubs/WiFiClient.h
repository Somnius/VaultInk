#pragma once
// Host stub: sockets to a fake server, one connection per connect(). The server answers a request
// when it has it whole (the headers and the Content-Length bytes), and only when the answer before
// it on that connection was read to its end: like a real server, it does not look at a request while
// it is still sending the last body. Every open and close is counted.
#include <Client.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

struct FakeNet {
  struct Conn {
    std::string in;      // written by the device, not yet a whole request
    std::string out;     // the server's bytes
    size_t outPos = 0;   // read by the device so far
    bool open = true;
    std::string late;  // the end of the answer, still on its way (see holdBack)
    unsigned long lateAt = 0;
    int requests = 0;    // answered here
    size_t unreadAtClose = 0;
  };
  std::vector<Conn> conns;
  int opens = 0;
  int closes = 0;
  int tlsOpens = 0;
  int tlsCloses = 0;
  int requestsBehindUnreadAnswer = 0;  // written to a connection whose last answer was not read whole
  std::vector<int> requestConn;        // the connection each answered request came on
  // The last `holdBack` bytes of each answer reach the device only `holdMs` later.
  bool refuseTcp = false;  // no connection can be made
  bool failTls = false;    // the connection is made, the TLS handshake fails
  size_t holdBack = 0;
  unsigned long holdMs = 0;
  // The whole request (head and body) in, the whole HTTP answer out.
  std::function<std::string(const std::string& request)> server;

  int openNow() const {
    return static_cast<int>(std::count_if(conns.begin(), conns.end(), [](const Conn& c) { return c.open; }));
  }
  static FakeNet& get() {
    static FakeNet n;
    return n;
  }
  static void reset() { get() = FakeNet{}; }
};

class WiFiClient : public Client {
 public:
  ~WiFiClient() override { WiFiClient::stop(); }
  int connect(IPAddress, uint16_t) override { return 0; }
  int connect(const char*, uint16_t) override {
    stop();
    auto& net = FakeNet::get();
    if (net.refuseTcp) return 0;
    id_ = static_cast<int>(net.conns.size());
    net.conns.emplace_back();
    ++net.opens;
    return 1;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t size) override {
    if (!connected()) return 0;
    auto& net = FakeNet::get();
    auto& c = net.conns[id_];
    c.in.append(reinterpret_cast<const char*>(buf), size);
    const size_t headEnd = c.in.find("\r\n\r\n");
    if (headEnd == std::string::npos) return size;
    size_t bodyLen = 0;
    const size_t at = c.in.find("Content-Length: ");
    if (at != std::string::npos && at < headEnd) bodyLen = std::stoul(c.in.substr(at + 16));
    if (c.in.size() < headEnd + 4 + bodyLen) return size;
    const std::string request = c.in.substr(0, headEnd + 4 + bodyLen);
    c.in.erase(0, request.size());
    if (c.outPos < c.out.size() || !c.late.empty()) {
      ++net.requestsBehindUnreadAnswer;  // never answered: the old body is still on its way
      return size;
    }
    ++c.requests;
    net.requestConn.push_back(id_);
    c.out += net.server(request);
    if (net.holdBack && net.holdBack < c.out.size() - c.outPos) {
      c.late = c.out.substr(c.out.size() - net.holdBack);
      c.out.erase(c.out.size() - net.holdBack);
      c.lateAt = millis() + net.holdMs;
    }
    return size;
  }
  int available() override {
    if (!connected()) return 0;
    auto& c = FakeNet::get().conns[id_];
    if (!c.late.empty() && static_cast<long>(millis() - c.lateAt) >= 0) {
      c.out += c.late;
      c.late.clear();
    }
    return static_cast<int>(c.out.size() - c.outPos);
  }
  int read() override {
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
  }
  int read(uint8_t* buf, size_t size) override {
    const size_t n = std::min(size, static_cast<size_t>(available()));
    if (n == 0) return -1;
    auto& c = FakeNet::get().conns[id_];
    memcpy(buf, c.out.data() + c.outPos, n);
    c.outPos += n;
    return static_cast<int>(n);
  }
  int peek() override { return -1; }
  void flush() override {}
  void stop() override {
    if (id_ < 0) return;
    auto& net = FakeNet::get();
    if (id_ < static_cast<int>(net.conns.size()) && net.conns[id_].open) {
      net.conns[id_].open = false;
      net.conns[id_].unreadAtClose = net.conns[id_].out.size() - net.conns[id_].outPos + net.conns[id_].late.size();
      ++net.closes;
    }
    id_ = -1;
  }
  uint8_t connected() override {
    const auto& net = FakeNet::get();
    return id_ >= 0 && id_ < static_cast<int>(net.conns.size()) && net.conns[id_].open;
  }
  operator bool() override { return connected(); }
  void setConnectionTimeout(unsigned long) {}

 private:
  int id_ = -1;
};
