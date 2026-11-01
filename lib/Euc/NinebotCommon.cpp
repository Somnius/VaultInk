#include "NinebotCommon.h"

#include <algorithm>
#include <cmath>

#include "CrcUtil.h"

namespace euc::ninebot {

namespace {

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

class RegisterProtocol : public Protocol {
 public:
  explicit RegisterProtocol(const Variant& v) : v_(v) {}
  Brand brand() const override { return v_.brand; }
  const GattProfile& profile() const override { return *v_.profile; }
  const char* name() const override { return v_.name; }

  void start(Link& link, const uint32_t nowMs) override {
    in_.reset();
    step_ = 0;
    alternate_ = false;
    sendStep(link, nowMs);
  }

  bool feed(const uint8_t* data, const size_t size, Telemetry& t) override {
    bool changed = false;
    for (size_t i = 0; i < size; ++i) {
      if (in_.push(data[i])) {
        changed = decodeReply(in_, t) || changed;
      }
    }
    return changed;
  }

  // eucWatch sends the next register 50 ms after each write, whether or not the wheel answered
  // (eucNinebotS.js:169-176).
  void poll(Link& link, const uint32_t nowMs) override {
    if (nowMs - lastSendMs_ < kStepIntervalMs) {
      return;
    }
    step_ = (step_ + 1) % kLoopSteps;
    sendStep(link, nowMs);
  }

  bool supports(Command) const override { return false; }
  bool send(Command, int, Link&) override { return false; }

 private:
  void sendStep(Link& link, const uint32_t nowMs) {
    uint8_t reg = 0;
    uint8_t count = 0;
    if (step_ == 14) {
      alternate_ = !alternate_;
    }
    loopRequest(step_, alternate_, reg, count);
    uint8_t req[kRequestSize];
    buildRead(v_.addr, reg, count, req, sizeof req);
    link.write(req, sizeof req);
    lastSendMs_ = nowMs;
  }

  Variant v_;
  Unpacker in_;
  size_t step_ = 0;
  bool alternate_ = false;
  uint32_t lastSendMs_ = 0;
};

class RegisterSim : public WheelSimulator {
 public:
  explicit RegisterSim(const Variant& v) : v_(v) {}
  Brand brand() const override { return v_.brand; }

  void onWrite(const uint8_t* data, const size_t size) override {
    for (size_t i = 0; i < size; ++i) {
      if (in_.push(data[i])) {
        answer();
      }
    }
  }
  void tick(const uint32_t, const Telemetry& t) override { t_ = t; }
  bool pop(uint8_t* out, const size_t cap, size_t& size) override {
    if (outCount_ == 0 || out_[outHead_].size > cap) {
      return false;
    }
    std::copy_n(out_[outHead_].bytes, out_[outHead_].size, out);
    size = out_[outHead_].size;
    outHead_ = (outHead_ + 1) % 4;
    --outCount_;
    return true;
  }

 private:
  struct Frame {
    uint8_t bytes[kMaxFrame];
    size_t size;
  };

  void answer() {
    // Request: 55 AA 03 addr 01 reg n ck ck.
    const uint8_t* f = in_.frame();
    if (in_.size() != kRequestSize || f[3] != v_.addr || f[4] != kCmdRead) {
      return;
    }
    const uint8_t reg = f[5];
    const uint8_t n = std::min<uint8_t>(f[6], 4);
    uint8_t data[4] = {};
    uint32_t value = 0;
    switch (reg) {
      case kRegSpeed:
        value = static_cast<uint32_t>(std::lround(t_.speedKmh * 1000));
        break;
      case kRegCurrent:
        value = static_cast<uint16_t>(static_cast<int16_t>(std::lround(t_.current * 100)));
        break;
      case kRegTemp:
        value = static_cast<uint32_t>(std::lround(t_.tempMos * 10));
        break;
      case kRegVoltage:
        value = static_cast<uint32_t>(std::lround(t_.voltage * 100));
        break;
      case kRegTrip:
        value = static_cast<uint32_t>(std::lround(t_.tripKm * kTripDivisor));
        break;
      case kRegRideTime:
        value = t_.rideSeconds;
        break;
      case kRegTotal:
        value = static_cast<uint32_t>(std::lround(t_.totalKm * 1000));
        break;
      case kRegRemaining:
        value = static_cast<uint32_t>(std::lround(t_.rangeKm * 100));
        break;
      case kRegRideMode:
        value = t_.rideMode;
        break;
      default:
        break;
    }
    putU32(data, 0, value);
    if (outCount_ >= 4) {
      return;
    }
    Frame& o = out_[(outHead_ + outCount_) % 4];
    o.size = buildReply(v_.addr, reg, data, n, o.bytes, sizeof o.bytes);
    if (o.size > 0) {
      ++outCount_;
    }
  }

  Variant v_;
  Unpacker in_;
  Telemetry t_{};
  Frame out_[4] = {};
  size_t outHead_ = 0;
  size_t outCount_ = 0;
};

}  // namespace

void loopRequest(const size_t step, const bool alternate, uint8_t& reg, uint8_t& count) {
  count = 2;
  switch (step % 3) {
    case 0:
      reg = kRegCurrent;
      return;
    case 1:
      reg = kRegSpeed;
      return;
    default:
      break;
  }
  switch (step) {
    case 2:
      reg = kRegTemp;
      return;
    case 5:
      reg = kRegVoltage;
      return;
    case 8:
      reg = kRegTrip;
      return;
    case 11:
      reg = kRegRideTime;
      return;
    case 14:
      if (alternate) {
        reg = kRegTotal;
        count = 4;
      } else {
        reg = kRegRemaining;
      }
      return;
    case 17:
      reg = kRegAvgSpeed;
      return;
    default:
      reg = kRegLock;  // step 20
      return;
  }
}

size_t buildRead(const uint8_t addr, const uint8_t reg, const uint8_t count, uint8_t* out, const size_t cap) {
  if (cap < kRequestSize) {
    return 0;
  }
  out[0] = 0x55;
  out[1] = 0xAA;
  out[2] = 0x03;
  out[3] = addr;
  out[4] = kCmdRead;
  out[5] = reg;
  out[6] = count;
  putU16(out, 7, crc::ninebotCheck(out + 2, 5));
  return kRequestSize;
}

size_t buildReply(const uint8_t src, const uint8_t reg, const uint8_t* data, const size_t n, uint8_t* out,
                  const size_t cap) {
  const size_t size = n + 8;
  if (size > cap || n > 250) {
    return 0;
  }
  out[0] = 0x55;
  out[1] = 0xAA;
  out[2] = static_cast<uint8_t>(n + 2);
  out[3] = src;
  out[4] = 0x04;
  out[5] = reg;
  std::copy_n(data, n, out + 6);
  putU16(out, 6 + n, crc::ninebotCheck(out + 2, n + 4));
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
    if (prev_ == 0x55 && raw == 0xAA) {
      collecting_ = true;
      buf_[0] = 0x55;
      buf_[1] = 0xAA;
      count_ = 2;
      need_ = 0;
      prev_ = 0;
      return false;
    }
    prev_ = raw;
    return false;
  }
  buf_[count_++] = raw;
  if (count_ == 3) {
    need_ = static_cast<size_t>(raw) + 6;
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
  prev_ = 0;
  if (!kVerifyReplyChecksum) {
    return true;
  }
  return u16(buf_, size_ - 2) == crc::ninebotCheck(buf_ + 2, size_ - 4);
}

bool decodeReply(const Unpacker& in, Telemetry& t) {
  const uint8_t* d = in.data();
  const size_t n = in.dataLen();
  if (n < 2) {
    return false;
  }
  const uint16_t v = u16(d, 0);
  switch (in.reg()) {
    case kRegSpeed:
      t.speedKmh = v / 1000.0f;
      t.mark(F_SPEED);
      return true;
    case kRegCurrent:
      t.current = static_cast<int16_t>(v) / 100.0f;
      t.mark(F_CURRENT);
      return true;
    case kRegTemp:
      t.tempMos = v / 10.0f;  // the wheel's single controller temperature
      t.mark(F_TEMP_MOS);
      return true;
    case kRegVoltage:
      t.voltage = v / 100.0f;
      t.mark(F_VOLTAGE);
      return true;
    case kRegTrip:
      t.tripKm = v / kTripDivisor;
      t.mark(F_TRIP);
      return true;
    case kRegRideTime:
      t.rideSeconds = v;
      t.mark(F_RIDE_TIME);
      return true;
    case kRegTotal:
      if (n < 4) {
        return false;
      }
      t.totalKm = u32(d, 0) / 1000.0f;
      t.mark(F_TOTAL);
      return true;
    case kRegRemaining:
      t.rangeKm = v / 100.0f;
      t.mark(F_RANGE);
      return true;
    case kRegRideMode:
      if (v >= 10) {  // eucNinebotS.js:117: values >= 10 acknowledge a change, not a mode
        return false;
      }
      t.rideMode = static_cast<uint8_t>(v);
      t.mark(F_RIDE_MODE);
      return true;
    default:
      return false;  // average speed, lock status: nothing in Telemetry for them
  }
}

std::unique_ptr<Protocol> makeRegisterProtocol(const Variant& v) { return std::make_unique<RegisterProtocol>(v); }
std::unique_ptr<WheelSimulator> makeRegisterSim(const Variant& v) { return std::make_unique<RegisterSim>(v); }

}  // namespace euc::ninebot
