#include "InmotionV1.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "CrcUtil.h"
#include "Protocols.h"

namespace euc::inmotionv1 {

namespace {

// eucInmotionV10.js:336-345. 16-bit UUIDs written in the 128-bit base form.
constexpr char kNotifyService[] = "0000ffe0-0000-1000-8000-00805f9b34fb";
constexpr char kNotifyChar[] = "0000ffe4-0000-1000-8000-00805f9b34fb";
constexpr char kWriteService[] = "0000ffe5-0000-1000-8000-00805f9b34fb";
constexpr char kWriteChar[] = "0000ffe9-0000-1000-8000-00805f9b34fb";
constexpr GattProfile kProfile{kNotifyService, kNotifyChar, kWriteService, kWriteChar, false, ""};

// eucWatch asks for live data every 0.5 s and for the info frame when none came for 1 s (eucInmotionV10.js:305-321).
constexpr uint32_t kLiveIntervalMs = 500;
constexpr uint32_t kInfoIntervalMs = 1000;

// eucWatch's "start" writes the "init" frame, a pin-code command with the default code "000000"
// (eucInmotionV10.js:10, 428). UNVERIFIED whether a wheel with a changed code still answers; flip to false to
// skip it. No other pin-code write exists here.
constexpr bool kSendInitWithDefaultPin = true;
// Whether the check byte itself is escaped on the wire. eucWatch's request constants carry it raw and its
// receive path strips escapes everywhere, so both work there. UNVERIFIED; requests send it raw.
constexpr bool kEscapeCheckByte = false;

// Speed: eucWatch's V10 module divides the two summed s32 by 2000 (eucInmotionV10.js:159), its alert parser
// uses 3812 counts per m/s (:223). UNVERIFIED which one every V1 model uses; the live parser's value is used.
constexpr float kSpeedDivisor = 2000.0f;
// Temperatures: eucWatch reads body 48 as the motor ("tmpM") and body 50 as the main temperature ("tmp",
// eucInmotionV10.js:174-176, frame 51/53). UNVERIFIED which sensor each is.
constexpr size_t kTempMotor = 48;
constexpr size_t kTempMos = 50;

// Body offsets (eucWatch frame index - 3).
constexpr size_t kLiveSpeedA = 28;
constexpr size_t kLiveSpeedB = 32;
constexpr size_t kLiveCurrent = 36;
constexpr size_t kLiveVoltage = 40;
constexpr size_t kLiveTotal = 60;
constexpr size_t kLiveMode = 78;
constexpr size_t kInfoSerial = 16;  // 8 bytes, printed as hex from byte 23 down to 16
constexpr size_t kInfoFwMinor2 = 40;
constexpr size_t kInfoFwMinor1 = 42;
constexpr size_t kInfoFwMajor = 43;
constexpr size_t kInfoLight = 96;
constexpr size_t kInfoModelLo = 120;
constexpr size_t kInfoModelHi = 123;
constexpr size_t kInfoVolume = 141;

uint16_t u16(const uint8_t* d, const size_t i) { return static_cast<uint16_t>(d[i] | (d[i + 1] << 8)); }
int16_t s16(const uint8_t* d, const size_t i) { return static_cast<int16_t>(u16(d, i)); }
uint32_t u32(const uint8_t* d, const size_t i) {
  return static_cast<uint32_t>(d[i]) | (static_cast<uint32_t>(d[i + 1]) << 8) |
         (static_cast<uint32_t>(d[i + 2]) << 16) | (static_cast<uint32_t>(d[i + 3]) << 24);
}
int32_t s32(const uint8_t* d, const size_t i) { return static_cast<int32_t>(u32(d, i)); }
void putU16(uint8_t* d, const size_t i, const uint16_t v) {
  d[i] = static_cast<uint8_t>(v & 0xFF);
  d[i + 1] = static_cast<uint8_t>(v >> 8);
}
void putU32(uint8_t* d, const size_t i, const uint32_t v) {
  putU16(d, i, static_cast<uint16_t>(v & 0xFFFF));
  putU16(d, i + 2, static_cast<uint16_t>(v >> 16));
}
bool needsEscape(const uint8_t b) { return b == 0xAA || b == 0x55 || b == 0xA5; }

// `AA AA` + escaped body (+ check, escaped or raw) into out; 0 when it does not fit.
size_t frameFromBody(const uint8_t* body, const size_t len, const bool escapeCheck, uint8_t* out, const size_t cap) {
  const uint8_t check = crc::sum8(body, len);
  const size_t escapes = static_cast<size_t>(std::count_if(body, body + len, needsEscape));
  const size_t size = 2 + len + escapes + 1 + ((escapeCheck && needsEscape(check)) ? 1 : 0);
  if (size > cap) {
    return 0;
  }
  size_t o = 0;
  out[o++] = 0xAA;
  out[o++] = 0xAA;
  for (size_t i = 0; i < len; ++i) {
    if (needsEscape(body[i])) {
      out[o++] = 0xA5;
    }
    out[o++] = body[i];
  }
  if (escapeCheck && needsEscape(check)) {
    out[o++] = 0xA5;
  }
  out[o++] = check;
  return o;
}

// eucInmotionV10.js:153-197.
bool decodeLive(const uint8_t* b, const size_t n, Telemetry& t) {
  if (n < kLiveBodyMin) {
    return false;
  }
  t.speedKmh =
      std::fabs((static_cast<float>(s32(b, kLiveSpeedA)) + static_cast<float>(s32(b, kLiveSpeedB))) / kSpeedDivisor);
  t.voltage = u32(b, kLiveVoltage) / 100.0f;
  t.current = s16(b, kLiveCurrent) / 100.0f;
  t.tempMotor = static_cast<float>(static_cast<int8_t>(b[kTempMotor]));
  t.tempMos = static_cast<float>(static_cast<int8_t>(b[kTempMos]));
  t.totalKm = u32(b, kLiveTotal) / 1000.0f;
  t.rideMode = static_cast<uint8_t>(s32(b, kLiveMode) & 0xFF);
  t.mark(F_SPEED);
  t.mark(F_VOLTAGE);
  t.mark(F_CURRENT);
  t.mark(F_TEMP_MOTOR);
  t.mark(F_TEMP_MOS);
  t.mark(F_TOTAL);
  t.mark(F_RIDE_MODE);
  return true;
}

// eucInmotionV10.js:110-151.
bool decodeInfo(const uint8_t* b, const size_t n, Telemetry& t) {
  if (n < kInfoBodyMin) {
    return false;
  }
  t.lightMode = b[kInfoLight] != 0 ? 1 : 0;
  t.mark(F_LIGHT);
  std::snprintf(t.firmware, sizeof t.firmware, "%d.%d.%d", static_cast<int>(b[kInfoFwMajor]),
                static_cast<int>(b[kInfoFwMinor1]), static_cast<int>(u16(b, kInfoFwMinor2)));
  t.mark(F_FIRMWARE);
  size_t o = 0;
  for (size_t i = kInfoSerial + 8; i > kInfoSerial; --i) {
    o += static_cast<size_t>(std::snprintf(t.serial + o, sizeof t.serial - o, "%02X", static_cast<int>(b[i - 1])));
  }
  t.mark(F_SERIAL);
  const char* known = modelName(b[kInfoModelHi], b[kInfoModelLo]);
  if (known[0] != '\0') {
    std::snprintf(t.model, sizeof t.model, "InMotion %s", known);
  } else {
    std::snprintf(t.model, sizeof t.model, "InMotion %d%d", static_cast<int>(b[kInfoModelHi]),
                  static_cast<int>(b[kInfoModelLo]));
  }
  t.mark(F_MODEL);
  return true;
}

class InmotionV1Protocol : public Protocol {
 public:
  Brand brand() const override { return Brand::InmotionV1; }
  const GattProfile& profile() const override { return kProfile; }
  const char* name() const override { return "InMotion V1"; }

  void start(Link& link, const uint32_t nowMs) override {
    in_.reset();
    lastLiveMs_ = nowMs;
    lastInfoReplyMs_ = 0;
    forceInfo_ = true;
    if (kSendInitWithDefaultPin) {
      const uint8_t pin[8] = {0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00};  // "000000"
      request(link, kCmdPinCode, 0x03, pin, 0x00);
    }
  }

  bool feed(const uint8_t* data, const size_t size, Telemetry& t) override {
    bool changed = false;
    for (size_t i = 0; i < size; ++i) {
      if (in_.push(data[i])) {
        changed = handleFrame(t) || changed;
      }
    }
    return changed;
  }

  void poll(Link& link, const uint32_t nowMs) override {
    if (nowMs - lastLiveMs_ < kLiveIntervalMs) {
      return;
    }
    lastLiveMs_ = nowMs;
    nowMs_ = nowMs;
    static constexpr uint8_t kAllFF[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    request(link, kCmdLive, 0x01, kAllFF, 0x00);  // eucInmotionV10.js:9
    if (forceInfo_ || nowMs - lastInfoReplyMs_ >= kInfoIntervalMs) {
      forceInfo_ = false;
      request(link, kCmdInfo, 0x01, kAllFF, 0x01);  // eucInmotionV10.js:7
    }
  }

  bool supports(const Command c) const override {
    switch (c) {
      case Command::Horn:
      case Command::Beep:
      case Command::Lights:
      case Command::Volume:
      case Command::RequestInfo:
        return true;
      default:
        return false;
    }
  }

  bool send(const Command c, const int value, Link& link) override {
    uint8_t d[8] = {};
    switch (c) {
      case Command::Horn:
        // playSound 0..23 (eucInmotionV10.js:17-19); 0 = eucWatch's horn sound.
        if (value < 0 || value > 23) {
          return false;
        }
        d[0] = value == 0 ? kHornSound : static_cast<uint8_t>(value);
        return request(link, kCmdPlaySound, 0x06, d, 0x00);
      case Command::Beep:
        d[0] = kBeepSound;
        return request(link, kCmdPlaySound, 0x06, d, 0x00);
      case Command::Lights:
        if (value != 0 && value != 1) {  // eucInmotionV10.js:24-27
          return false;
        }
        d[0] = static_cast<uint8_t>(value);
        return request(link, kCmdLights, 0x01, d, 0x00);
      case Command::Volume: {
        if (value < 0 || value > 100) {  // eucInmotionV10.js:20-23: u16 LE of value * 100
          return false;
        }
        putU16(d, 0, static_cast<uint16_t>(value * 100));
        return request(link, kCmdVolume, 0x06, d, 0x00);
      }
      case Command::RequestInfo:
        forceInfo_ = true;
        return true;
      default:
        return false;
    }
  }

 private:
  // One request = the frame (chunked at 20 bytes) followed by the `55 55` terminator as its own write
  // (eucInmotionV10.js:309-316, 452-453).
  static bool request(Link& link, const uint8_t cmd, const uint8_t flags, const uint8_t* data8, const uint8_t type) {
    uint8_t frame[2 + 2 * kRequestBody + 2];
    const size_t size = buildRequest(cmd, flags, data8, type, frame, sizeof frame);
    if (size == 0) {
      return false;
    }
    for (size_t o = 0; o < size; o += 20) {
      if (!link.write(frame + o, std::min<size_t>(20, size - o))) {
        return false;
      }
    }
    return link.write(kTerminator, sizeof kTerminator);
  }

  bool handleFrame(Telemetry& t) {
    const uint8_t* b = in_.body();
    const size_t n = in_.bodyLen();
    if (n < 4) {
      return false;
    }
    switch (b[0]) {
      case kCmdLive:
        return decodeLive(b, n, t);
      case kCmdInfo:
        if (decodeInfo(b, n, t)) {
          lastInfoReplyMs_ = nowMs_ != 0 ? nowMs_ : 1;
          return true;
        }
        return false;
      default:
        return false;  // alerts (cmd 0x01) are not decoded: eucWatch's offsets for them are not consistent
    }
  }

  Unpacker in_;
  uint32_t lastLiveMs_ = 0;
  uint32_t lastInfoReplyMs_ = 0;
  uint32_t nowMs_ = 0;
  bool forceInfo_ = true;
};

}  // namespace

size_t buildRequest(const uint8_t cmd, const uint8_t flags, const uint8_t* data8, const uint8_t type, uint8_t* out,
                    const size_t cap) {
  uint8_t body[kRequestBody] = {cmd, flags, 0x55, 0x0F};
  std::copy_n(data8, 8, body + 4);
  body[12] = 0x08;
  body[13] = 0x05;
  body[14] = 0x00;
  body[15] = type;
  return frameFromBody(body, sizeof body, kEscapeCheckByte, out, cap);
}

const char* modelName(const uint8_t hi, const uint8_t lo) {
  // eucInmotionV10.js:91-108 concatenates the decimal strings of the two bytes.
  const int id = hi * 10 + lo;
  if (lo > 9) {
    return "";
  }
  switch (id) {
    case 50:
      return "V5";
    case 51:
      return "V5PLUS";
    case 52:
      return "V5F";
    case 53:
      return "V5D";
    case 80:
      return "V8";
    case 86:
      return "V8F";
    case 87:
      return "V8S";
    case 100:
      return "V10S";
    case 101:
      return "V10SF";
    case 140:
      return "V10";
    case 141:
      return "V10F";
    case 142:
      return "V10T";
    case 143:
      return "V10FT";
    default:
      return "";
  }
}

void Unpacker::reset() {
  state_ = State::Idle;
  count_ = 0;
  bodyLen_ = 0;
  prev_ = 0;
  escaped_ = false;
  pending55_ = false;
}

bool Unpacker::push(const uint8_t raw) {
  if (state_ == State::Idle) {
    if (raw == 0xAA && prev_ == 0xAA) {
      state_ = State::Collect;
      count_ = 0;
      prev_ = 0;
      escaped_ = false;
      pending55_ = false;
      return false;
    }
    prev_ = raw;
    return false;
  }
  if (!escaped_ && raw == 0xA5) {
    escaped_ = true;
    return false;
  }
  if (!escaped_ && raw == 0x55) {
    if (pending55_) {
      // Terminator: the body is everything collected but the check byte.
      state_ = State::Idle;
      prev_ = 0;
      pending55_ = false;
      if (count_ < 2) {
        return false;
      }
      bodyLen_ = count_ - 1;
      return crc::sum8(body_, bodyLen_) == body_[bodyLen_];
    }
    pending55_ = true;
    return false;
  }
  if (!escaped_ && raw == 0xAA && prev_ == 0xAA) {
    // A raw AA AA inside a frame can only be the next header (0xAA data is escaped): resync.
    count_ = 0;
    prev_ = 0;
    pending55_ = false;
    return false;
  }
  if (pending55_) {
    // A lone raw 0x55 was the (unescaped) check byte, keep it.
    pending55_ = false;
    if (count_ < sizeof body_) {
      body_[count_++] = 0x55;
    }
  }
  prev_ = escaped_ ? 0 : raw;
  escaped_ = false;
  if (count_ >= sizeof body_) {
    reset();
    return false;
  }
  body_[count_++] = raw;
  return false;
}

// ---- simulator -------------------------------------------------------------------------------------------------

void InmotionV1Sim::tick(const uint32_t, const Telemetry& t) { t_ = t; }

size_t InmotionV1Sim::lastRequest(uint8_t* out, const size_t cap) const {
  const size_t n = std::min(cap, requestLen_);
  std::copy_n(request_, n, out);
  return n;
}

bool InmotionV1Sim::pop(uint8_t* out, const size_t cap, size_t& size) {
  if (outCount_ == 0) {
    return false;
  }
  const Frame& f = out_[outHead_];
  if (f.size > cap) {
    return false;
  }
  std::copy_n(f.bytes, f.size, out);
  size = f.size;
  outHead_ = (outHead_ + 1) % 4;
  --outCount_;
  return true;
}

void InmotionV1Sim::reply(const uint8_t* body, const size_t len) {
  if (outCount_ >= 4) {
    return;
  }
  Frame& f = out_[(outHead_ + outCount_) % 4];
  // The simulated wheel escapes its check byte too, so a 0x55 check never looks like the terminator.
  f.size = frameFromBody(body, len, true, f.bytes, sizeof f.bytes - 2);
  if (f.size == 0) {
    return;
  }
  f.bytes[f.size++] = 0x55;
  f.bytes[f.size++] = 0x55;
  ++outCount_;
}

void InmotionV1Sim::onWrite(const uint8_t* data, const size_t size) {
  for (size_t i = 0; i < size; ++i) {
    if (in_.push(data[i])) {
      handleRequest();
    }
  }
}

void InmotionV1Sim::handleRequest() {
  const uint8_t* b = in_.body();
  const size_t n = in_.bodyLen();
  if (n < kRequestBody) {
    return;
  }
  uint8_t body[kMaxBody] = {};
  body[1] = 0x01;
  body[2] = 0x55;
  body[3] = 0x0F;
  body[12] = 0xFE;  // extended frame marker, data[0..3] = extra length
  body[13] = 0x05;
  switch (b[0]) {
    case kCmdLive: {
      constexpr size_t len = kLiveBodyMin + 2;
      body[0] = kCmdLive;
      putU32(body, 4, static_cast<uint32_t>(len - 16));
      const int32_t half = static_cast<int32_t>(std::lround(t_.speedKmh * kSpeedDivisor / 2));
      putU32(body, kLiveSpeedA, static_cast<uint32_t>(half));
      putU32(body, kLiveSpeedB, static_cast<uint32_t>(half));
      putU16(body, kLiveCurrent, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t_.current * 100))));
      putU32(body, kLiveVoltage, static_cast<uint32_t>(std::lround(t_.voltage * 100)));
      body[kTempMotor] = static_cast<uint8_t>(static_cast<int8_t>(std::lround(t_.tempMotor)));
      body[kTempMos] = static_cast<uint8_t>(static_cast<int8_t>(std::lround(t_.tempMos)));
      putU32(body, kLiveTotal, static_cast<uint32_t>(std::lround(t_.totalKm * 1000)));
      putU32(body, kLiveMode, t_.rideMode);
      reply(body, len);
      return;
    }
    case kCmdInfo: {
      constexpr size_t len = 150;
      body[0] = kCmdInfo;
      putU32(body, 4, static_cast<uint32_t>(len - 16));
      const uint8_t serial[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
      std::copy_n(serial, 8, body + kInfoSerial);
      putU16(body, kInfoFwMinor2, 123);
      body[kInfoFwMinor1] = 2;
      body[kInfoFwMajor] = 1;
      body[kInfoLight] = lightOn_ ? 1 : 0;
      body[kInfoModelLo] = modelLo_;
      body[kInfoModelHi] = modelHi_;
      putU16(body, kInfoVolume, 5000);
      reply(body, len);
      return;
    }
    case kCmdPinCode:
      initSeen_ = true;
      break;
    case kCmdLights:
      lightOn_ = b[4] != 0;
      break;
    default:
      break;
  }
  requestLen_ = std::min(n, sizeof request_);
  std::copy_n(b, requestLen_, request_);
}

}  // namespace euc::inmotionv1

namespace euc {

std::unique_ptr<Protocol> makeInmotionV1() { return std::make_unique<inmotionv1::InmotionV1Protocol>(); }
std::unique_ptr<WheelSimulator> makeInmotionV1Sim() { return std::make_unique<inmotionv1::InmotionV1Sim>(); }

}  // namespace euc
