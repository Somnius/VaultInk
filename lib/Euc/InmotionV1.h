#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// InMotion V1 (V5/V8/V10 family): write FFE5/FFE9, notify FFE0/FFE4. Frames are `AA AA` + body + sum8(body) and
// are terminated by `55 55`; 0xAA, 0x55 and 0xA5 inside the body are escaped with a leading 0xA5. The only open
// source is eucWatch's eucInmotionV10.js (based on freestyl3r's work), so every offset here is cited against it.
// Requests are 16-byte bodies `<cmd> <flags> 55 0F <8 data bytes> 08 05 00 <type>`; replies are long frames the
// decoder indexes directly (eucWatch's frame index minus 3 = body index, since eucWatch keeps the header's escape
// byte).
namespace euc::inmotionv1 {

constexpr uint8_t kCmdAlert = 0x01;
constexpr uint8_t kCmdPinCode = 0x07;  // "init" with the default code 000000 (eucInmotionV10.js:10)
constexpr uint8_t kCmdPlaySound = 0x09;
constexpr uint8_t kCmdVolume = 0x0A;
constexpr uint8_t kCmdLights = 0x0D;
constexpr uint8_t kCmdLive = 0x13;
constexpr uint8_t kCmdInfo = 0x14;
constexpr uint8_t kHornSound = 0x04;  // eucWatch "horn" (eucInmotionV10.js:12)
constexpr uint8_t kBeepSound = 0x15;  // eucWatch "beep" (eucInmotionV10.js:13)

constexpr size_t kRequestBody = 16;
constexpr size_t kMaxBody = 240;
constexpr uint8_t kTerminator[2] = {0x55, 0x55};

// Minimum body lengths the decoders need (eucInmotionV10.js:153-197 and 110-151).
constexpr size_t kLiveBodyMin = 82;
constexpr size_t kInfoBodyMin = 124;

// Builds `AA AA` + escaped(cmd, flags, 55, 0F, data[8], 08, 05, 00, type) + sum8 check. Returns the size.
size_t buildRequest(uint8_t cmd, uint8_t flags, const uint8_t* data8, uint8_t type, uint8_t* out, size_t cap);

// Model name from the two model bytes (eucInmotionV10.js:91-108): "" when unknown.
const char* modelName(uint8_t hi, uint8_t lo);

// Stream reassembler: header `AA AA`, unescape, collect until a raw `55 55`, verify the additive check.
class Unpacker {
 public:
  bool push(uint8_t raw);
  const uint8_t* body() const { return body_; }
  size_t bodyLen() const { return bodyLen_; }
  void reset();

 private:
  enum class State : uint8_t { Idle, Collect };
  State state_ = State::Idle;
  uint8_t body_[kMaxBody + 1] = {};
  size_t count_ = 0;
  size_t bodyLen_ = 0;
  uint8_t prev_ = 0;
  bool escaped_ = false;
  bool pending55_ = false;
};

class InmotionV1Sim : public WheelSimulator {
 public:
  Brand brand() const override { return Brand::InmotionV1; }
  void onWrite(const uint8_t* data, size_t size) override;
  void tick(uint32_t nowMs, const Telemetry& t) override;
  bool pop(uint8_t* out, size_t cap, size_t& size) override;

  void setModel(uint8_t hi, uint8_t lo) {
    modelHi_ = hi;
    modelLo_ = lo;
  }
  // Last request body the app sent that was not a live/info poll (for tests). Returns its length.
  size_t lastRequest(uint8_t* out, size_t cap) const;
  bool lightOn() const { return lightOn_; }
  bool initSeen() const { return initSeen_; }

 private:
  struct Frame {
    uint8_t bytes[2 * kMaxBody + 8];
    size_t size;
  };
  void handleRequest();
  void reply(const uint8_t* body, size_t len);

  Unpacker in_;
  Frame out_[4] = {};
  size_t outHead_ = 0;
  size_t outCount_ = 0;
  Telemetry t_{};
  uint8_t modelHi_ = 14;  // V10 (model id "140")
  uint8_t modelLo_ = 0;
  uint8_t request_[kRequestBody] = {};
  size_t requestLen_ = 0;
  bool lightOn_ = false;
  bool initSeen_ = false;
};

}  // namespace euc::inmotionv1
