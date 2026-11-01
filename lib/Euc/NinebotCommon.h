#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "Euc.h"

// Ninebot One S1/S2/A1 (Nordic UART, address 0x11) and One E+/C/P (FFE0/FFE1, address 0x09) read registers:
// request `55 AA 03 <addr> 01 <reg> <n> <ck lo> <ck hi>`, checksum = 0xFFFF - sum(len..n), little-endian
// (every constant in eucNinebotS.js:5-33 and eucNinebotE.js:5-33 reproduces with that rule, see CrcTest.cpp).
// Replies are read as `55 AA <len> <src> <cmd> <reg> <data[n]> <ck lo> <ck hi>`: eucWatch only reads byte 5
// (register) and the value from byte 6 (eucNinebotS.js:61-62). UNVERIFIED: the reply length and checksum
// convention (len = n + 2, total = len + 6, same checksum rule as the requests) is inferred from the requests;
// kVerifyReplyChecksum turns the check off if a real wheel disagrees.
//
// No commands: the sources only show lock/unlock, units, ring and riding-mode writes, none of which is in the
// safe set, so supports() is false for everything.
namespace euc::ninebot {

constexpr uint8_t kAddrS = 0x11;
constexpr uint8_t kAddrE = 0x09;
constexpr uint8_t kCmdRead = 0x01;

// Registers (eucNinebotS.js:5-33, comments and decoders :65-125).
constexpr uint8_t kRegRemaining = 0x25;  // km * 100
constexpr uint8_t kRegSpeed = 0x26;      // km/h * 1000
constexpr uint8_t kRegTotal = 0x29;      // m, u32
constexpr uint8_t kRegRideTime = 0x3A;   // s
constexpr uint8_t kRegTemp = 0x3E;       // C * 10
constexpr uint8_t kRegVoltage = 0x47;    // V * 100
constexpr uint8_t kRegCurrent = 0x50;    // A * 100, signed
constexpr uint8_t kRegLock = 0x70;
constexpr uint8_t kRegAvgSpeed = 0xB6;  // km/h * 1000
constexpr uint8_t kRegTrip = 0xB9;      // eucWatch divides by 100 for km, its comment says metres
constexpr uint8_t kRegRideMode = 0xD2;

constexpr bool kVerifyReplyChecksum = true;
// Trip (0xB9): eucNinebotS.js:91 divides by 100 (km), its comment on line 11 says metres. UNVERIFIED; the code
// wins (100), flip to 1000 for metres.
constexpr float kTripDivisor = 100.0f;

constexpr size_t kMaxFrame = 64;
constexpr size_t kRequestSize = 9;
// eucWatch polls a 21-step register loop, one request every 50 ms (eucNinebotS.js:171-176).
constexpr size_t kLoopSteps = 21;
constexpr uint32_t kStepIntervalMs = 50;

// The register and byte count for a loop step (eucNinebotS.js:5-17). Step 14 alternates total/remaining.
void loopRequest(size_t step, bool alternate, uint8_t& reg, uint8_t& count);

// Builds the 9-byte read request. Returns 9.
size_t buildRead(uint8_t addr, uint8_t reg, uint8_t count, uint8_t* out, size_t cap);
// Builds a reply frame `55 AA n+2 src 04 reg data ck`. Returns the size, 0 when it does not fit.
size_t buildReply(uint8_t src, uint8_t reg, const uint8_t* data, size_t n, uint8_t* out, size_t cap);

// Reassembles `55 AA` frames of len + 6 bytes from any chunking and verifies the checksum.
class Unpacker {
 public:
  bool push(uint8_t raw);
  const uint8_t* frame() const { return buf_; }
  size_t size() const { return size_; }
  uint8_t reg() const { return buf_[5]; }
  const uint8_t* data() const { return buf_ + 6; }
  size_t dataLen() const { return size_ >= 8 ? size_ - 8 : 0; }
  void reset();

 private:
  uint8_t buf_[kMaxFrame] = {};
  size_t count_ = 0;
  size_t size_ = 0;
  size_t need_ = 0;
  uint8_t prev_ = 0;
  bool collecting_ = false;
};

// Decodes one verified reply into `t`. Returns true when something changed.
bool decodeReply(const Unpacker& in, Telemetry& t);

struct Variant {
  Brand brand;
  const GattProfile* profile;
  const char* name;
  uint8_t addr;
};

std::unique_ptr<Protocol> makeRegisterProtocol(const Variant& v);
std::unique_ptr<WheelSimulator> makeRegisterSim(const Variant& v);

}  // namespace euc::ninebot
