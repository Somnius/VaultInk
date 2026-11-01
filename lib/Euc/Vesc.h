#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// VESC-based boards (Floatwheel, DIY): Nordic UART, name prefix "VESC". Frames `02 <len> <payload> <crc16 hi>
// <crc16 lo> 03`, CRC16-XMODEM over the payload, big-endian values (VESCDash/source/BLE/frameDecoder.mc:24-76).
// The only request is COMM_GET_VALUES_SETUP (0x2F): `02 01 2F D5 8D 03` (VESCDash GarminEUCBleDelegate.mc:129),
// sent again 200 ms after each reply (vescData.mc:10 BLECmdDelay). Long frames (start byte 0x03, 16-bit length)
// are not handled: VESCDash does not either, and the setup reply is 70 bytes. No commands, no pairing.
namespace euc::vesc {

constexpr uint8_t kCommGetValuesSetup = 0x2F;
constexpr uint8_t kRequest[6] = {0x02, 0x01, 0x2F, 0xD5, 0x8D, 0x03};
constexpr uint32_t kPollIntervalMs = 200;
constexpr uint32_t kReplyTimeoutMs = 1000;
constexpr size_t kMaxPayload = 255;
constexpr size_t kSetupPayload = 70;  // what a VESC answers to 0x2F (VESCDash test packets: len 0x46)

// Wraps a payload: 02 len payload crc 03. Returns the size, 0 when it does not fit.
size_t buildFrame(const uint8_t* payload, size_t n, uint8_t* out, size_t cap);

class Unpacker {
 public:
  bool push(uint8_t raw);
  const uint8_t* payload() const { return buf_ + 2; }
  size_t payloadLen() const { return size_ >= 5 ? size_ - 5 : 0; }
  void reset();

 private:
  uint8_t buf_[kMaxPayload + 5] = {};
  size_t count_ = 0;
  size_t size_ = 0;
  size_t need_ = 0;
  bool collecting_ = false;
};

// Decodes a COMM_GET_VALUES_SETUP payload (first byte 0x2F) into `t` (frameDecoder.mc:86-168).
bool decodeValuesSetup(const uint8_t* p, size_t n, Telemetry& t);

}  // namespace euc::vesc
