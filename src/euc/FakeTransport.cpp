#include <Arduino.h>
#include <Euc.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include "EucTransport.h"

namespace euc {
namespace {

constexpr int kBrandCount = static_cast<int>(Brand::Count);

// A looping ride of about three minutes: pull away, cruise, a quick burst, brake to a stop, wait, again. The
// numbers move the way a real wheel's do (voltage sags with current, temperature creeps up, the trip grows).
// The full-charge voltage of a typical pack of each brand, so the demo wheel reads like that wheel.
float packVolts(const Brand brand) {
  switch (brand) {
    case Brand::Veteran:
      return 100.8f;
    case Brand::NinebotS:
    case Brand::NinebotE:
    case Brand::NinebotZ:
      return 54.6f;
    case Brand::Vesc:
      return 50.4f;
    default:
      return 84.0f;
  }
}

class RideScript {
 public:
  void setPack(const float volts) { pack_ = volts; }

  void reset(const uint32_t nowMs) {
    startMs_ = nowMs;
    lastMs_ = nowMs;
    trip_ = 0;
    battery_ = 92.0f;
    temp_ = 31.0f;
    seconds_ = 0;
    top_ = 0;
  }

  void step(const uint32_t nowMs, Telemetry& t) {
    const float dt = std::min(1.0f, static_cast<float>(nowMs - lastMs_) / 1000.0f);
    lastMs_ = nowMs;
    const float phase = std::fmod(static_cast<float>(nowMs - startMs_) / 1000.0f, 180.0f);
    float target = 0;
    if (phase < 10) {
      target = 0;
    } else if (phase < 30) {
      target = 24.0f * (phase - 10) / 20.0f;
    } else if (phase < 90) {
      target = 24.0f + 3.0f * std::sin(phase / 4.0f);
    } else if (phase < 105) {
      target = 38.0f;  // a burst, trips the speed alarm of the default thresholds
    } else if (phase < 125) {
      target = 38.0f * (125 - phase) / 20.0f;
    } else {
      target = 0;
    }
    speed_ += (target - speed_) * std::min(1.0f, dt * 1.5f);
    if (speed_ < 0.2f) speed_ = 0;
    const float accel = (target - speed_) * 0.6f;
    const float amps = speed_ * 0.55f + std::max(0.0f, accel) * 3.0f - (accel < 0 ? -accel * 1.5f : 0.0f);
    trip_ += speed_ * dt / 3600.0f;
    battery_ = std::max(5.0f, battery_ - (speed_ * 0.0006f + 0.0004f) * dt * 60.0f / 60.0f);
    temp_ += ((30.0f + speed_ * 0.55f + amps * 0.25f) - temp_) * dt * 0.02f;
    if (speed_ > 1) seconds_ += dt;
    top_ = std::max(top_, speed_);

    t.mark(static_cast<Field>(F_SPEED | F_VOLTAGE | F_CURRENT | F_POWER | F_BATTERY | F_TEMP_MOS | F_TEMP_MOTOR |
                              F_TRIP | F_TOTAL | F_RIDE_TIME | F_TOP_SPEED | F_PWM | F_LIGHT));
    t.speedKmh = speed_;
    t.voltage = pack_ * (0.78f + 0.22f * battery_ / 100.0f) - amps * 0.04f;
    t.current = amps;
    t.power = t.voltage * amps;
    t.batteryPct = battery_;
    t.tempMos = temp_ + 3.0f;
    t.tempMotor = temp_;
    t.tripKm = trip_;
    t.totalKm = 1234.5f + trip_;
    t.rideSeconds = static_cast<uint32_t>(seconds_);
    t.topSpeedKmh = top_;
    t.pwm = std::min(99.0f, speed_ * 1.8f + amps * 0.4f);
    t.lightMode = 1;
    t.alarmBits = speed_ > 32.0f ? ALARM_SPEED2 : 0;
    if (t.alarmBits != 0) t.mark(F_ALARMS);
    std::snprintf(t.model, sizeof(t.model), "DEMO");
    std::snprintf(t.firmware, sizeof(t.firmware), "1.0");
    std::snprintf(t.serial, sizeof(t.serial), "DEMO0001");
    t.mark(static_cast<Field>(F_MODEL | F_FIRMWARE | F_SERIAL));
  }

 private:
  float pack_ = 84.0f;
  uint32_t startMs_ = 0;
  uint32_t lastMs_ = 0;
  float speed_ = 0;
  float trip_ = 0;
  float battery_ = 92;
  float temp_ = 31;
  float seconds_ = 0;
  float top_ = 0;
};

class FakeTransport final : public Transport {
 public:
  bool begin() override {
    state_ = LinkState::Idle;
    return true;
  }
  void end() override {
    sim_.reset();
    state_ = LinkState::Off;
  }

  void startScan() override {
    scanStart_ = millis();
    scanning_ = true;
    state_ = LinkState::Scanning;
  }
  void stopScan() override {
    scanning_ = false;
    if (state_ == LinkState::Scanning) state_ = LinkState::Idle;
  }
  int scanCount() const override { return scanning_ && millis() - scanStart_ < 600 ? 0 : kBrandCount; }
  bool scanAt(const int index, ScanEntry& out) const override {
    if (index < 0 || index >= kBrandCount) return false;
    out = ScanEntry{};
    std::snprintf(out.name, sizeof(out.name), "Demo %s", brandName(static_cast<Brand>(index)));
    std::snprintf(out.addr, sizeof(out.addr), "DE:AD:BE:EF:00:%02X", index);
    out.rssi = static_cast<int8_t>(-50 - index * 3);
    out.demo = true;
    out.demoBrand = static_cast<Brand>(index);
    return true;
  }

  void connect(const ScanEntry& wheel, const GattProfile&, const Brand brand) override {
    brand_ = wheel.demo ? wheel.demoBrand : brand;
    sim_ = makeSimulator(brand_);
    connectAt_ = millis() + 1200;
    state_ = LinkState::Connecting;
    head_ = tail_ = 0;
  }
  void disconnect() override {
    sim_.reset();
    state_ = LinkState::Idle;
  }

  LinkState state() const override { return state_; }
  const char* lastError() const override { return ""; }

  bool write(const uint8_t* data, const size_t size) override {
    if (state_ != LinkState::Connected || !sim_) return false;
    sim_->onWrite(data, size);
    return true;
  }

  // One notification per call, as the radio delivers them: some protocols (Begode's text replies) rely on it.
  size_t read(uint8_t* out, const size_t cap) override {
    if (head_ == tail_) return 0;
    const size_t length = ring_[tail_];
    tail_ = (tail_ + 1) % sizeof(ring_);
    size_t n = 0;
    for (size_t i = 0; i < length; ++i) {
      if (n < cap) out[n++] = ring_[tail_];
      tail_ = (tail_ + 1) % sizeof(ring_);
    }
    return n;
  }

  void tick(const uint32_t nowMs) override {
    if (state_ == LinkState::Connecting && nowMs >= connectAt_) {
      state_ = LinkState::Connected;
      script_.setPack(packVolts(brand_));
      script_.reset(nowMs);
      lastTick_ = nowMs;
    }
    if (state_ != LinkState::Connected || !sim_) return;
    if (nowMs - lastTick_ >= 100) {
      lastTick_ = nowMs;
      script_.step(nowMs, telemetry_);
      sim_->tick(nowMs, telemetry_);
    }
    // Frames out in notifications of at most 20 bytes, as a real link delivers them.
    uint8_t frame[160];
    size_t size = 0;
    while (sim_->pop(frame, sizeof(frame), size)) {
      for (size_t at = 0; at < size; at += 20) push(frame + at, std::min<size_t>(20, size - at));
    }
  }

 private:
  void push(const uint8_t* data, const size_t size) {
    const size_t used = (head_ + sizeof(ring_) - tail_) % sizeof(ring_);
    if (used + size + 1 >= sizeof(ring_)) return;  // full: the app is slow, drop the notification like a radio would
    ring_[head_] = static_cast<uint8_t>(size);
    head_ = (head_ + 1) % sizeof(ring_);
    for (size_t i = 0; i < size; ++i) {
      ring_[head_] = data[i];
      head_ = (head_ + 1) % sizeof(ring_);
    }
  }

  LinkState state_ = LinkState::Off;
  Brand brand_ = Brand::Begode;
  std::unique_ptr<WheelSimulator> sim_;
  RideScript script_;
  Telemetry telemetry_;
  uint32_t connectAt_ = 0;
  uint32_t lastTick_ = 0;
  uint32_t scanStart_ = 0;
  bool scanning_ = false;
  uint8_t ring_[1024] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
};

}  // namespace

Transport* makeFakeTransport() { return new (std::nothrow) FakeTransport(); }

}  // namespace euc
