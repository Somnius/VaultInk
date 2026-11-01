#include "Kingsong.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "BatteryCurve.h"
#include "Protocols.h"

namespace euc {

namespace {

constexpr GattProfile kKingsongGatt{"0000ffe0-0000-1000-8000-00805f9b34fb",
                                    "0000ffe1-0000-1000-8000-00805f9b34fb",
                                    "0000ffe0-0000-1000-8000-00805f9b34fb",
                                    "0000ffe1-0000-1000-8000-00805f9b34fb",
                                    false,
                                    "KS"};

// Frame types (eucKingsong.js:11-110, frameDecoder.mc:338-466).
constexpr uint8_t kTypeLive = 0xA9;
constexpr uint8_t kTypeTrip = 0xB9;
constexpr uint8_t kTypeName = 0xBB;
constexpr uint8_t kTypeSerial = 0xB3;
constexpr uint8_t kTypeCpu = 0xF5;
constexpr uint8_t kTypeLimits = 0xF6;
constexpr uint8_t kTypeAlarmsA4 = 0xA4;
constexpr uint8_t kTypeAlarmsB5 = 0xB5;
// Requests / commands.
constexpr uint8_t kReqModel = 0x9B;
constexpr uint8_t kReqSerial = 0x63;
constexpr uint8_t kReqAlarms = 0x98;
constexpr uint8_t kCmdHorn = 0x88;
constexpr uint8_t kCmdBeep = 0x7C;
constexpr uint8_t kCmdLights = 0x73;    // b2 = 0x12 + mode (0 on, 1 off, 2 auto), b3 = 1
constexpr uint8_t kCmdStrobe = 0x53;    // b2 = 0/1
constexpr uint8_t kCmdRideLeds = 0x6C;  // b2 = 0/1
constexpr uint8_t kCmdPedal = 0x87;     // b2 = 0 hard, 1 medium, 2 soft; b3 = 0xE0; b17 = kKingsongPedalByte17
constexpr uint8_t kLightBase = 0x12;

// 0xF6 alarm codes (eucKingsong.js:403-423) to AlarmBit.
uint16_t alarmBitsFromCode(const uint16_t code) {
  switch (code) {
    case 202:
      return ALARM_OVER_CURRENT;
    case 203:
      return ALARM_MOTOR_BLOCKED;
    case 217:
      return ALARM_HALL_ERROR;
    case 218:
      return ALARM_HIGH_POWER;
    case 220:
      return ALARM_OVER_VOLTAGE;
    case 232:
      return ALARM_LIFTED;
    default:
      return 0;
  }
}

uint16_t u16le(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
int16_t s16le(const uint8_t* p) { return static_cast<int16_t>(u16le(p)); }
// Word-swapped u32: (b0<<16 | b1<<24 | b2 | b3<<8) (frameDecoder.mc:359-363 decode4bytes, eucKingsong.js:237).
uint32_t u32swapped(const uint8_t* p) { return (static_cast<uint32_t>(u16le(p)) << 16) | u16le(p + 2); }
void putU16le(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void putU32swapped(uint8_t* p, const uint32_t v) {
  putU16le(p, static_cast<uint16_t>(v >> 16));
  putU16le(p + 2, static_cast<uint16_t>(v & 0xFFFF));
}

void fillRequest(uint8_t* f, const uint8_t type) {
  // AA 55 00*14 <type> 14 5A 5A (eucKingsong.js:12, GarminEUCBleDelegate.mc:108-111)
  std::fill(f, f + 20, 0);
  f[0] = 0xAA;
  f[1] = 0x55;
  f[16] = type;
  f[17] = 0x14;
  f[18] = 0x5A;
  f[19] = 0x5A;
}

bool writeRequest(Link& link, const uint8_t type) {
  uint8_t f[20];
  fillRequest(f, type);
  return link.write(f, sizeof(f));
}

void copyString(char* dst, const size_t cap, const char* src) {
  const size_t n = std::min(std::strlen(src), cap - 1);
  std::copy(src, src + n, dst);
  dst[n] = 0;
}

}  // namespace

float kingsongPackVolts(const char* model) {
  // eucData.mc:135-145
  constexpr const char* k84[] = {"KS-18L", "KS-16X", "KS-16XF", "RW", "KS-18LH", "KS-18LY", "KS-S18"};
  constexpr const char* k100[] = {"KS-S19"};
  constexpr const char* k126[] = {"KS-S20", "KS-S22"};
  const auto in = [model](const auto& list) {
    return std::any_of(std::begin(list), std::end(list), [model](const char* m) { return std::strcmp(m, model) == 0; });
  };
  if (in(k84)) {
    return 84.0f;
  }
  if (in(k100)) {
    return 100.8f;
  }
  if (in(k126)) {
    return 126.0f;
  }
  return 67.2f;
}

const GattProfile& KingsongProtocol::profile() const { return kKingsongGatt; }

void KingsongProtocol::requestInfo(Link& link) {
  // GarminEUCBleDelegate.mc:106-136 and eucKingsong.js:500-531: model, serial, alarms.
  writeRequest(link, kReqModel);
  writeRequest(link, kReqSerial);
  writeRequest(link, kReqAlarms);
}

void KingsongProtocol::start(Link& link, const uint32_t nowMs) {
  lastRequestMs_ = nowMs;
  retries_ = 0;
  requestInfo(link);
}

void KingsongProtocol::poll(Link& link, const uint32_t nowMs) {
  // The wheel streams live data by itself; only the name request is repeated when it got lost.
  if (haveModel_ || retries_ >= kInfoRetries || nowMs - lastRequestMs_ < kInfoRetryMs) {
    return;
  }
  lastRequestMs_ = nowMs;
  ++retries_;
  writeRequest(link, kReqModel);
}

bool KingsongProtocol::feedByte(const uint8_t c) {
  if (len_ == 0) {
    if (c == 0xAA) {
      frame_[len_++] = c;
    }
    return false;
  }
  if (len_ == 1) {
    if (c == 0x55) {
      frame_[len_++] = c;
    } else if (c != 0xAA) {
      len_ = 0;
    }
    return false;
  }
  frame_[len_++] = c;
  if (len_ == kFrame) {
    len_ = 0;
    return true;
  }
  return false;
}

bool KingsongProtocol::decodeFrame(Telemetry& t) {
  const uint8_t* f = frame_;
  switch (f[16]) {
    case kTypeLive:
      // frameDecoder.mc:338-376, eucKingsong.js:207-246
      t.voltage = u16le(f + 2) / 100.0f;
      t.speedKmh = u16le(f + 4) / 100.0f;
      t.totalKm = u32swapped(f + 6) / 1000.0f;
      t.current = s16le(f + 10) / 100.0f;
      t.power = t.voltage * t.current;
      t.tempMos = u16le(f + 12) / 100.0f;
      t.batteryPct = batteryPercentFromVoltage(t.voltage, packVolts_);
      t.mark(F_VOLTAGE);
      t.mark(F_SPEED);
      t.mark(F_TOTAL);
      t.mark(F_CURRENT);
      t.mark(F_POWER);
      t.mark(F_TEMP_MOS);
      t.mark(F_BATTERY);
      if (f[15] == 0xE0) {
        t.rideMode = f[14];
        t.mark(F_RIDE_MODE);
      }
      return true;
    case kTypeTrip: {
      // eucKingsong.js:247-259, frameDecoder.mc:377-383
      t.tripKm = u32swapped(f + 2) / 1000.0f;
      t.rideSeconds = u16le(f + 6);
      t.topSpeedKmh = u16le(f + 8) / 100.0f;
      t.charging = f[13] != 0;
      t.tempMotor = u16le(f + 14) / 100.0f;
      t.mark(F_TRIP);
      t.mark(F_RIDE_TIME);
      t.mark(F_TOP_SPEED);
      t.mark(F_CHARGING);
      t.mark(F_TEMP_MOTOR);
      // byte 10 - 0x12: 0 on, 1 off, 2 auto (the same coding as the 0x73 request)
      const int light = f[10] - kLightBase;
      if (light >= 0 && light <= 2) {
        constexpr uint8_t kToTelemetry[] = {1, 0, 2};
        t.lightMode = kToTelemetry[light];
        t.mark(F_LIGHT);
      }
      return true;
    }
    case kTypeCpu:
      // eucKingsong.js:169-178, frameDecoder.mc:431-435: byte 15 = output rate in percent
      t.pwm = f[15];
      t.mark(F_PWM);
      return true;
    case kTypeLimits:
      // eucKingsong.js:260-271: byte 14-15 = alarm code (0 = none)
      t.faultCode = u16le(f + 14);
      t.alarmBits = alarmBitsFromCode(t.faultCode);
      t.mark(F_FAULT);
      t.mark(F_ALARMS);
      return true;
    case kTypeAlarmsA4:
    case kTypeAlarmsB5:
      // eucKingsong.js:346-362, frameDecoder.mc:440-447
      alarms_.alarm1 = f[4] == 0xFF ? 0 : f[4];
      alarms_.alarm2 = f[6];
      alarms_.alarm3 = f[8];
      alarms_.tiltback = f[10];
      alarms_.valid = true;
      return false;
    case kTypeName: {
      // "KS-S22-0231": every part but the last is the model, the last the firmware (frameDecoder.mc:384-407,
      // eucKingsong.js:363-370).
      char nameBuf[15] = {};
      const uint8_t* end = std::find(f + 2, f + 16, 0);
      std::copy(f + 2, end, nameBuf);
      const char* dash = std::strrchr(nameBuf, '-');
      if (dash != nullptr && dash != nameBuf) {
        copyString(t.firmware, sizeof(t.firmware), dash + 1);
        t.mark(F_FIRMWARE);
        const size_t modelLen = std::min(static_cast<size_t>(dash - nameBuf), sizeof(t.model) - 1);
        std::copy(nameBuf, nameBuf + modelLen, t.model);
        t.model[modelLen] = 0;
      } else {
        copyString(t.model, sizeof(t.model), nameBuf);
      }
      t.mark(F_MODEL);
      copyString(model_, sizeof(model_), t.model);
      packVolts_ = kingsongPackVolts(model_);
      haveModel_ = true;
      return true;
    }
    case kTypeSerial: {
      // bytes 2..15 then 17..19 (eucKingsong.js:340, frameDecoder.mc:410-429)
      char* out = std::copy(f + 2, f + 16, t.serial);
      out = std::copy(f + 17, f + 20, out);
      *out = 0;
      t.mark(F_SERIAL);
      return true;
    }
    default:
      return false;  // BMS frames F1/F2, E1/E2, E5/E6 and anything else: ignored
  }
}

bool KingsongProtocol::feed(const uint8_t* data, const size_t size, Telemetry& t) {
  if (data == nullptr) {
    return false;
  }
  bool changed = false;
  for (size_t i = 0; i < size; ++i) {
    if (feedByte(data[i]) && decodeFrame(t)) {
      changed = true;
    }
  }
  return changed;
}

bool KingsongProtocol::supports(const Command c) const {
  switch (c) {
    case Command::Horn:
    case Command::Beep:
    case Command::Lights:
    case Command::RideLeds:
    case Command::PedalMode:
    case Command::RequestInfo:
      return true;
    default:
      return false;
  }
}

bool KingsongProtocol::send(const Command c, const int value, Link& link) {
  uint8_t f[kFrame];
  switch (c) {
    case Command::Horn:
      return writeRequest(link, kCmdHorn);
    case Command::Beep:
      return writeRequest(link, kCmdBeep);
    case Command::Lights:
      if (value < 0 || value > 3) {
        return false;
      }
      if (value == 3) {
        fillRequest(f, kCmdStrobe);  // GarminEUCMenu2Delegate_generic.mc:280-283
        f[2] = 1;
      } else {
        constexpr uint8_t kToWheel[] = {1, 0, 2};  // Telemetry 0 off, 1 on, 2 auto -> wheel 1 off, 0 on, 2 auto
        fillRequest(f, kCmdLights);                // GarminEUCMenu2Delegate_generic.mc:274-279
        f[2] = static_cast<uint8_t>(kLightBase + kToWheel[value]);
        f[3] = 1;
      }
      return link.write(f, kFrame);
    case Command::RideLeds:
      if (value < 0 || value > 1) {
        return false;
      }
      fillRequest(f, kCmdRideLeds);  // GarminEUCMenu2Delegate_generic.mc:285-288
      f[2] = static_cast<uint8_t>(value);
      return link.write(f, kFrame);
    case Command::PedalMode:
      if (value < 0 || value > 2) {
        return false;
      }
      fillRequest(f, kCmdPedal);               // GarminEUCMenu2Delegate_generic.mc:290-295
      f[2] = static_cast<uint8_t>(2 - value);  // Command 0 soft..2 hard -> wheel 2 soft..0 hard
      f[3] = 0xE0;
      f[17] = kKingsongPedalByte17;
      return link.write(f, kFrame);
    case Command::RequestInfo:
      requestInfo(link);
      return true;
    default:
      return false;
  }
}

// ---- simulator ----

void KingsongSimulator::setName(const char* name) { copyString(name_, sizeof(name_), name); }
void KingsongSimulator::setSerial(const char* serial) { copyString(serial_, sizeof(serial_), serial); }

void KingsongSimulator::push(const uint8_t* frame) {
  if (count_ == kQueue) {
    return;
  }
  std::copy(frame, frame + kFrame, queue_[(head_ + count_) % kQueue]);
  ++count_;
}

bool KingsongSimulator::pop(uint8_t* out, const size_t cap, size_t& size) {
  if (count_ == 0 || cap < kFrame) {
    size = 0;
    return false;
  }
  size = kFrame;
  std::copy(queue_[head_], queue_[head_] + kFrame, out);
  head_ = (head_ + 1) % kQueue;
  --count_;
  return true;
}

void KingsongSimulator::onWrite(const uint8_t* data, const size_t size) {
  if (size != kFrame || data[0] != 0xAA || data[1] != 0x55) {
    return;
  }
  std::copy(data, data + kFrame, lastWrite_);
  lastType_ = data[16];
  ++writes_;
  uint8_t f[kFrame];
  switch (data[16]) {
    case kReqModel: {
      fillRequest(f, kTypeName);
      const size_t n = std::min(std::strlen(name_), static_cast<size_t>(14));
      std::copy(name_, name_ + n, f + 2);
      push(f);
      break;
    }
    case kReqSerial: {
      fillRequest(f, kTypeSerial);
      char padded[18] = {};
      copyString(padded, sizeof(padded), serial_);
      std::copy(padded, padded + 14, f + 2);
      std::copy(padded + 14, padded + 17, f + 17);
      push(f);
      break;
    }
    case kReqAlarms:
      fillRequest(f, kTypeAlarmsB5);
      f[4] = alarms_.alarm1;
      f[6] = alarms_.alarm2;
      f[8] = alarms_.alarm3;
      f[10] = alarms_.tiltback;
      push(f);
      break;
    default:
      break;
  }
}

void KingsongSimulator::tick(const uint32_t nowMs, const Telemetry& t) {
  (void)nowMs;
  uint8_t f[kFrame];
  fillRequest(f, kTypeLive);
  putU16le(f + 2, static_cast<uint16_t>(std::lround(t.voltage * 100.0f)));
  putU16le(f + 4, static_cast<uint16_t>(std::lround(t.speedKmh * 100.0f)));
  putU32swapped(f + 6, static_cast<uint32_t>(std::lround(t.totalKm * 1000.0f)));
  putU16le(f + 10, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t.current * 100.0f))));
  putU16le(f + 12, static_cast<uint16_t>(std::lround(t.tempMos * 100.0f)));
  f[14] = t.rideMode;
  f[15] = 0xE0;
  push(f);

  fillRequest(f, kTypeTrip);
  putU32swapped(f + 2, static_cast<uint32_t>(std::lround(t.tripKm * 1000.0f)));
  putU16le(f + 6, static_cast<uint16_t>(t.rideSeconds));
  putU16le(f + 8, static_cast<uint16_t>(std::lround(t.topSpeedKmh * 100.0f)));
  constexpr uint8_t kToWheel[] = {1, 0, 2};
  f[10] = static_cast<uint8_t>(kLightBase + kToWheel[t.lightMode <= 2 ? t.lightMode : 0]);
  f[13] = t.charging ? 1 : 0;
  putU16le(f + 14, static_cast<uint16_t>(std::lround(t.tempMotor * 100.0f)));
  push(f);

  fillRequest(f, kTypeCpu);
  f[15] = static_cast<uint8_t>(std::lround(std::clamp(t.pwm, 0.0f, 255.0f)));
  push(f);

  fillRequest(f, kTypeLimits);
  putU16le(f + 14, t.faultCode);
  push(f);
}

std::unique_ptr<Protocol> makeKingsong() { return std::make_unique<KingsongProtocol>(); }
std::unique_ptr<WheelSimulator> makeKingsongSim() { return std::make_unique<KingsongSimulator>(); }

}  // namespace euc
