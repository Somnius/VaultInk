#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// Veteran / Leaperkim (Sherman, Sherman S/Max, Abrams, Patton). Sources: WheelDash source/BLE/frameDecoder.mc:117-318,
// source/eucConfig.mc:141-153, eucWatch v2/euc/eucVeteran/eucVeteran.js and the Sherman S dumps in
// v2/euc/eucVeteran/info.txt.
//
// GATT: FFE0 / FFE1 like Begode. Frames: header DC 5A 5C, byte 3 = payload length `len`, frame = len + 4 bytes,
// delivered in 20-byte notifications. Firmware >= 3.012 uses len > 38 and appends a CRC32 (zlib polynomial, over
// the first `len` bytes, stored big endian) in the last 4 bytes; older firmware (len <= 38) has no checksum. Big
// endian throughout; the two u32 distances are word-swapped (low u16 first). Commands are ASCII strings.
namespace euc {

// Live byte 16-17 current scale: WheelDash divides by 10 (frameDecoder.mc:279-283), eucWatch by 100
// (eucVeteran.js:61). UNVERIFIED; /10 (the larger reading) is the default so a current alarm fires early.
constexpr float kVeteranCurrentScaleWheelDash = 0.1f;
constexpr float kVeteranCurrentScaleEucWatch = 0.01f;
constexpr float kVeteranCurrentScale = kVeteranCurrentScaleWheelDash;

// Payload length above which the frame carries a CRC32 (frameDecoder.mc:210).
constexpr uint8_t kVeteranCrcMinLen = 39;
// Firmware id (u16 at byte 28, /1000 in WheelDash) from which the wheel is a 126 V Patton (eucData.mc:107-129).
constexpr uint16_t kVeteranPattonFirmware = 4000;

uint32_t veteranCrc32(const uint8_t* data, size_t size);

class VeteranProtocol final : public Protocol {
 public:
  Brand brand() const override { return Brand::Veteran; }
  const GattProfile& profile() const override;
  const char* name() const override { return "Veteran"; }
  void start(Link& link, uint32_t nowMs) override;
  bool feed(const uint8_t* data, size_t size, Telemetry& t) override;
  void poll(Link& link, uint32_t nowMs) override;
  bool supports(Command c) const override;
  bool send(Command c, int value, Link& link) override;

 private:
  static constexpr size_t kMaxFrame = 4 + 255;
  static constexpr size_t kMinPayload = 32;  // the decoder reads up to byte 35
  enum class State : uint8_t { Sync, Length, Collect };
  bool feedByte(uint8_t c);
  void decodeFrame(Telemetry& t) const;
  void reset();

  uint8_t frame_[kMaxFrame] = {};
  size_t len_ = 0;
  size_t payload_ = 0;
  State state_ = State::Sync;
  uint8_t prev1_ = 0;
  uint8_t prev2_ = 0;
};

class VeteranSimulator final : public WheelSimulator {
 public:
  Brand brand() const override { return Brand::Veteran; }
  void onWrite(const uint8_t* data, size_t size) override;
  void tick(uint32_t nowMs, const Telemetry& t) override;
  bool pop(uint8_t* out, size_t cap, size_t& size) override;

  // New firmware (default): len 47 with CRC32, like the Sherman S dumps. Old: len 32, no CRC.
  void setNewFormat(const bool on) { newFormat_ = on; }
  // Firmware id in the frame (3012 = "3.012"); >= 4000 selects the 126 V battery curve.
  void setFirmware(const uint16_t fw) { firmware_ = fw; }
  // Last ASCII command received, for tests.
  const char* lastCommand() const { return lastCmd_; }
  size_t writeCount() const { return writes_; }

 private:
  static constexpr size_t kMaxFrame = 4 + 255;
  uint8_t frame_[kMaxFrame] = {};
  size_t size_ = 0;
  bool pending_ = false;
  bool newFormat_ = true;
  uint16_t firmware_ = 3012;
  char lastCmd_[24] = {};
  size_t writes_ = 0;
};

}  // namespace euc
