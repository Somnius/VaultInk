#include "Vesc.h"

#include <algorithm>
#include <cmath>

#include "CrcUtil.h"
#include "Protocols.h"

namespace euc::vesc {

namespace {

// VESCDash/source/BLE/eucPM.mc:39-41, name prefix from WD/BLE/GarminEUCBleDelegate.mc:310-318.
constexpr GattProfile kProfile{"6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                               "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
                               "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                               "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
                               false,
                               "VESC"};

// Big-endian reader that walks the payload like VESCDash's VByteArray pops.
class Reader {
 public:
  Reader(const uint8_t* p, const size_t n) : p_(p), n_(n) {}
  bool has(const size_t k) const { return i_ + k <= n_; }
  uint8_t u8() { return p_[i_++]; }
  int16_t s16() {
    const uint16_t v = static_cast<uint16_t>((p_[i_] << 8) | p_[i_ + 1]);
    i_ += 2;
    return static_cast<int16_t>(v);
  }
  uint32_t u32() {
    const uint32_t v = (static_cast<uint32_t>(p_[i_]) << 24) | (static_cast<uint32_t>(p_[i_ + 1]) << 16) |
                       (static_cast<uint32_t>(p_[i_ + 2]) << 8) | static_cast<uint32_t>(p_[i_ + 3]);
    i_ += 4;
    return v;
  }
  int32_t s32() { return static_cast<int32_t>(u32()); }
  void skip(const size_t k) { i_ += k; }

 private:
  const uint8_t* p_;
  size_t n_;
  size_t i_ = 0;
};

class Writer {
 public:
  explicit Writer(uint8_t* p) : p_(p) {}
  void u8(const uint8_t v) { p_[i_++] = v; }
  void s16(const int32_t v) {
    p_[i_++] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p_[i_++] = static_cast<uint8_t>(v & 0xFF);
  }
  void u32(const uint32_t v) {
    p_[i_++] = static_cast<uint8_t>(v >> 24);
    p_[i_++] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p_[i_++] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p_[i_++] = static_cast<uint8_t>(v & 0xFF);
  }
  void s32(const int32_t v) { u32(static_cast<uint32_t>(v)); }
  size_t size() const { return i_; }

 private:
  uint8_t* p_;
  size_t i_ = 0;
};

class VescProtocol : public Protocol {
 public:
  Brand brand() const override { return Brand::Vesc; }
  const GattProfile& profile() const override { return kProfile; }
  const char* name() const override { return "VESC"; }

  void start(Link& link, const uint32_t nowMs) override {
    in_.reset();
    request(link, nowMs);
  }
  bool feed(const uint8_t* data, const size_t size, Telemetry& t) override {
    bool changed = false;
    for (size_t i = 0; i < size; ++i) {
      if (in_.push(data[i])) {
        replied_ = true;
        changed = decodeValuesSetup(in_.payload(), in_.payloadLen(), t) || changed;
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

class VescSim : public WheelSimulator {
 public:
  Brand brand() const override { return Brand::Vesc; }
  void onWrite(const uint8_t* data, const size_t size) override {
    if (size != sizeof kRequest || !std::equal(data, data + size, kRequest) || pending_) {
      return;
    }
    uint8_t p[kSetupPayload] = {};
    Writer w(p);
    const Telemetry& t = t_;
    w.u8(kCommGetValuesSetup);
    w.s16(std::lround(t.tempMos * 10));
    w.s16(std::lround(t.tempMotor * 10));
    w.s32(0);  // motor current
    w.s32(std::lround(t.current * 100));
    w.s16(std::lround(t.pwm * 10));
    w.s32(0);  // rpm
    w.s32(std::lround(t.speedKmh / 3.6 * 1000));
    w.s16(std::lround(t.voltage * 10));
    w.s16(std::lround(t.batteryPct * 10));
    w.s32(0);  // Ah
    w.s32(0);  // Ah charged
    w.s32(0);  // Wh
    w.s32(0);  // Wh charged
    w.s32(0);  // tachometer
    w.s32(0);  // tachometer abs
    w.s32(0);  // position
    w.u8(static_cast<uint8_t>(t.faultCode));
    w.u8(1);   // vesc id
    w.u8(1);   // num vescs
    w.s32(0);  // battery Wh
    w.u32(static_cast<uint32_t>(std::lround(t.totalKm * 1000)));
    w.u32(t.rideSeconds * 1000u);
    size_ = buildFrame(p, w.size(), frame_, sizeof frame_);
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
  uint8_t frame_[kSetupPayload + 5] = {};
  size_t size_ = 0;
  bool pending_ = false;
};

}  // namespace

size_t buildFrame(const uint8_t* payload, const size_t n, uint8_t* out, const size_t cap) {
  if (n == 0 || n > kMaxPayload || n + 5 > cap) {
    return 0;
  }
  out[0] = 0x02;
  out[1] = static_cast<uint8_t>(n);
  std::copy_n(payload, n, out + 2);
  const uint16_t crc = crc::crc16Xmodem(payload, n);
  out[2 + n] = static_cast<uint8_t>(crc >> 8);
  out[3 + n] = static_cast<uint8_t>(crc & 0xFF);
  out[4 + n] = 0x03;
  return n + 5;
}

void Unpacker::reset() {
  count_ = 0;
  size_ = 0;
  need_ = 0;
  collecting_ = false;
}

bool Unpacker::push(const uint8_t raw) {
  if (!collecting_) {
    if (raw == 0x02) {
      collecting_ = true;
      buf_[0] = raw;
      count_ = 1;
    }
    return false;
  }
  buf_[count_++] = raw;
  if (count_ == 2) {
    need_ = static_cast<size_t>(raw) + 5;  // [02][len] + payload + crc16 + [03] (frameDecoder.mc:50)
    if (raw == 0) {
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
  if (buf_[size_ - 1] != 0x03) {
    return false;
  }
  const uint16_t crc = crc::crc16Xmodem(buf_ + 2, size_ - 5);
  return buf_[size_ - 3] == static_cast<uint8_t>(crc >> 8) && buf_[size_ - 2] == static_cast<uint8_t>(crc & 0xFF);
}

bool decodeValuesSetup(const uint8_t* p, const size_t n, Telemetry& t) {
  Reader r(p, n);
  if (!r.has(27) || r.u8() != kCommGetValuesSetup) {
    return false;
  }
  t.tempMos = r.s16() / 10.0f;
  t.tempMotor = r.s16() / 10.0f;
  r.skip(4);  // motor current
  t.current = r.s32() / 100.0f;
  const float duty = std::fabs(r.s16() / 10.0f);
  r.skip(4);  // rpm
  t.speedKmh = std::fabs(r.s32() / 1000.0f) * 3.6f;
  t.voltage = r.s16() / 10.0f;
  t.batteryPct = std::clamp(r.s16() / 10.0f, 0.0f, 100.0f);
  t.mark(F_TEMP_MOS);
  t.mark(F_TEMP_MOTOR);
  t.mark(F_CURRENT);
  t.mark(F_SPEED);
  t.mark(F_VOLTAGE);
  t.mark(F_BATTERY);
  if (duty <= 100.0f) {  // VESCDash's sanity check (frameDecoder.mc:107-111)
    t.pwm = duty;
    t.mark(F_PWM);
  }
  if (r.has(28 + 3)) {
    r.skip(28);  // Ah, Ah charged, Wh, Wh charged, tachometer, tachometer abs, position
    t.faultCode = r.u8();
    t.mark(F_FAULT);
    r.skip(2);  // vesc id, num vescs
    if (r.has(8)) {
      r.skip(4);  // battery Wh
      t.totalKm = r.u32() / 1000.0f;
      t.mark(F_TOTAL);
    }
  }
  return true;
}

}  // namespace euc::vesc

namespace euc {

std::unique_ptr<Protocol> makeVesc() { return std::make_unique<vesc::VescProtocol>(); }
std::unique_ptr<WheelSimulator> makeVescSim() { return std::make_unique<vesc::VescSim>(); }

}  // namespace euc
