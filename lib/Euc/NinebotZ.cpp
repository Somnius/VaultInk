#include "NinebotZ.h"

#include <algorithm>
#include <cmath>

#include "CrcUtil.h"
#include "Protocols.h"

namespace euc::ninebotz {

namespace {

// eucNinebotZ.js:26-32.
constexpr GattProfile kProfile{"6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                               "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
                               "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                               "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
                               false,
                               ""};

uint16_t u16(const uint8_t* d, const size_t i) { return static_cast<uint16_t>(d[i] | (d[i + 1] << 8)); }
uint32_t u32(const uint8_t* d, const size_t i) {
  return static_cast<uint32_t>(d[i]) | (static_cast<uint32_t>(d[i + 1]) << 8) |
         (static_cast<uint32_t>(d[i + 2]) << 16) | (static_cast<uint32_t>(d[i + 3]) << 24);
}
void putU16(uint8_t* d, const size_t i, const uint16_t v) {
  d[i] = static_cast<uint8_t>(v & 0xFF);
  d[i + 1] = static_cast<uint8_t>(v >> 8);
}
void putU32(uint8_t* d, const size_t i, const uint32_t v) {
  putU16(d, i, static_cast<uint16_t>(v & 0xFFFF));
  putU16(d, i + 2, static_cast<uint16_t>(v >> 16));
}

bool decodeLive(const uint8_t* d, const size_t n, Telemetry& t) {
  if (n < kLiveDataLen) {
    return false;
  }
  t.totalKm = u32(d, kOffTotal) / 1000.0f;
  t.rideSeconds = u16(d, kOffRideTime);
  t.tempMos = u16(d, kOffTemp) / 10.0f;
  t.voltage = u16(d, kOffVoltage) / 100.0f;
  t.current = static_cast<int16_t>(u16(d, kOffCurrent)) / 100.0f;
  t.speedKmh = u16(d, kOffSpeed) / 100.0f;
  t.mark(F_TOTAL);
  t.mark(F_RIDE_TIME);
  t.mark(F_TEMP_MOS);
  t.mark(F_VOLTAGE);
  t.mark(F_CURRENT);
  t.mark(F_SPEED);
  return true;
}

class NinebotZProtocol : public Protocol {
 public:
  Brand brand() const override { return Brand::NinebotZ; }
  const GattProfile& profile() const override { return kProfile; }
  const char* name() const override { return "Ninebot Z"; }

  void start(Link& link, const uint32_t nowMs) override {
    in_.reset();
    request(link, nowMs);
  }
  bool feed(const uint8_t* data, const size_t size, Telemetry& t) override {
    bool changed = false;
    for (size_t i = 0; i < size; ++i) {
      if (in_.push(data[i])) {
        replied_ = true;
        changed = decodeLive(in_.data(), in_.dataLen(), t) || changed;
      }
    }
    return changed;
  }
  void poll(Link& link, const uint32_t nowMs) override {
    const uint32_t since = nowMs - lastSendMs_;
    if (since < kPollIntervalMs || (!replied_ && since < kReplyTimeoutMs)) {
      return;
    }
    request(link, nowMs);
  }
  bool supports(Command) const override { return false; }
  bool send(Command, int, Link&) override { return false; }

 private:
  void request(Link& link, const uint32_t nowMs) {
    link.write(kRequest, sizeof kRequest);
    lastSendMs_ = nowMs;
    replied_ = false;
  }

  Unpacker in_;
  uint32_t lastSendMs_ = 0;
  bool replied_ = false;
};

class NinebotZSim : public WheelSimulator {
 public:
  Brand brand() const override { return Brand::NinebotZ; }
  void onWrite(const uint8_t* data, const size_t size) override {
    // Only the live request is known; answer it whole, the transport chunks it into 20-byte notifications.
    if (size != sizeof kRequest || !std::equal(data, data + size, kRequest) || pending_) {
      return;
    }
    uint8_t d[kLiveDataLen] = {};
    putU16(d, kOffSpeedFirst, static_cast<uint16_t>(std::lround(t_.speedKmh * 100)));
    putU32(d, kOffTotal, static_cast<uint32_t>(std::lround(t_.totalKm * 1000)));
    putU16(d, kOffRideTime, static_cast<uint16_t>(std::min<uint32_t>(t_.rideSeconds, 0xFFFF)));
    putU16(d, kOffTemp, static_cast<uint16_t>(std::lround(t_.tempMos * 10)));
    putU16(d, kOffVoltage, static_cast<uint16_t>(std::lround(t_.voltage * 100)));
    putU16(d, kOffCurrent, static_cast<uint16_t>(static_cast<int16_t>(std::lround(t_.current * 100))));
    putU16(d, kOffSpeed, static_cast<uint16_t>(std::lround(t_.speedKmh * 100)));
    putU16(d, kOffAvgSpeed, static_cast<uint16_t>(std::lround(t_.speedKmh * 100)));
    size_ = buildReply(d, sizeof d, frame_, sizeof frame_);
    pending_ = size_ > 0;
  }
  void tick(const uint32_t, const Telemetry& t) override { t_ = t; }
  bool pop(uint8_t* out, const size_t cap, size_t& size) override {
    if (!pending_ || size_ > cap) {
      return false;
    }
    std::copy_n(frame_, size_, out);
    size = size_;
    pending_ = false;
    return true;
  }

 private:
  Telemetry t_{};
  uint8_t frame_[kMaxFrame] = {};
  size_t size_ = 0;
  bool pending_ = false;
};

}  // namespace

size_t buildReply(const uint8_t* data, const size_t n, uint8_t* out, const size_t cap) {
  const size_t size = kHeader + n + 2;
  if (size > cap || n > 255) {
    return 0;
  }
  out[0] = 0x5A;
  out[1] = 0xA5;
  out[2] = static_cast<uint8_t>(n);
  out[3] = 0x14;  // src: the request's dst
  out[4] = 0x3E;  // dst: the request's src
  out[5] = 0x04;
  out[6] = 0xB0;
  std::copy_n(data, n, out + kHeader);
  putU16(out, kHeader + n, crc::ninebotCheck(out + 2, n + 5));
  return size;
}

void Unpacker::reset() {
  count_ = 0;
  size_ = 0;
  need_ = 0;
  prev_ = 0;
  collecting_ = false;
}

bool Unpacker::push(const uint8_t raw) {
  if (!collecting_) {
    if (prev_ == 0x5A && raw == 0xA5) {
      collecting_ = true;
      buf_[0] = 0x5A;
      buf_[1] = 0xA5;
      count_ = 2;
      prev_ = 0;
      return false;
    }
    prev_ = raw;
    return false;
  }
  buf_[count_++] = raw;
  if (count_ == 3) {
    need_ = static_cast<size_t>(raw) + kHeader + 2;
    if (need_ > kMaxFrame) {
      reset();
    }
    return false;
  }
  if (count_ < need_) {
    return false;
  }
  size_ = count_;
  collecting_ = false;
  count_ = 0;
  return u16(buf_, size_ - 2) == crc::ninebotCheck(buf_ + 2, size_ - 4);
}

}  // namespace euc::ninebotz

namespace euc {

std::unique_ptr<Protocol> makeNinebotZ() { return std::make_unique<ninebotz::NinebotZProtocol>(); }
std::unique_ptr<WheelSimulator> makeNinebotZSim() { return std::make_unique<ninebotz::NinebotZSim>(); }

}  // namespace euc
