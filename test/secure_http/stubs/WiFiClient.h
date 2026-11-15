#pragma once
// Host stub: a socket that takes writes the way a real one can. It accepts at most `perWrite`
// bytes of a write (0: all of it), answers 0 ("nothing for now") `busyRounds` times before each
// accepted piece, and takes nothing at all once `budget` bytes are in (a stall; with `dropOnStall`
// the peer is gone instead). What it took is in `received`; `reply` is what the server answers.
#include <Client.h>

#include <algorithm>
#include <string>

struct FakeSocket {
  size_t perWrite = 0;
  int busyRounds = 0;
  size_t budget = static_cast<size_t>(-1);
  bool dropOnStall = false;
  std::string reply;

  std::string received;
  int writes = 0;  // calls to write()
  int connects = 0;
  bool open = false;
  size_t replyPos = 0;
  int busyLeft = 0;

  static FakeSocket& get() {
    static FakeSocket s;
    return s;
  }
  static void reset() { get() = FakeSocket{}; }
};

class WiFiClient : public Client {
 public:
  int connect(IPAddress, uint16_t) override { return 0; }
  int connect(const char*, uint16_t) override {
    auto& s = FakeSocket::get();
    ++s.connects;
    s.open = true;
    s.replyPos = 0;
    s.busyLeft = s.busyRounds;
    return 1;
  }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t size) override {
    auto& s = FakeSocket::get();
    ++s.writes;
    if (!s.open) return 0;
    if (s.received.size() >= s.budget) {
      if (s.dropOnStall) s.open = false;
      return 0;
    }
    if (s.busyLeft > 0) {
      --s.busyLeft;
      return 0;
    }
    s.busyLeft = s.busyRounds;
    size_t n = s.perWrite ? std::min(size, s.perWrite) : size;
    n = std::min(n, s.budget - s.received.size());
    s.received.append(reinterpret_cast<const char*>(buf), n);
    return n;
  }
  int available() override {
    const auto& s = FakeSocket::get();
    return s.open ? static_cast<int>(s.reply.size() - s.replyPos) : 0;
  }
  int read() override {
    auto& s = FakeSocket::get();
    return available() > 0 ? static_cast<uint8_t>(s.reply[s.replyPos++]) : -1;
  }
  int read(uint8_t* buf, size_t size) override {
    auto& s = FakeSocket::get();
    const size_t n = std::min(size, static_cast<size_t>(available()));
    memcpy(buf, s.reply.data() + s.replyPos, n);
    s.replyPos += n;
    return n ? static_cast<int>(n) : -1;
  }
  int peek() override { return -1; }
  void flush() override {}
  void stop() override { FakeSocket::get().open = false; }
  uint8_t connected() override { return FakeSocket::get().open; }
  operator bool() override { return connected(); }
  void setConnectionTimeout(unsigned long) {}
};
