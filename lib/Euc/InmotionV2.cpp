#include "InmotionV2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "CrcUtil.h"
#include "Protocols.h"

namespace euc::inmotionv2 {

namespace {

constexpr char kNus[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kNusWrite[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char kNusNotify[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
// eucInmotionV2.js:484-491, WD/BLE/eucPM.mc:91-97. Scan name from the Amazfit docs (SITE:740); WheelDash Garmin
// only accepts V11/V12 names (GarminEUCBleDelegate.mc:298-309).
constexpr GattProfile kProfile{kNus, kNusNotify, kNus, kNusWrite, false, "V1"};

// WheelDash polls live every 200 ms and stats every 48th tick (WD/BLE/BleQueue.mc:41-73); WheelLog retries the
// info requests every 10 x 25 ms (REF:204-271).
constexpr uint32_t kLiveIntervalMs = 200;
constexpr uint32_t kInfoRetryMs = 250;
constexpr uint32_t kStatsEveryNTicks = 48;

// V11 main board version that switches to the 1.4+ live layout. WheelLog: main1 < 2 && main2 < 4 is old
// (REF:613); eucWatch: main1 < 1 && main2 < 4 (eucInmotionV2.js:135), which can never match a 1.x board.
// UNVERIFIED on hardware; WheelLog's rule is used.
constexpr uint8_t kOldLayoutMain1Below = 2;
constexpr uint8_t kOldLayoutMain2Below = 4;

// V13/V14 PWM: WheelLog passes the s16 at 14 to the same setOutput() as the V11/V12 s16 at 8, which the apps
// show as hundredths of a percent; kvazimoda24's eucWatch stores the raw value (eucwatch-compare NOTES.md bug C).
// UNVERIFIED on a V13/V14 packet log; /100 like V11/V12 is used.
constexpr float kNewLayoutPwmDivisor = 100.0f;
// V13/V14 temperatures: WheelLog reads MOS at 58, motor at 59, battery at 60, board at 61 (REF:1399-1402);
// kvazimoda24 labels 59 as battery (NOTES.md bug D). UNVERIFIED; WheelLog's offsets are used.
constexpr size_t kNewLayoutTempMos = 58;
constexpr size_t kNewLayoutTempMotor = 59;
constexpr size_t kNewLayoutTempBattery = 60;
constexpr size_t kNewLayoutTempBoard = 61;

uint16_t u16(const uint8_t* d, const size_t i) { return static_cast<uint16_t>(d[i] | (d[i + 1] << 8)); }
int16_t s16(const uint8_t* d, const size_t i) { return static_cast<int16_t>(u16(d, i)); }
uint32_t u32(const uint8_t* d, const size_t i) {
  return static_cast<uint32_t>(d[i]) | (static_cast<uint32_t>(d[i + 1]) << 8) |
         (static_cast<uint32_t>(d[i + 2]) << 16) | (static_cast<uint32_t>(d[i + 3]) << 24);
}
void putU16(uint8_t* d, const size_t i, const uint16_t v) {
  d[i] = static_cast<uint8_t>(v & 0xFF);
  d[i + 1] = static_cast<uint8_t>(v >> 8);
}
void putS16(uint8_t* d, const size_t i, const float v) {
  putU16(d, i, static_cast<uint16_t>(static_cast<int16_t>(std::lround(v))));
}
void putU32(uint8_t* d, const size_t i, const uint32_t v) {
  putU16(d, i, static_cast<uint16_t>(v & 0xFFFF));
  putU16(d, i + 2, static_cast<uint16_t>(v >> 16));
}

// Temperatures are one byte, value + 80 - 256 degrees C (REF:1100). A raw 0 (-176 C) means "no sensor"
// (WheelLog notes the battery and lamp bytes read 0), so it is not reported.
bool temp(const uint8_t raw, float& out) {
  if (raw == 0) {
    return false;
  }
  out = static_cast<float>(static_cast<int>(raw) + 80 - 256);
  return true;
}
uint8_t tempRaw(const Telemetry& t, const Field f, const float value) {
  return t.has(f) ? static_cast<uint8_t>(std::lround(value) + 176) : 0u;
}

Layout layoutFor(const int modelId, const int series, const int protoVer) {
  switch (modelId) {
    case 61:
      return protoVer == 1 ? Layout::V11Old : Layout::V11;
    case 62:
    case 81:
    case 82:
    case 91:
    case 92:
    case 111:
    case 121:
      return Layout::New;
    case 71:
    case 72:
    case 73:
      return Layout::V12;
    default:
      break;
  }
  // Unknown type byte: eucWatch keys on the series alone (eucInmotionV2.js:92-99).
  switch (series) {
    case 6:
      return protoVer == 1 ? Layout::V11Old : Layout::V11;
    case 7:
      return Layout::V12;
    case 8:
    case 9:
      return Layout::New;
    default:
      return Layout::Unknown;
  }
}

// The wheel's error bits (REF:1036-1085), mapped to AlarmBit. `i` is the first error byte.
uint16_t errorBits(const uint8_t* d, const size_t i) {
  uint16_t bits = 0;
  if ((d[i] >> 2) & 0x01) {
    bits |= ALARM_HALL_ERROR;
  }
  if (d[i + 3] & 0x01) {
    bits |= ALARM_LOW_VOLTAGE;
  }
  if ((d[i + 3] >> 1) & 0x01) {
    bits |= ALARM_OVER_VOLTAGE;
  }
  if ((d[i + 3] >> 2) & 0x03) {
    bits |= ALARM_OVER_CURRENT;
  }
  if ((d[i + 3] >> 4) & 0x03) {
    bits |= ALARM_LOW_VOLTAGE;
  }
  if ((d[i + 3] >> 6) & 0x03) {
    bits |= ALARM_HIGH_TEMP;  // MOS or motor over temperature
  }
  if (d[i + 4] & 0x03) {
    bits |= ALARM_HIGH_TEMP;  // battery or board over temperature
  }
  if ((d[i + 4] >> 2) & 0x01) {
    bits |= ALARM_SPEED3;  // err_overSpeedState
  }
  if ((d[i + 4] >> 3) & 0x01) {
    bits |= ALARM_HIGH_POWER;  // err_outputSaturationState
  }
  if ((d[i + 4] >> 5) & 0x01) {
    bits |= ALARM_MOTOR_BLOCKED;
  }
  return bits;
}

// State byte: bit 7 charging, bit 6 motor active (REF:1236-1239); next byte bit 2 lifted.
void decodeState(const uint8_t stateByte, const uint8_t flagsByte, const bool lightBit, Telemetry& t) {
  t.charging = ((stateByte >> 7) & 0x01) != 0;
  t.mark(F_CHARGING);
  t.lightMode = lightBit ? 1 : 0;
  t.mark(F_LIGHT);
  if ((flagsByte >> 2) & 0x01) {
    t.alarmBits |= ALARM_LIFTED;
  }
}

void decodeCommonHead(const uint8_t* d, Telemetry& t) {
  t.voltage = u16(d, 0) / 100.0f;
  t.current = s16(d, 2) / 100.0f;
  t.mark(F_VOLTAGE);
  t.mark(F_CURRENT);
}

void decodeTemps(const uint8_t* d, const size_t mos, const size_t motor, const size_t battery, const size_t board,
                 Telemetry& t) {
  if (temp(d[mos], t.tempMos)) {
    t.mark(F_TEMP_MOS);
  }
  if (temp(d[motor], t.tempMotor)) {
    t.mark(F_TEMP_MOTOR);
  }
  if (temp(d[battery], t.tempBattery)) {
    t.mark(F_TEMP_BATTERY);
  }
  if (temp(d[board], t.tempBoard)) {
    t.mark(F_TEMP_BOARD);
  }
}

// REF:1087-1177. The state byte moves with the frame length and PWM only exists in the long variant.
bool decodeLiveV11Old(const uint8_t* d, const size_t n, Telemetry& t) {
  if (n < 22) {
    return false;
  }
  t.alarmBits = 0;
  decodeCommonHead(d, t);
  t.speedKmh = std::fabs(s16(d, 4) / 100.0f);
  t.power = static_cast<float>(s16(d, 8));
  t.tripKm = u16(d, 12) * 10 / 1000.0f;
  t.rangeKm = u16(d, 14) * 10 / 1000.0f;
  t.batteryPct = static_cast<float>(d[16] & 0x7F);
  t.mark(F_SPEED);
  t.mark(F_POWER);
  t.mark(F_TRIP);
  t.mark(F_RANGE);
  t.mark(F_BATTERY);
  decodeTemps(d, 17, 18, 19, 20, t);
  if (n >= 28) {
    t.pitch = s16(d, 22) / 100.0f;
    t.roll = s16(d, 26) / 100.0f;
    t.mark(F_PITCH);
    t.mark(F_ROLL);
  }
  const size_t i = (n < 49) ? 36 : 38;
  if (n >= 49) {
    t.pwm = std::fabs(s16(d, 36) / 100.0f);
    t.mark(F_PWM);
  }
  if (n >= i + 2) {
    decodeState(d[i], d[i + 1], (d[i + 1] & 0x01) != 0, t);
  }
  if (n >= i + 5 + 7) {
    t.alarmBits |= errorBits(d, i + 5);
  }
  t.mark(F_ALARMS);
  return true;
}

// REF:1179-1270 (V11 1.4+) and REF:1273-1362 (V12): same fields. V11 has two extra words at 22/24 (mileage,
// battery, range, limits sit 4 bytes later) and one extra word before the temperatures (temperatures, state and
// error bytes sit 2 bytes later: 42/56/61 against 40/54/59).
bool decodeLiveV11V12(const uint8_t* d, const size_t n, const bool v12, Telemetry& t) {
  const size_t shift = v12 ? 0 : 4;
  const size_t tail = v12 ? 0 : 2;
  if (n < 34 + shift) {
    return false;
  }
  t.alarmBits = 0;
  decodeCommonHead(d, t);
  t.speedKmh = std::fabs(s16(d, 4) / 100.0f);
  t.pwm = std::fabs(s16(d, 8) / 100.0f);
  t.power = static_cast<float>(s16(d, 10));
  t.pitch = s16(d, 16) / 100.0f;
  t.roll = s16(d, 20) / 100.0f;
  t.tripKm = u16(d, 22 + shift) * 10 / 1000.0f;
  t.batteryPct = u16(d, 24 + shift) / 100.0f;
  t.rangeKm = u16(d, 26 + shift) * 10 / 1000.0f;
  t.mark(F_SPEED);
  t.mark(F_PWM);
  t.mark(F_POWER);
  t.mark(F_PITCH);
  t.mark(F_ROLL);
  t.mark(F_TRIP);
  t.mark(F_BATTERY);
  t.mark(F_RANGE);
  if (n >= 47 + tail) {
    decodeTemps(d, 40 + tail, 41 + tail, 42 + tail, 43 + tail, t);
  }
  if (n >= 56 + tail) {
    const uint8_t lights = d[55 + tail];
    decodeState(d[54 + tail], lights, (lights & 0x03) != 0, t);  // low or high beam
  }
  if (n >= 59 + tail + 7) {
    t.alarmBits |= errorBits(d, 59 + tail);
  }
  t.mark(F_ALARMS);
  return true;
}

// REF:1364-1583 (V13, V14) and the V11y/V9/V12S parsers, which share these offsets. Fields WheelLog marks
// "not sure" (torque, pitch, roll, mileage) are left out.
bool decodeLiveNew(const uint8_t* d, const size_t n, const bool v13, Telemetry& t) {
  if (n < 18) {
    return false;
  }
  t.alarmBits = 0;
  decodeCommonHead(d, t);
  t.speedKmh = std::fabs(s16(d, 8) / 100.0f);
  t.pwm = std::fabs(static_cast<float>(s16(d, 14)) / kNewLayoutPwmDivisor);
  t.power = static_cast<float>(s16(d, 16));
  t.mark(F_SPEED);
  t.mark(F_PWM);
  t.mark(F_POWER);
  if (n >= 38) {
    // Two battery packs, averaged (REF:1421).
    t.batteryPct = (u16(d, 34) + u16(d, 36)) / 200.0f;
    t.mark(F_BATTERY);
  }
  if (n >= kNewLayoutTempBoard + 1) {
    decodeTemps(d, kNewLayoutTempMos, kNewLayoutTempMotor, kNewLayoutTempBattery, kNewLayoutTempBoard, t);
  }
  if (n >= 77) {
    decodeState(d[74], d[75], ((d[76] >> 1) & 0x01) != 0, t);
  }
  const size_t err = v13 ? 76 : 77;
  if (n >= err + 7) {
    t.alarmBits |= errorBits(d, err);
  }
  t.mark(F_ALARMS);
  return true;
}

// REF:1012-1034, eucInmotionV2.js:320-341: total in 10 m units, ride time in seconds.
bool decodeStats(const uint8_t* d, const size_t n, Telemetry& t) {
  if (n < 20) {
    return false;
  }
  t.totalKm = u32(d, 0) / 100.0f;
  t.rideSeconds = u32(d, 12);
  t.mark(F_TOTAL);
  t.mark(F_RIDE_TIME);
  return true;
}

class InmotionV2Protocol : public Protocol {
 public:
  Brand brand() const override { return Brand::InmotionV2; }
  const GattProfile& profile() const override { return kProfile; }
  const char* name() const override { return "InMotion V2"; }

  void start(Link& link, const uint32_t nowMs) override {
    in_.reset();
    step_ = Step::Type;
    liveTick_ = 0;
    sendStep(link, nowMs);
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
    const uint32_t interval = (step_ <= Step::Version) ? kInfoRetryMs : kLiveIntervalMs;
    if (nowMs - lastSendMs_ < interval) {
      return;
    }
    sendStep(link, nowMs);
  }

  bool supports(const Command c) const override {
    switch (c) {
      case Command::Horn:
      case Command::Beep:
      case Command::Lights:
      case Command::RideLeds:
      case Command::Volume:
      case Command::RequestInfo:
        return true;
      default:
        return false;
    }
  }

  bool send(const Command c, const int value, Link& link) override {
    if (!supports(c)) {
      return false;
    }
    const bool oldV11 = (layout_ == Layout::V11Old);
    uint8_t d[3] = {};
    size_t n = 0;
    switch (c) {
      case Command::Horn:
        // playSound (REF:2025-2036): value 0 = WheelLog's default sound, 1..255 = that sound number.
        if (value < 0 || value > 255) {
          return false;
        }
        d[0] = oldV11 ? kCtlPlaySoundOldV11 : kCtlPlaySound;
        d[1] = value == 0 ? kDefaultSound : static_cast<uint8_t>(value);
        d[2] = 0x01;
        n = 3;
        break;
      case Command::Beep:
        // WheelLog wheelBeep() (REF:273-281): playBeep(2) on V13/V14/V11y, playSound(0x18) elsewhere.
        if (isBeepModel()) {
          d[0] = kCtlPlaySound;
          d[1] = 0x02;
          d[2] = 0x64;
        } else {
          d[0] = oldV11 ? kCtlPlaySoundOldV11 : kCtlPlaySound;
          d[1] = kDefaultSound;
          d[2] = 0x01;
        }
        n = 3;
        break;
      case Command::Lights:
        if (value != 0 && value != 1) {
          return false;
        }
        d[0] = oldV11 ? kCtlHeadlightOldV11 : kCtlHeadlight;
        d[1] = static_cast<uint8_t>(value);
        n = 2;
        if (layout_ == Layout::V12) {
          d[2] = 0;  // V12: low beam, high beam (REF:2070-2080); 1 = low beam only
          n = 3;
        }
        break;
      case Command::RideLeds:
        if (value != 0 && value != 1) {
          return false;
        }
        d[0] = kCtlDrl;
        d[1] = static_cast<uint8_t>(value);
        n = 2;
        break;
      case Command::Volume:
        if (value < 0 || value > 100) {
          return false;
        }
        d[0] = kCtlVolume;
        d[1] = static_cast<uint8_t>(value);
        n = 2;
        break;
      case Command::RequestInfo:
        step_ = Step::Type;
        lastSendMs_ = 0;
        return sendStep(link, lastSendMs_);
      default:
        return false;
    }
    return write(link, kFlagData, kCmdControl, d, n);
  }

 private:
  // WheelLog's connection sequence (REF:204-271): type, serial, versions, settings, "useless", stats, then live.
  enum class Step : uint8_t { Type, Serial, Version, Settings, Useless, Stats, Live };

  bool isBeepModel() const {
    return modelId_ == 81 || modelId_ == 82 || modelId_ == 91 || modelId_ == 92 || modelId_ == 62;
  }

  static bool write(Link& link, const uint8_t flag, const uint8_t cmd, const uint8_t* data, const size_t n) {
    uint8_t frame[32];
    const size_t size = buildFrame(flag, cmd, data, n, frame, sizeof frame);
    return size > 0 && link.write(frame, size);
  }

  bool sendStep(Link& link, const uint32_t nowMs) {
    lastSendMs_ = nowMs;
    static constexpr uint8_t kType = 0x01, kSerial = 0x02, kVersion = 0x06, kSettings = 0x20;
    static constexpr uint8_t kUseless[2] = {0x00, 0x01};
    switch (step_) {
      case Step::Type:
        return write(link, kFlagInfo, kCmdMainInfo, &kType, 1);
      case Step::Serial:
        return write(link, kFlagInfo, kCmdMainInfo, &kSerial, 1);
      case Step::Version:
        return write(link, kFlagInfo, kCmdMainInfo, &kVersion, 1);
      case Step::Settings:
        step_ = Step::Useless;
        return write(link, kFlagData, kCmdSettings, &kSettings, 1);
      case Step::Useless:
        step_ = Step::Stats;
        return write(link, kFlagData, kCmdSomething1, kUseless, 2);
      case Step::Stats:
        step_ = Step::Live;
        liveTick_ = 0;
        return write(link, kFlagData, kCmdStats, nullptr, 0);
      case Step::Live:
        ++liveTick_;
        if (liveTick_ % kStatsEveryNTicks == 0) {
          return write(link, kFlagData, kCmdStats, nullptr, 0);
        }
        return write(link, kFlagData, kCmdLive, nullptr, 0);
    }
    return false;
  }

  void advance(const Step from, const Step to) {
    if (step_ == from) {
      step_ = to;
      lastSendMs_ = 0;  // send the next request on the next poll
    }
  }

  bool handleFrame(Telemetry& t) {
    const uint8_t* d = in_.data();
    const size_t n = in_.dataLen();
    if (in_.flag() == kFlagInfo && in_.cmd() == kCmdMainInfo) {
      return handleMainInfo(d, n, t);
    }
    if (in_.flag() != kFlagData) {
      return false;
    }
    switch (in_.cmd()) {
      case kCmdLive:
        return handleLive(d, n, t);
      case kCmdStats:
        return decodeStats(d, n, t);
      default:
        return false;
    }
  }

  bool handleLive(const uint8_t* d, const size_t n, Telemetry& t) {
    switch (layout_) {
      case Layout::V11Old:
        return decodeLiveV11Old(d, n, t);
      case Layout::V11:
        return decodeLiveV11V12(d, n, false, t);
      case Layout::V12:
        return decodeLiveV11V12(d, n, true, t);
      case Layout::New:
        return decodeLiveNew(d, n, series_ == 8, t);  // V13 error bytes start at 76, the others at 77
      case Layout::Unknown:
        break;
    }
    return false;
  }

  // REF:552-617, eucInmotionV2.js:102-141.
  bool handleMainInfo(const uint8_t* d, const size_t n, Telemetry& t) {
    if (n == 0) {
      return false;
    }
    if (d[0] == 0x01 && n >= 6) {
      // 01 <mainSeries> <series> <type> <batch> <feature> <reverse>
      series_ = d[2];
      modelId_ = d[2] * 10 + d[3];
      const char* known = modelName(modelId_);
      if (known[0] != '\0') {
        std::snprintf(t.model, sizeof t.model, "%s", known);
      } else {
        std::snprintf(t.model, sizeof t.model, "InMotion %d/%d", static_cast<int>(d[2]), static_cast<int>(d[3]));
      }
      t.mark(F_MODEL);
      layout_ = layoutFor(modelId_, series_, protoVer_);
      advance(Step::Type, Step::Serial);
      return true;
    }
    if (d[0] == 0x02 && n >= 17) {
      std::memcpy(t.serial, d + 1, 16);
      t.serial[16] = '\0';
      t.mark(F_SERIAL);
      advance(Step::Serial, Step::Version);
      return true;
    }
    if (d[0] == 0x06 && n >= 24) {
      const uint16_t main3 = u16(d, 11);
      const uint8_t main2 = d[13];
      const uint8_t main1 = d[14];
      std::snprintf(t.firmware, sizeof t.firmware, "%d.%d.%d", static_cast<int>(main1), static_cast<int>(main2),
                    static_cast<int>(main3));
      t.mark(F_FIRMWARE);
      protoVer_ = (main1 < kOldLayoutMain1Below && main2 < kOldLayoutMain2Below) ? 1 : 2;
      layout_ = layoutFor(modelId_, series_, protoVer_);
      advance(Step::Version, Step::Settings);
      return true;
    }
    return false;
  }

  Unpacker in_;
  Step step_ = Step::Type;
  uint32_t lastSendMs_ = 0;
  uint32_t liveTick_ = 0;
  int modelId_ = 0;
  int series_ = 0;
  int protoVer_ = 0;  // 0 unknown, 1 V11 < 1.4, 2 otherwise
  Layout layout_ = Layout::Unknown;
};

}  // namespace

size_t buildFrame(const uint8_t flag, const uint8_t cmd, const uint8_t* data, const size_t n, uint8_t* out,
                  const size_t cap) {
  if (n + 3 > kMaxBody) {
    return 0;
  }
  uint8_t body[kMaxBody];
  body[0] = flag;
  body[1] = static_cast<uint8_t>(n + 1);
  body[2] = cmd;
  if (n > 0) {
    std::memcpy(body + 3, data, n);
  }
  const size_t bodyLen = n + 3;
  const uint8_t check = crc::xor8(body, bodyLen);  // over the unescaped body (REF:2340-2355)
  // Worst case every body byte is escaped.
  const size_t escapes =
      static_cast<size_t>(std::count_if(body, body + bodyLen, [](const uint8_t b) { return b == 0xAA || b == 0xA5; }));
  const size_t size = 2 + bodyLen + escapes + 1;
  if (size > cap) {
    return 0;
  }
  size_t o = 0;
  out[o++] = 0xAA;
  out[o++] = 0xAA;
  for (size_t i = 0; i < bodyLen; ++i) {
    if (body[i] == 0xAA || body[i] == 0xA5) {
      out[o++] = 0xA5;
    }
    out[o++] = body[i];
  }
  out[o++] = check;
  return o;
}

const char* modelName(const int modelId) {
  switch (modelId) {
    case 61:
      return "Inmotion V11";
    case 62:
      return "Inmotion V11y";
    case 71:
      return "Inmotion V12 HS";
    case 72:
      return "Inmotion V12 HT";
    case 73:
      return "Inmotion V12 PRO";
    case 81:
      return "Inmotion V13";
    case 82:
      return "Inmotion V13 PRO";
    case 91:
      return "Inmotion V14 50GB";
    case 92:
      return "Inmotion V14 50S";
    case 111:
      return "Inmotion V12S";
    case 121:
      return "Inmotion V9";
    default:
      return "";
  }
}

void Unpacker::reset() {
  state_ = State::Idle;
  count_ = 0;
  len_ = 0;
  prev_ = 0;
  escaped_ = false;
}

bool Unpacker::push(const uint8_t raw) {
  if (state_ == State::Idle) {
    if (raw == 0xAA && prev_ == 0xAA) {
      state_ = State::Flag;
      count_ = 0;
      prev_ = 0;
      escaped_ = false;
      return false;
    }
    prev_ = raw;
    return false;
  }
  if (!escaped_ && raw == 0xA5) {
    escaped_ = true;
    prev_ = 0;
    return false;
  }
  // A raw (unescaped) AA AA can only be a new header: data bytes 0xAA are always escaped. Resync on it.
  if (!escaped_ && raw == 0xAA && prev_ == 0xAA) {
    state_ = State::Flag;
    count_ = 0;
    prev_ = 0;
    return false;
  }
  prev_ = escaped_ ? 0 : raw;
  escaped_ = false;
  if (count_ >= kMaxBody) {
    reset();
    return false;
  }
  body_[count_++] = raw;
  switch (state_) {
    case State::Flag:
      state_ = State::Len;
      return false;
    case State::Len:
      len_ = raw;
      if (len_ == 0) {  // "No data, packet dropped" (eucInmotionV2.js:408)
        reset();
        return false;
      }
      state_ = State::Collect;
      return false;
    case State::Collect:
    case State::Idle:
      break;
  }
  if (count_ < static_cast<size_t>(len_) + 3) {
    return false;
  }
  state_ = State::Idle;
  prev_ = 0;
  // check = XOR of flag, len, cmd and data (REF:2380-2392; the AA AA header XORs to zero).
  return crc::xor8(body_, count_ - 1) == body_[count_ - 1];
}

// ---- simulator -------------------------------------------------------------------------------------------------

void InmotionV2Sim::setModel(const uint8_t series, const uint8_t type, const uint8_t fwMain1, const uint8_t fwMain2,
                             const uint16_t fwMain3) {
  series_ = series;
  type_ = type;
  fwMain1_ = fwMain1;
  fwMain2_ = fwMain2;
  fwMain3_ = fwMain3;
}

size_t InmotionV2Sim::lastControl(uint8_t* out, const size_t cap) const {
  const size_t n = std::min(cap, controlLen_);
  std::copy_n(control_, n, out);
  return n;
}

Layout InmotionV2Sim::layout() const {
  const int protoVer = (fwMain1_ < kOldLayoutMain1Below && fwMain2_ < kOldLayoutMain2Below) ? 1 : 2;
  return layoutFor(series_ * 10 + type_, series_, protoVer);
}

void InmotionV2Sim::tick(const uint32_t, const Telemetry& t) { t_ = t; }

bool InmotionV2Sim::pop(uint8_t* out, const size_t cap, size_t& size) {
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

void InmotionV2Sim::reply(const uint8_t flag, const uint8_t cmd, const uint8_t* data, const size_t n) {
  if (outCount_ >= 4) {
    return;
  }
  Frame& f = out_[(outHead_ + outCount_) % 4];
  f.size = buildFrame(flag, cmd, data, n, f.bytes, sizeof f.bytes);
  if (f.size > 0) {
    ++outCount_;
  }
}

void InmotionV2Sim::onWrite(const uint8_t* data, const size_t size) {
  for (size_t i = 0; i < size; ++i) {
    if (in_.push(data[i])) {
      handleRequest();
    }
  }
}

void InmotionV2Sim::handleRequest() {
  const uint8_t* d = in_.data();
  const size_t n = in_.dataLen();
  uint8_t out[kMaxBody] = {};
  if (in_.flag() == kFlagInfo && in_.cmd() == kCmdMainInfo && n >= 1) {
    if (d[0] == 0x01) {
      // "020601010100 -v11" (REF:559-567): main series, series, type, batch, feature, reverse.
      const uint8_t type[7] = {0x01, 0x02, series_, type_, 0x01, 0x01, 0x00};
      reply(kFlagInfo, kCmdMainInfo, type, sizeof type);
    } else if (d[0] == 0x02) {
      out[0] = 0x02;
      std::memcpy(out + 1, "2309A1B2C3D4E5F6", 16);
      reply(kFlagInfo, kCmdMainInfo, out, 17);
    } else if (d[0] == 0x06) {
      out[0] = 0x06;
      putU16(out, 11, fwMain3_);  // main board: data[11..12], data[13], data[14] (REF:590-593)
      out[13] = fwMain2_;
      out[14] = fwMain1_;
      reply(kFlagInfo, kCmdMainInfo, out, 25);
    }
    return;
  }
  if (in_.flag() != kFlagData) {
    return;
  }
  switch (in_.cmd()) {
    case kCmdLive: {
      size_t len = 0;
      const Telemetry& t = t_;
      // Common head: volt u16/100, current s16/100.
      putU16(out, 0, static_cast<uint16_t>(std::lround(t.voltage * 100)));
      putS16(out, 2, t.current * 100);
      const uint8_t state = static_cast<uint8_t>((t.charging ? 0x80 : 0x00) | 0x40);
      switch (layout()) {
        case Layout::V11Old:
          len = 50;
          putS16(out, 4, t.speedKmh * 100);
          putS16(out, 8, t.power);
          putU16(out, 12, static_cast<uint16_t>(std::lround(t.tripKm * 100)));
          putU16(out, 14, static_cast<uint16_t>(std::lround(t.rangeKm * 100)));
          out[16] = static_cast<uint8_t>(std::lround(t.batteryPct)) & 0x7F;
          out[17] = tempRaw(t, F_TEMP_MOS, t.tempMos);
          out[18] = tempRaw(t, F_TEMP_MOTOR, t.tempMotor);
          out[19] = tempRaw(t, F_TEMP_BATTERY, t.tempBattery);
          out[20] = tempRaw(t, F_TEMP_BOARD, t.tempBoard);
          putS16(out, 22, t.pitch * 100);
          putS16(out, 26, t.roll * 100);
          putU16(out, 36, static_cast<uint16_t>(std::lround(t.pwm * 100)));
          out[38] = state;
          out[39] = t.lightMode != 0 ? 0x01 : 0x00;
          break;
        case Layout::V11:
        case Layout::V12: {
          const bool v12 = layout() == Layout::V12;
          const size_t shift = v12 ? 0 : 4;
          const size_t tail = v12 ? 0 : 2;
          len = 66 + tail;
          putS16(out, 4, t.speedKmh * 100);
          putS16(out, 8, t.pwm * 100);
          putS16(out, 10, t.power);
          putS16(out, 16, t.pitch * 100);
          putS16(out, 20, t.roll * 100);
          putU16(out, 22 + shift, static_cast<uint16_t>(std::lround(t.tripKm * 100)));
          putU16(out, 24 + shift, static_cast<uint16_t>(std::lround(t.batteryPct * 100)));
          putU16(out, 26 + shift, static_cast<uint16_t>(std::lround(t.rangeKm * 100)));
          out[40 + tail] = tempRaw(t, F_TEMP_MOS, t.tempMos);
          out[41 + tail] = tempRaw(t, F_TEMP_MOTOR, t.tempMotor);
          out[42 + tail] = tempRaw(t, F_TEMP_BATTERY, t.tempBattery);
          out[43 + tail] = tempRaw(t, F_TEMP_BOARD, t.tempBoard);
          out[54 + tail] = state;
          out[55 + tail] = t.lightMode != 0 ? 0x01 : 0x00;
          break;
        }
        case Layout::New:
          len = 84;
          putS16(out, 8, t.speedKmh * 100);
          putS16(out, 14, t.pwm * kNewLayoutPwmDivisor);
          putS16(out, 16, t.power);
          putU16(out, 34, static_cast<uint16_t>(std::lround(t.batteryPct * 100)));
          putU16(out, 36, static_cast<uint16_t>(std::lround(t.batteryPct * 100)));
          out[kNewLayoutTempMos] = tempRaw(t, F_TEMP_MOS, t.tempMos);
          out[kNewLayoutTempMotor] = tempRaw(t, F_TEMP_MOTOR, t.tempMotor);
          out[kNewLayoutTempBattery] = tempRaw(t, F_TEMP_BATTERY, t.tempBattery);
          out[kNewLayoutTempBoard] = tempRaw(t, F_TEMP_BOARD, t.tempBoard);
          out[74] = state;
          out[76] = t.lightMode != 0 ? 0x02 : 0x00;
          break;
        case Layout::Unknown:
          return;
      }
      reply(kFlagData, kCmdLive, out, len);
      return;
    }
    case kCmdStats:
      putU32(out, 0, static_cast<uint32_t>(std::lround(t_.totalKm * 100)));
      putU32(out, 12, t_.rideSeconds);
      putU32(out, 16, t_.rideSeconds);
      reply(kFlagData, kCmdStats, out, 20);
      return;
    case kCmdSettings:
      out[0] = 0x20;
      reply(kFlagData, kCmdSettings, out, 10);
      return;
    case kCmdControl:
      controlLen_ = std::min(n, sizeof control_);
      std::copy_n(d, controlLen_, control_);
      if (n >= 2 && (d[0] == kCtlHeadlight || d[0] == kCtlHeadlightOldV11)) {
        lightOn_ = d[1] != 0;
      }
      return;
    default:
      return;
  }
}

}  // namespace euc::inmotionv2

namespace euc {

std::unique_ptr<Protocol> makeInmotionV2() { return std::make_unique<inmotionv2::InmotionV2Protocol>(); }
std::unique_ptr<WheelSimulator> makeInmotionV2Sim() { return std::make_unique<inmotionv2::InmotionV2Sim>(); }

}  // namespace euc
