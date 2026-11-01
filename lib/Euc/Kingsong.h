#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// Kingsong (14D/14S/16S, 18L/18XL/16X/S18, S19, S20/S22). Sources: WheelDash source/BLE/frameDecoder.mc:320-470,
// source/BLE/GarminEUCBleDelegate.mc:106-136, source/GarminEUCMenu2Delegate_generic.mc:267-298,
// source/eucData.mc:134-190 and eucWatch v2/euc/eucKingsong/eucKingsong.js.
//
// GATT: FFE0 / FFE1 notify + write. Frames: fixed 20 bytes AA 55 + 14 data bytes + byte 16 = type + 14 5A 5A,
// little endian, no checksum. Requests use the same layout with everything zero but the type. Frames starting 0xBC
// and the BMS frames (F1/F2, E1/E2, E5/E6) are ignored. The lock handshake (0x5F/0x5D/0x47) and the password
// commands (0x41/0x42/0x44) are out of scope and never sent.
namespace euc {

// Pedal mode request byte 17: WheelDash and WheelLog write 0x15 (GarminEUCMenu2Delegate_generic.mc:292), eucWatch
// leaves the usual 0x14 (eucKingsong.js:77). UNVERIFIED; 0x15 (the two sources that agree) is the default.
constexpr uint8_t kKingsongPedalByte17 = 0x15;

// Pack nominal voltage for a model name from the 0xBB frame ("KS-S22"), per eucData.mc:134-190.
float kingsongPackVolts(const char* model);

// The wheel's own speed alarm settings (0xA4 / 0xB5 reply), km/h; 0 = off. Not Telemetry, read with alarms().
struct KingsongAlarms {
  uint8_t alarm1 = 0;
  uint8_t alarm2 = 0;
  uint8_t alarm3 = 0;
  uint8_t tiltback = 0;
  bool valid = false;
};

class KingsongProtocol final : public Protocol {
 public:
  Brand brand() const override { return Brand::Kingsong; }
  const GattProfile& profile() const override;
  const char* name() const override { return "KingSong"; }
  void start(Link& link, uint32_t nowMs) override;
  bool feed(const uint8_t* data, size_t size, Telemetry& t) override;
  void poll(Link& link, uint32_t nowMs) override;
  bool supports(Command c) const override;
  bool send(Command c, int value, Link& link) override;

  const KingsongAlarms& alarms() const { return alarms_; }

 private:
  static constexpr size_t kFrame = 20;
  static constexpr uint32_t kInfoRetryMs = 2000;
  static constexpr uint8_t kInfoRetries = 3;
  bool feedByte(uint8_t c);
  bool decodeFrame(Telemetry& t);
  static void requestInfo(Link& link);

  uint8_t frame_[kFrame] = {};
  size_t len_ = 0;
  KingsongAlarms alarms_;
  char model_[24] = {};
  float packVolts_ = 67.2f;
  bool haveModel_ = false;
  uint32_t lastRequestMs_ = 0;
  uint8_t retries_ = 0;
};

class KingsongSimulator final : public WheelSimulator {
 public:
  Brand brand() const override { return Brand::Kingsong; }
  void onWrite(const uint8_t* data, size_t size) override;
  void tick(uint32_t nowMs, const Telemetry& t) override;
  bool pop(uint8_t* out, size_t cap, size_t& size) override;

  // The name the fake wheel answers 0x9B with, "<model>-<firmware>" (default "KS-S22-0231").
  void setName(const char* name);
  void setSerial(const char* serial);
  void setAlarms(const KingsongAlarms& a) { alarms_ = a; }
  // Last request type byte (byte 16) written by the app, and how many writes, for tests.
  uint8_t lastType() const { return lastType_; }
  size_t writeCount() const { return writes_; }
  const uint8_t* lastWrite() const { return lastWrite_; }

 private:
  static constexpr size_t kFrame = 20;
  static constexpr size_t kQueue = 8;
  void push(const uint8_t* frame);

  uint8_t queue_[kQueue][kFrame] = {};
  size_t head_ = 0;
  size_t count_ = 0;
  char name_[24] = "KS-S22-0231";
  char serial_[24] = "KSS22A1B2C3D4E5F6";
  KingsongAlarms alarms_;
  uint8_t lastWrite_[kFrame] = {};
  uint8_t lastType_ = 0;
  size_t writes_ = 0;
};

}  // namespace euc
