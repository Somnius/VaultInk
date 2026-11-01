#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// InMotion V2 (V11, V12, V13, V14 and the WheelLog ids for V11y, V9, V12S): Nordic UART, frames
// `AA AA <flag> <len> <cmd> <data...> <xor>` with 0xAA/0xA5 inside escaped by a leading 0xA5, little-endian.
// Sources: REF/InmotionAdapterV2.latest.java (WheelLog, the origin of every other decoder), eucInmotionV2.js,
// WheelDash BLE/frameDecoderIMV2.mc and BLE/BleQueue.mc. The wheel only answers requests, so poll() drives it.
//
// Pairing: none of the open sources has a key exchange. V11/V12 answer an unauthenticated client (WheelDash
// Garmin works that way); whether V13/V14/P6/Adventure firmware refuses one is unknown and needs hardware.
namespace euc::inmotionv2 {

constexpr uint8_t kFlagInfo = 0x11;  // "Initial" in WheelLog
constexpr uint8_t kFlagData = 0x14;  // "Default"
constexpr uint8_t kCmdMainInfo = 0x02;
constexpr uint8_t kCmdLive = 0x04;
constexpr uint8_t kCmdSomething1 = 0x10;
constexpr uint8_t kCmdStats = 0x11;
constexpr uint8_t kCmdSettings = 0x20;
constexpr uint8_t kCmdControl = 0x60;

// Control sub-commands we send (eucInmotionV2.js:28-79, REF:2025-2192). Nothing else is ever encoded.
constexpr uint8_t kCtlVolume = 0x26;
constexpr uint8_t kCtlDrl = 0x2D;
constexpr uint8_t kCtlHeadlight = 0x50;
constexpr uint8_t kCtlHeadlightOldV11 = 0x40;  // V11 main board < 1.4 (REF:2060-2062)
constexpr uint8_t kCtlPlaySound = 0x51;
constexpr uint8_t kCtlPlaySoundOldV11 = 0x41;  // REF:2028-2030
constexpr uint8_t kDefaultSound = 0x18;        // WheelLog wheelBeep() on V11/V12 (REF:278)

// Largest unescaped body (flag..check) we accept; V13/V14 live frames are about 90 bytes.
constexpr size_t kMaxBody = 200;

// Which live-frame layout a model id (series*10 + type, REF:107-144) uses.
enum class Layout : uint8_t {
  Unknown,  // model not known yet: live frames are not decoded
  V11Old,   // V11 main board < 1.4 (REF:1087-1177)
  V11,      // V11 main board 1.4+ (REF:1179-1270)
  V12,      // V12 HS/HT/PRO (REF:1273-1362)
  New       // V13, V14, V11y, V9, V12S (REF:1364-1928): speed at 8, PWM at 14, temps at 58
};

// Builds `AA AA` + escaped(flag, n+1, cmd, data) + xor. Returns the frame size, 0 when it does not fit `cap`.
size_t buildFrame(uint8_t flag, uint8_t cmd, const uint8_t* data, size_t n, uint8_t* out, size_t cap);

// The WheelLog model name for a model id, or "" for an unknown id.
const char* modelName(int modelId);

// Reassembles frames from a byte stream in any chunking: finds `AA AA`, removes the 0xA5 escapes, collects
// len+3 body bytes and checks the XOR (REF:2406-2481). push() returns true when a verified frame is ready.
class Unpacker {
 public:
  bool push(uint8_t raw);
  uint8_t flag() const { return body_[0]; }
  uint8_t cmd() const { return static_cast<uint8_t>(body_[2] & 0x7F); }
  const uint8_t* data() const { return body_ + 3; }
  size_t dataLen() const { return len_ > 0 ? len_ - 1u : 0u; }
  void reset();

 private:
  enum class State : uint8_t { Idle, Flag, Len, Collect };
  State state_ = State::Idle;
  uint8_t body_[kMaxBody] = {};
  size_t count_ = 0;
  uint8_t len_ = 0;
  uint8_t prev_ = 0;  // previous literal (unescaped) byte, 0 after an escaped one
  bool escaped_ = false;
};

// The simulator, exposed so tests can pick the wheel it imitates. Default: V11, main board 1.4.0.
class InmotionV2Sim : public WheelSimulator {
 public:
  Brand brand() const override { return Brand::InmotionV2; }
  void onWrite(const uint8_t* data, size_t size) override;
  void tick(uint32_t nowMs, const Telemetry& t) override;
  bool pop(uint8_t* out, size_t cap, size_t& size) override;

  // series/type as in the type reply (6/1 V11, 7/1 V12 HS, 8/1 V13, 9/2 V14 50S...), main board version.
  void setModel(uint8_t series, uint8_t type, uint8_t fwMain1, uint8_t fwMain2, uint16_t fwMain3);
  // The last control (0x60) payload the app sent, for tests. Returns its length.
  size_t lastControl(uint8_t* out, size_t cap) const;
  bool lightOn() const { return lightOn_; }

 private:
  struct Frame {
    uint8_t bytes[2 * kMaxBody + 3];
    size_t size;
  };
  void handleRequest();
  void reply(uint8_t flag, uint8_t cmd, const uint8_t* data, size_t n);
  Layout layout() const;

  Unpacker in_;
  Frame out_[4] = {};
  size_t outHead_ = 0;
  size_t outCount_ = 0;
  Telemetry t_{};
  uint8_t series_ = 6;
  uint8_t type_ = 1;
  uint8_t fwMain1_ = 1;
  uint8_t fwMain2_ = 4;
  uint16_t fwMain3_ = 0;
  uint8_t control_[8] = {};
  size_t controlLen_ = 0;
  bool lightOn_ = false;
};

}  // namespace euc::inmotionv2
