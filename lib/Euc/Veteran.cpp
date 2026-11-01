#include "Veteran.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "BatteryCurve.h"
#include "Protocols.h"

namespace euc {

namespace {

constexpr GattProfile kVeteranGatt{"0000ffe0-0000-1000-8000-00805f9b34fb",
                                   "0000ffe1-0000-1000-8000-00805f9b34fb",
                                   "0000ffe0-0000-1000-8000-00805f9b34fb",
                                   "0000ffe1-0000-1000-8000-00805f9b34fb",
                                   false,
                                   "LK"};

// ASCII commands (eucVeteran.js:3-19, eucConfig.mc:141-153).
constexpr const char* kCmdPedalSoft = "SETs";
constexpr const char* kCmdPedalMedium = "SETm";
constexpr const char* kCmdPedalHard = "SETh";
constexpr const char* kCmdLightOn = "SetLightON";
constexpr const char* kCmdLightOff = "SetLightOFF";
constexpr const char* kCmdResetTrip = "CLEARMETER";
constexpr const char* kCmdBeep = "b";  // eucVeteran.js:5 only (not in WheelDash)

constexpr uint8_t kHeader[3] = {0xDC, 0x5A, 0x5C};

uint16_t u16be(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
int16_t s16be(const uint8_t* p) { return static_cast<int16_t>(u16be(p)); }
// Word-swapped u32: low word first (frameDecoder.mc:284-295, eucVeteran.js:57-58).
uint32_t u32swapped(const uint8_t* p) { return (static_cast<uint32_t>(u16be(p + 2)) << 16) | u16be(p); }
void putU16be(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v & 0xFF);
}
void putU32swapped(uint8_t* p, const uint32_t v) {
  putU16be(p, static_cast<uint16_t>(v & 0xFFFF));
  putU16be(p + 2, static_cast<uint16_t>(v >> 16));
}

bool writeText(Link& link, const char* s) { return link.write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)); }

}  // namespace

uint32_t veteranCrc32(const uint8_t* data, const size_t size) {
  // Same table as frameDecoder.mc:131-175 (zlib CRC-32), computed bitwise.
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) {
      crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

const GattProfile& VeteranProtocol::profile() const { return kVeteranGatt; }

void VeteranProtocol::start(Link& link, const uint32_t nowMs) {
  (void)link;
  (void)nowMs;  // the wheel streams by itself; neither app requests anything (eucVeteran.js:195-206)
}

void VeteranProtocol::poll(Link& link, const uint32_t nowMs) {
  (void)link;
  (void)nowMs;
}

void VeteranProtocol::reset() {
  state_ = State::Sync;
  len_ = 0;
  payload_ = 0;
  prev1_ = 0;
  prev2_ = 0;
}

bool VeteranProtocol::feedByte(const uint8_t c) {
  // WheelLog's state machine as copied in frameDecoder.mc:193-249.
  switch (state_) {
    case State::Collect: {
      // Sanity bytes WheelDash checks before accepting: byte 22 == 0, byte 23 even, byte 30 == 0, byte 31 < 4.
      if ((len_ == 22 && c != 0) || (len_ == 23 && (c & 0xFE) != 0) || (len_ == 30 && c != 0) ||
          (len_ == 31 && (c & 0xFC) != 0)) {
        reset();
        return false;
      }
      frame_[len_++] = c;
      if (len_ == payload_ + 4) {
        const size_t payload = payload_;
        reset();
        if (payload >= kVeteranCrcMinLen) {
          // New firmware: CRC32 over the first `payload` bytes, big endian in the last 4 (frameDecoder.mc:210-219).
          const uint32_t given = (static_cast<uint32_t>(u16be(frame_ + payload)) << 16) | u16be(frame_ + payload + 2);
          return veteranCrc32(frame_, payload) == given;
        }
        return true;
      }
      return false;
    }
    case State::Length:
      if (c < kMinPayload) {
        reset();
        return false;
      }
      frame_[3] = c;
      payload_ = c;
      len_ = 4;
      state_ = State::Collect;
      return false;
    case State::Sync:
    default:
      if (prev2_ == kHeader[0] && prev1_ == kHeader[1] && c == kHeader[2]) {
        std::copy(kHeader, kHeader + 3, frame_);
        state_ = State::Length;
      }
      prev2_ = prev1_;
      prev1_ = c;
      return false;
  }
}

void VeteranProtocol::decodeFrame(Telemetry& t) const {
  // frameDecoder.mc:256-317, eucVeteran.js:39-79
  const uint8_t* f = frame_;
  t.voltage = u16be(f + 4) / 100.0f;
  t.speedKmh = std::fabs(s16be(f + 6) / 10.0f);
  t.tripKm = u32swapped(f + 8) / 1000.0f;
  t.totalKm = u32swapped(f + 12) / 1000.0f;
  t.current = s16be(f + 16) * kVeteranCurrentScale;
  t.power = t.voltage * t.current;
  t.tempMos = s16be(f + 18) / 100.0f;
  const uint16_t fw = u16be(f + 28);
  std::snprintf(t.firmware, sizeof(t.firmware), "%u.%03u", fw / 1000u, fw % 1000u);
  t.rideMode = static_cast<uint8_t>(u16be(f + 30) & 0xFF);  // 0 hard, 1 medium, 2 soft (eucConfig.mc dictPedalStatus)
  t.pwm = u16be(f + 34) / 100.0f;
  t.batteryPct = batteryPercentFromVoltage(t.voltage, fw >= kVeteranPattonFirmware ? 126.0f : 100.8f);
  t.mark(F_VOLTAGE);
  t.mark(F_SPEED);
  t.mark(F_TRIP);
  t.mark(F_TOTAL);
  t.mark(F_CURRENT);
  t.mark(F_POWER);
  t.mark(F_TEMP_MOS);
  t.mark(F_FIRMWARE);
  t.mark(F_RIDE_MODE);
  t.mark(F_PWM);
  t.mark(F_BATTERY);
}

bool VeteranProtocol::feed(const uint8_t* data, const size_t size, Telemetry& t) {
  if (data == nullptr) {
    return false;
  }
  bool changed = false;
  for (size_t i = 0; i < size; ++i) {
    if (feedByte(data[i])) {
      decodeFrame(t);
      changed = true;
    }
  }
  return changed;
}

bool VeteranProtocol::supports(const Command c) const {
  switch (c) {
    case Command::Beep:
    case Command::Lights:
    case Command::PedalMode:
    case Command::ResetTrip:
      return true;
    default:
      return false;
  }
}

bool VeteranProtocol::send(const Command c, const int value, Link& link) {
  switch (c) {
    case Command::Beep:
      return writeText(link, kCmdBeep);
    case Command::Lights:
      switch (value) {
        case 0:
          return writeText(link, kCmdLightOff);
        case 1:
          return writeText(link, kCmdLightOn);
        default:
          return false;
      }
    case Command::PedalMode:
      switch (value) {
        case 0:
          return writeText(link, kCmdPedalSoft);
        case 1:
          return writeText(link, kCmdPedalMedium);
        case 2:
          return writeText(link, kCmdPedalHard);
        default:
          return false;
      }
    case Command::ResetTrip:
      return writeText(link, kCmdResetTrip);
    default:
      return false;
  }
}

// ---- simulator ----

void VeteranSimulator::onWrite(const uint8_t* data, const size_t size) {
  const size_t n = std::min(size, sizeof(lastCmd_) - 1);
  std::copy(data, data + n, lastCmd_);
  lastCmd_[n] = 0;
  ++writes_;
}

bool VeteranSimulator::pop(uint8_t* out, const size_t cap, size_t& size) {
  if (!pending_ || cap < size_) {
    size = 0;
    return false;
  }
  size = size_;
  std::copy(frame_, frame_ + size_, out);
  pending_ = false;
  return true;
}

void VeteranSimulator::tick(const uint32_t nowMs, const Telemetry& t) {
  (void)nowMs;
  const uint8_t payload = newFormat_ ? 47 : 32;  // 47 = the Sherman S dumps (info.txt), 32 = the smallest old frame
  std::fill(frame_, frame_ + kMaxFrame, 0);
  std::copy(kHeader, kHeader + 3, frame_);
  frame_[3] = payload;
  putU16be(frame_ + 4, static_cast<uint16_t>(std::lround(t.voltage * 100.0f)));
  putU16be(frame_ + 6, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t.speedKmh * 10.0f))));
  putU32swapped(frame_ + 8, static_cast<uint32_t>(std::lround(t.tripKm * 1000.0f)));
  putU32swapped(frame_ + 12, static_cast<uint32_t>(std::lround(t.totalKm * 1000.0f)));
  putU16be(frame_ + 16, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t.current / kVeteranCurrentScale))));
  putU16be(frame_ + 18, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t.tempMos * 100.0f))));
  putU16be(frame_ + 28, firmware_);
  putU16be(frame_ + 30, t.rideMode);
  putU16be(frame_ + 34, static_cast<uint16_t>(std::lround(t.pwm * 100.0f)));
  size_ = static_cast<size_t>(payload) + 4;
  if (payload >= kVeteranCrcMinLen) {
    const uint32_t crc = veteranCrc32(frame_, payload);
    putU16be(frame_ + payload, static_cast<uint16_t>(crc >> 16));
    putU16be(frame_ + payload + 2, static_cast<uint16_t>(crc & 0xFFFF));
  }
  pending_ = true;
}

std::unique_ptr<Protocol> makeVeteran() { return std::make_unique<VeteranProtocol>(); }
std::unique_ptr<WheelSimulator> makeVeteranSim() { return std::make_unique<VeteranSimulator>(); }

}  // namespace euc
