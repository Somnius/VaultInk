#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// Ninebot One Z6/Z8/Z10: Nordic UART, read only. eucWatch writes `5A A5 01 3E 14 01 B0 20 DB FE` every 50 ms
// (eucNinebotZ.js:6, 127-132) and reads the 20-byte notifications one by one: the first (starting 5A) gives the
// speed at byte 17, the second gives total/ride time/temperature/voltage/current/speed/average from byte 1
// (eucNinebotZ.js:39-80). Here the two chunks are reassembled into the one reply they are:
// `5A A5 <len=0x20> <src> <dst> <cmd> <arg> <data[32]> <ck lo> <ck hi>` (41 bytes, data at byte 7), so the
// eucWatch offsets become data offsets 10, 14, 20, 22, 24, 26, 28, 30. The checksum rule is the one that produces
// DB FE for the request (0xFFFF - sum from the length byte to the last data byte).
//
// UNVERIFIED: the reassembled layout and the reply checksum rest on eucWatch's per-chunk reading; WheelLog's
// NinebotZAdapter does a key exchange first and eucWatch skips it (eucwatch research, survey 1.6). Needs a Z10.
// No commands.
namespace euc::ninebotz {

constexpr uint8_t kRequest[10] = {0x5A, 0xA5, 0x01, 0x3E, 0x14, 0x01, 0xB0, 0x20, 0xDB, 0xFE};
constexpr uint32_t kPollIntervalMs = 50;
constexpr uint32_t kReplyTimeoutMs = 500;
constexpr size_t kHeader = 7;  // 5A A5 len src dst cmd arg
constexpr size_t kMaxFrame = 80;
constexpr size_t kLiveDataLen = 0x20;

// Data offsets inside the reply (eucWatch chunk index -> data index).
constexpr size_t kOffSpeedFirst = 10;  // chunk 1 byte 17
constexpr size_t kOffTotal = 14;       // chunk 2 byte 1, u32 metres
constexpr size_t kOffRideTime = 20;    // u16 s
constexpr size_t kOffTemp = 22;        // u16 C*10
constexpr size_t kOffVoltage = 24;     // u16 V*100
constexpr size_t kOffCurrent = 26;     // s16 A*100
constexpr size_t kOffSpeed = 28;       // u16 km/h*100
constexpr size_t kOffAvgSpeed = 30;    // u16 km/h*100

// Builds a reply frame for `data` of n bytes. Returns the size, 0 when it does not fit.
size_t buildReply(const uint8_t* data, size_t n, uint8_t* out, size_t cap);

class Unpacker {
 public:
  bool push(uint8_t raw);
  const uint8_t* data() const { return buf_ + kHeader; }
  size_t dataLen() const { return size_ >= kHeader + 2 ? size_ - kHeader - 2 : 0; }
  void reset();

 private:
  uint8_t buf_[kMaxFrame] = {};
  size_t count_ = 0;
  size_t size_ = 0;
  size_t need_ = 0;
  uint8_t prev_ = 0;
  bool collecting_ = false;
};

}  // namespace euc::ninebotz
