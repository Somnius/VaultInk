#pragma once

#include <cstdint>

#include "Euc.h"

// Watch-side alarms (the thresholds the rider sets on the reader, not the wheel's own flags; those arrive as
// Telemetry::alarmBits and are folded in as AlarmKind::Wheel). Modelled on WheelDash `WD/util/GarminEUCAlarms.mc`:
// PWM has priority over everything, then temperature, then speed; a PWM reading more than 5 % past the threshold is
// the "hard" level that repeats every 200 ms instead of every second. The caller must call evaluate() on EVERY
// telemetry update, however rarely the e-ink screen refreshes.
namespace euc {

enum class AlarmKind : uint8_t {
  None,
  PwmHard,  // pwm > threshold + 5 (WheelDash level 2)
  Pwm,      // pwm > threshold
  Temperature,
  Speed,
  Current,
  LowBattery,
  Wheel,  // Telemetry::alarmBits
  Count
};

enum class AlarmResult : uint8_t {
  Quiet,   // nothing above a threshold
  Fire,    // vibrate/beep now: a new alarm, or a repeat after its interval
  Active,  // still above a threshold, already signalled recently
};

// 0 disables a threshold (WheelDash convention).
struct AlarmThresholds {
  float pwmPct = 80.0f;
  float speedKmh = 0.0f;
  float tempC = 65.0f;
  float currentA = 0.0f;
  float lowBatteryPct = 20.0f;
  bool wheelAlarms = true;  // honour the wheel's own alarmBits
};

struct AlarmEvent {
  AlarmResult result = AlarmResult::Quiet;
  AlarmKind kind = AlarmKind::None;
  uint16_t wheelBits = 0;  // the alarmBits when kind == Wheel
};

class Alarms {
 public:
  Alarms() = default;
  explicit Alarms(const AlarmThresholds& t) : thresholds_(t) {}

  void setThresholds(const AlarmThresholds& t) { thresholds_ = t; }
  const AlarmThresholds& thresholds() const { return thresholds_; }

  // Evaluates every threshold against `t` and returns the highest-priority alarm. PWM thresholds are never
  // evaluated when the wheel does not report PWM (F_PWM clear: Begode).
  AlarmEvent evaluate(const Telemetry& t, uint32_t nowMs);

  // The alarm currently above its threshold (after hysteresis), None when quiet.
  AlarmKind active() const { return active_; }
  void reset();

  // Hysteresis margins: an alarm clears only once the value is this far back below its threshold.
  static constexpr float kPwmMargin = 2.0f;
  static constexpr float kSpeedMargin = 1.0f;
  static constexpr float kTempMargin = 2.0f;
  static constexpr float kCurrentMargin = 5.0f;
  static constexpr float kBatteryMargin = 2.0f;
  static constexpr float kPwmHardStep = 5.0f;  // WheelDash: threshold + 5
  static constexpr uint32_t kRepeatMs = 1000;
  static constexpr uint32_t kRepeatHardMs = 200;

  // The highest temperature the wheel reports, or false when it reports none.
  static bool maxTemp(const Telemetry& t, float& outC);

 private:
  bool latch(AlarmKind k, bool above, bool below);

  AlarmThresholds thresholds_;
  bool on_[static_cast<size_t>(AlarmKind::Count)] = {};
  AlarmKind active_ = AlarmKind::None;
  uint32_t lastFireMs_ = 0;
  bool fired_ = false;
  uint16_t lastWheelBits_ = 0;
};

const char* alarmKindName(AlarmKind k);

}  // namespace euc
