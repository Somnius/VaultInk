#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// Begode / Gotway (Mten3, MCM5, Tesla, Nikola, MSuper Pro, MSP/RS, EX, EXN, Monster, Master, ExtremeBull).
// Sources: WheelDash source/BLE/frameDecoder.mc:27-115, source/eucConfig.mc:43-85 and eucWatch
// v2/euc/eucBegode/eucBegode.js (both in .work/euc-repos and ~/dev/euc-related/eucwatch-compare).
//
// GATT: service FFE0, one characteristic FFE1 for notify and write-without-response. Frames: 24 bytes, header
// 55 AA, byte 18 = type (0 live, 4 totals/settings, 1 "Master" PWM), byte 19 = 0x18, trailer 5A 5A 5A 5A, big
// endian, no checksum. The model ('N') and firmware ('V') replies are plain text notifications "NAME:xxx" and
// "GWxxx". All commands are single ASCII bytes.
namespace euc {

// Frame A byte 10-11 current scale. WheelDash (frameDecoder.mc:111) and WheelLog divide by 100, eucWatch
// (eucBegode.js:198) by 1000. UNVERIFIED which one current firmware uses; the larger reading (/100) is the default
// so a current alarm fires early rather than late.
constexpr float kBegodeCurrentScaleWheelDash = 0.01f;
constexpr float kBegodeCurrentScaleEucWatch = 0.001f;
constexpr float kBegodeCurrentScale = kBegodeCurrentScaleWheelDash;

// Frame A byte 14-15: WheelDash reads it as PWM / 100 (frameDecoder.mc:113) but then ignores it for Begode and
// computes PWM from a free-spin test (eucData.mc:219-232); eucWatch reads the same word as a reset counter
// (eucBegode.js:212). UNVERIFIED: F_PWM is never set for Begode (safety note in .work/euc-research.md section 3).
constexpr float kBegodePwmScaleWheelDash = 0.01f;

// Pack voltage multiplier per model name (eucBegode.js:43-63): 1 = 67.2 V, 1.25 = 84 V, 1.5 = 100.8 V, 2 = 134.4 V.
float begodeVoltageMultiplier(const char* model);

class BegodeProtocol final : public Protocol {
 public:
  Brand brand() const override { return Brand::Begode; }
  const GattProfile& profile() const override;
  const char* name() const override { return "Begode"; }
  void start(Link& link, uint32_t nowMs) override;
  bool feed(const uint8_t* data, size_t size, Telemetry& t) override;
  void poll(Link& link, uint32_t nowMs) override;
  bool supports(Command c) const override;
  bool send(Command c, int value, Link& link) override;

  // The voltage multiplier in use (from the 'N' reply, 1.0 until the wheel names itself).
  float voltageMultiplier() const { return voltMult_; }

 private:
  static constexpr size_t kFrame = 24;
  bool feedByte(uint8_t c);
  bool textReply(const uint8_t* data, size_t size, Telemetry& t);
  bool decodeFrame(Telemetry& t) const;

  uint8_t frame_[kFrame] = {};
  size_t len_ = 0;
  bool collecting_ = false;
  uint8_t prev_ = 0;
  float voltMult_ = 1.0f;
};

class BegodeSimulator final : public WheelSimulator {
 public:
  Brand brand() const override { return Brand::Begode; }
  void onWrite(const uint8_t* data, size_t size) override;
  void tick(uint32_t nowMs, const Telemetry& t) override;
  bool pop(uint8_t* out, size_t cap, size_t& size) override;

  // The model the fake wheel answers to 'N' with (sets its voltage multiplier). Default "Mten3" (84 V).
  void setModel(const char* model);
  void setFirmware(const char* firmware);
  // What the app wrote, for tests.
  uint8_t lastWrite() const { return lastWrite_; }
  size_t writeCount() const { return writes_; }

 private:
  static constexpr size_t kQueue = 4;
  static constexpr size_t kMaxFrame = 24;
  void push(const uint8_t* frame, size_t size);

  uint8_t queue_[kQueue][kMaxFrame] = {};
  size_t sizes_[kQueue] = {};
  size_t head_ = 0;
  size_t count_ = 0;
  char model_[24] = "Mten3";
  char firmware_[16] = "GW2.01";
  uint8_t lastWrite_ = 0;
  size_t writes_ = 0;
};

}  // namespace euc
