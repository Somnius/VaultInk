#include "Begode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

#include "BatteryCurve.h"
#include "Protocols.h"

namespace euc {

namespace {

constexpr GattProfile kBegodeGatt{"0000ffe0-0000-1000-8000-00805f9b34fb",
                                  "0000ffe1-0000-1000-8000-00805f9b34fb",
                                  "0000ffe0-0000-1000-8000-00805f9b34fb",
                                  "0000ffe1-0000-1000-8000-00805f9b34fb",
                                  false,
                                  "GW"};

// Single ASCII command bytes (eucBegode.js:11-41, eucConfig.mc:43-85).
constexpr uint8_t kCmdBeep = 'b';
constexpr uint8_t kCmdLightsOn = 'Q';
constexpr uint8_t kCmdLightsOff = 'E';
constexpr uint8_t kCmdLightsStrobe = 'T';
constexpr uint8_t kCmdPedalSoft = 's';
constexpr uint8_t kCmdPedalMedium = 'f';
constexpr uint8_t kCmdPedalHard = 'h';
constexpr uint8_t kCmdKilometers = 'g';
constexpr uint8_t kCmdMiles = 'm';
constexpr uint8_t kCmdModel = 'N';
constexpr uint8_t kCmdFirmware = 'V';
constexpr uint8_t kCmdPrefixW =
    'W';  // 'W' 'M' digit: LED mode, three separate writes (GarminEUCMenu2Delegate_generic.mc:216-240)
constexpr uint8_t kCmdLedMode = 'M';
constexpr int kLedModeMax = 4;  // eucConfig.mc dictLedMode "0".."4"

constexpr uint8_t kTypeLive = 0;
constexpr uint8_t kTypeTotals = 4;
constexpr uint8_t kLengthByte = 0x18;
constexpr uint8_t kTrailer = 0x5A;

uint16_t u16be(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
int16_t s16be(const uint8_t* p) { return static_cast<int16_t>(u16be(p)); }
uint32_t u32be(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
void putU16be(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v & 0xFF);
}
void putU32be(uint8_t* p, const uint32_t v) {
  putU16be(p, static_cast<uint16_t>(v >> 16));
  putU16be(p + 2, static_cast<uint16_t>(v & 0xFFFF));
}

bool writeByte(Link& link, const uint8_t c) { return link.write(&c, 1); }

// Frame B byte 14 alarm bits (eucBegode.js:65-76) to AlarmBit.
uint16_t alarmBits(const uint8_t code) {
  struct Map {
    uint8_t bit;
    uint16_t alarm;
  };
  constexpr Map kMap[] = {{0, ALARM_HIGH_POWER},   {1, ALARM_SPEED2},    {2, ALARM_SPEED1},     {3, ALARM_LOW_VOLTAGE},
                          {4, ALARM_OVER_VOLTAGE}, {5, ALARM_HIGH_TEMP}, {6, ALARM_HALL_ERROR}, {7, ALARM_TRANSPORT}};
  return std::accumulate(std::begin(kMap), std::end(kMap), static_cast<uint16_t>(0),
                         [code](const uint16_t bits, const Map& m) {
                           return static_cast<uint16_t>(((code >> m.bit) & 1u) != 0 ? bits | m.alarm : bits);
                         });
}

void copyText(char* dst, const size_t cap, const uint8_t* src, size_t n) {
  // Trim trailing spaces, CR/LF and NULs like eucWatch's .trim().
  while (n > 0 && (src[n - 1] == ' ' || src[n - 1] == '\r' || src[n - 1] == '\n' || src[n - 1] == 0)) {
    --n;
  }
  n = std::min(n, cap - 1);
  std::copy(src, src + n, dst);
  dst[n] = 0;
}

}  // namespace

float begodeVoltageMultiplier(const char* model) {
  struct Entry {
    const char* name;
    float mult;
  };
  // eucBegode.js:43-63
  constexpr Entry kTable[] = {{"Mten3", 1.25f},    {"MCM5", 1.25f},      {"RecioWheel", 1.25f}, {"T3", 1.25f},
                              {"Nikola", 1.5f},    {"Msuper Pro", 1.5f}, {"MSP C30", 1.5f},     {"MSP C38", 1.5f},
                              {"RS C30", 1.5f},    {"RS C38", 1.5f},     {"EX", 1.5f},          {"EX20S C30", 1.5f},
                              {"EX20S C38", 1.5f}, {"Monster", 1.5f},    {"EXN", 1.5f},         {"Monster Pro", 1.5f},
                              {"Master", 2.0f}};
  const auto it = std::find_if(std::begin(kTable), std::end(kTable),
                               [model](const Entry& e) { return std::strcmp(e.name, model) == 0; });
  return it == std::end(kTable) ? 1.0f : it->mult;
}

const GattProfile& BegodeProtocol::profile() const { return kBegodeGatt; }

void BegodeProtocol::start(Link& link, const uint32_t nowMs) {
  (void)nowMs;
  // eucBegode.js:265-271: ask for model and firmware once.
  writeByte(link, kCmdModel);
  writeByte(link, kCmdFirmware);
}

void BegodeProtocol::poll(Link& link, const uint32_t nowMs) {
  (void)link;
  (void)nowMs;  // the wheel streams by itself
}

bool BegodeProtocol::textReply(const uint8_t* data, const size_t size, Telemetry& t) {
  // eucBegode.js:153-163: a notification starting "NAME" is the model ("NAME:Mten3"), "GW" the firmware string.
  if (size >= 5 && std::memcmp(data, "NAME", 4) == 0) {
    copyText(t.model, sizeof(t.model), data + 5, size - 5);
    if (std::strcmp(t.model, "Barton") == 0) {
      std::strcpy(t.model, "RecioWheel");  // eucBegode.js:156
    }
    voltMult_ = begodeVoltageMultiplier(t.model);
    t.mark(F_MODEL);
    return true;
  }
  if (size >= 3 && data[0] == 'G' && data[1] == 'W') {
    copyText(t.firmware, sizeof(t.firmware), data, size);
    t.mark(F_FIRMWARE);
    return true;
  }
  return false;
}

bool BegodeProtocol::feedByte(const uint8_t c) {
  // WheelLog's state machine as copied in frameDecoder.mc:52-82.
  if (collecting_) {
    frame_[len_++] = c;
    prev_ = c;
    if ((len_ == 20 && c != kLengthByte) || (len_ > 20 && len_ <= kFrame && c != kTrailer)) {
      collecting_ = false;
      len_ = 0;
      return false;
    }
    if (len_ == kFrame) {
      collecting_ = false;
      return true;
    }
    return false;
  }
  if (prev_ == 0x55 && c == 0xAA) {
    frame_[0] = 0x55;
    frame_[1] = 0xAA;
    len_ = 2;
    collecting_ = true;
  }
  prev_ = c;
  return false;
}

bool BegodeProtocol::decodeFrame(Telemetry& t) const {
  const uint8_t* f = frame_;
  if (f[18] == kTypeLive) {
    // frameDecoder.mc:108-113, eucBegode.js:177-215
    t.voltage = u16be(f + 2) / 100.0f * voltMult_;
    t.speedKmh = std::fabs(s16be(f + 4) * 3.6f / 100.0f);
    t.tripKm = u16be(f + 8) / 1000.0f;
    t.current = s16be(f + 10) * kBegodeCurrentScale;
    t.power = t.voltage * t.current;
    t.tempMos = s16be(f + 12) / 340.0f + 36.53f;
    t.batteryPct = batteryPercentFromVoltage(t.voltage, 67.2f * voltMult_);
    t.mark(F_VOLTAGE);
    t.mark(F_SPEED);
    t.mark(F_TRIP);
    t.mark(F_CURRENT);
    t.mark(F_POWER);
    t.mark(F_TEMP_MOS);
    t.mark(F_BATTERY);
    return true;
  }
  if (f[18] == kTypeTotals) {
    // frameDecoder.mc:84-97, eucBegode.js:219-256
    t.totalKm = u32be(f + 2) / 1000.0f;
    const uint16_t settings = u16be(f + 6);
    t.rideMode =
        static_cast<uint8_t>((settings >> 13) & 0x3);  // 0 soft, 1 medium, 2 hard (eucConfig.mc dictPedalStatus)
    t.alarmBits = alarmBits(f[14]);
    t.mark(F_TOTAL);
    t.mark(F_RIDE_MODE);
    t.mark(F_ALARMS);
    // byte 15 is a headlight state in eucWatch but WheelDash says the wheel does not report light mode
    // (frameDecoder.mc:95). UNVERIFIED: F_LIGHT is not set.
    return true;
  }
  return false;  // type 1 "Master" PWM frame and unknown types: ignored
}

bool BegodeProtocol::feed(const uint8_t* data, const size_t size, Telemetry& t) {
  if (data == nullptr || size == 0) {
    return false;
  }
  if (textReply(data, size, t)) {
    return true;
  }
  bool changed = false;
  for (size_t i = 0; i < size; ++i) {
    if (feedByte(data[i]) && decodeFrame(t)) {
      changed = true;
    }
  }
  return changed;
}

bool BegodeProtocol::supports(const Command c) const {
  switch (c) {
    case Command::Beep:
    case Command::Lights:
    case Command::RideLeds:
    case Command::PedalMode:
    case Command::Miles:
    case Command::RequestInfo:
      return true;
    default:
      return false;
  }
}

bool BegodeProtocol::send(const Command c, const int value, Link& link) {
  switch (c) {
    case Command::Beep:
      return writeByte(link, kCmdBeep);
    case Command::Lights:
      switch (value) {
        case 0:
          return writeByte(link, kCmdLightsOff);
        case 1:
          return writeByte(link, kCmdLightsOn);
        case 3:
          return writeByte(link, kCmdLightsStrobe);
        default:
          return false;  // no "auto" headlight command in the sources
      }
    case Command::RideLeds: {
      // LED mode 0..4 as three writes 'W', 'M', digit (GarminEUCMenu2Delegate_generic.mc:216-240; eucBegode.js:39).
      if (value < 0 || value > kLedModeMax) {
        return false;
      }
      return writeByte(link, kCmdPrefixW) && writeByte(link, kCmdLedMode) &&
             writeByte(link, static_cast<uint8_t>('0' + value));
    }
    case Command::PedalMode:
      switch (value) {
        case 0:
          return writeByte(link, kCmdPedalSoft);
        case 1:
          return writeByte(link, kCmdPedalMedium);
        case 2:
          return writeByte(link, kCmdPedalHard);
        default:
          return false;
      }
    case Command::Miles:
      switch (value) {
        case 0:
          return writeByte(link, kCmdKilometers);
        case 1:
          return writeByte(link, kCmdMiles);
        default:
          return false;
      }
    case Command::RequestInfo:
      return writeByte(link, kCmdModel) && writeByte(link, kCmdFirmware);
    default:
      return false;
  }
}

// ---- simulator ----

void BegodeSimulator::setModel(const char* model) {
  copyText(model_, sizeof(model_), reinterpret_cast<const uint8_t*>(model), std::strlen(model));
}

void BegodeSimulator::setFirmware(const char* firmware) {
  copyText(firmware_, sizeof(firmware_), reinterpret_cast<const uint8_t*>(firmware), std::strlen(firmware));
}

void BegodeSimulator::push(const uint8_t* frame, const size_t size) {
  if (count_ == kQueue || size > kMaxFrame) {
    return;
  }
  const size_t slot = (head_ + count_) % kQueue;
  std::copy(frame, frame + size, queue_[slot]);
  sizes_[slot] = size;
  ++count_;
}

bool BegodeSimulator::pop(uint8_t* out, const size_t cap, size_t& size) {
  if (count_ == 0 || cap < sizes_[head_]) {
    size = 0;
    return false;
  }
  size = sizes_[head_];
  std::copy(queue_[head_], queue_[head_] + size, out);
  head_ = (head_ + 1) % kQueue;
  --count_;
  return true;
}

void BegodeSimulator::onWrite(const uint8_t* data, const size_t size) {
  if (size == 0) {
    return;
  }
  lastWrite_ = data[0];
  ++writes_;
  uint8_t reply[kMaxFrame] = {};
  if (data[0] == kCmdModel) {
    // "NAME:" + model (eucBegode.js:153-155)
    const size_t n = std::min(std::strlen(model_), kMaxFrame - 5);
    std::memcpy(reply, "NAME:", 5);
    std::copy(model_, model_ + n, reinterpret_cast<char*>(reply) + 5);
    push(reply, 5 + n);
  } else if (data[0] == kCmdFirmware) {
    const size_t n = std::min(std::strlen(firmware_), kMaxFrame);
    std::copy(firmware_, firmware_ + n, reinterpret_cast<char*>(reply));
    push(reply, n);
  }
}

void BegodeSimulator::tick(const uint32_t nowMs, const Telemetry& t) {
  (void)nowMs;
  const float mult = begodeVoltageMultiplier(model_);
  uint8_t a[kMaxFrame] = {};
  a[0] = 0x55;
  a[1] = 0xAA;
  putU16be(a + 2, static_cast<uint16_t>(std::lround(t.voltage / mult * 100.0f)));
  putU16be(a + 4, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t.speedKmh / 3.6f * 100.0f))));
  putU16be(a + 8, static_cast<uint16_t>(std::lround(t.tripKm * 1000.0f)));
  putU16be(a + 10, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t.current / kBegodeCurrentScale))));
  putU16be(a + 12, static_cast<uint16_t>(static_cast<int16_t>(std::lround((t.tempMos - 36.53f) * 340.0f))));
  a[18] = kTypeLive;
  a[19] = kLengthByte;
  std::fill(a + 20, a + kMaxFrame, kTrailer);
  push(a, kMaxFrame);

  uint8_t b[kMaxFrame] = {};
  b[0] = 0x55;
  b[1] = 0xAA;
  putU32be(b + 2, static_cast<uint32_t>(std::lround(t.totalKm * 1000.0f)));
  putU16be(b + 6, static_cast<uint16_t>((t.rideMode & 0x3) << 13));
  uint8_t code = 0;
  constexpr uint16_t kOrder[] = {ALARM_HIGH_POWER,   ALARM_SPEED2,    ALARM_SPEED1,     ALARM_LOW_VOLTAGE,
                                 ALARM_OVER_VOLTAGE, ALARM_HIGH_TEMP, ALARM_HALL_ERROR, ALARM_TRANSPORT};
  for (size_t bit = 0; bit < 8; ++bit) {
    if ((t.alarmBits & kOrder[bit]) != 0) {
      code |= static_cast<uint8_t>(1u << bit);
    }
  }
  b[14] = code;
  b[18] = kTypeTotals;
  b[19] = kLengthByte;
  std::fill(b + 20, b + kMaxFrame, kTrailer);
  push(b, kMaxFrame);
}

std::unique_ptr<Protocol> makeBegode() { return std::make_unique<BegodeProtocol>(); }
std::unique_ptr<WheelSimulator> makeBegodeSim() { return std::make_unique<BegodeSimulator>(); }

}  // namespace euc
