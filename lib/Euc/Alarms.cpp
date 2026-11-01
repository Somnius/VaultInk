#include "Alarms.h"

#include <algorithm>
#include <array>
#include <utility>

namespace euc {

namespace {
size_t idx(const AlarmKind k) { return static_cast<size_t>(k); }
}  // namespace

bool Alarms::maxTemp(const Telemetry& t, float& outC) {
  bool any = false;
  float best = 0.0f;
  const std::array<std::pair<Field, float>, 4> temps = {{{F_TEMP_MOS, t.tempMos},
                                                         {F_TEMP_MOTOR, t.tempMotor},
                                                         {F_TEMP_BATTERY, t.tempBattery},
                                                         {F_TEMP_BOARD, t.tempBoard}}};
  for (const auto& [field, value] : temps) {
    if (t.has(field) && (!any || value > best)) {
      best = value;
      any = true;
    }
  }
  if (any) {
    outC = best;
  }
  return any;
}

// Latches `k` on when `above`, off when `below`; in between it keeps its state (hysteresis).
bool Alarms::latch(const AlarmKind k, const bool above, const bool below) {
  bool& state = on_[idx(k)];
  if (above) {
    state = true;
  } else if (below) {
    state = false;
  }
  return state;
}

void Alarms::reset() {
  std::fill(std::begin(on_), std::end(on_), false);
  active_ = AlarmKind::None;
  lastFireMs_ = 0;
  fired_ = false;
  lastWheelBits_ = 0;
}

AlarmEvent Alarms::evaluate(const Telemetry& t, const uint32_t nowMs) {
  const AlarmThresholds& th = thresholds_;

  // PWM: only when the wheel reports it. A Begode frame has no PWM, so both levels stay off there.
  if (th.pwmPct > 0.0f && t.has(F_PWM)) {
    const float hard = th.pwmPct + kPwmHardStep;
    latch(AlarmKind::PwmHard, t.pwm > hard, t.pwm < hard - kPwmMargin);
    latch(AlarmKind::Pwm, t.pwm > th.pwmPct, t.pwm < th.pwmPct - kPwmMargin);
  } else {
    on_[idx(AlarmKind::PwmHard)] = false;
    on_[idx(AlarmKind::Pwm)] = false;
  }

  float temp = 0.0f;
  if (th.tempC > 0.0f && maxTemp(t, temp)) {
    latch(AlarmKind::Temperature, temp > th.tempC, temp < th.tempC - kTempMargin);
  } else {
    on_[idx(AlarmKind::Temperature)] = false;
  }

  if (th.speedKmh > 0.0f && t.has(F_SPEED)) {
    latch(AlarmKind::Speed, t.speedKmh > th.speedKmh, t.speedKmh < th.speedKmh - kSpeedMargin);
  } else {
    on_[idx(AlarmKind::Speed)] = false;
  }

  if (th.currentA > 0.0f && t.has(F_CURRENT)) {
    const float a = t.current < 0.0f ? -t.current : t.current;
    latch(AlarmKind::Current, a > th.currentA, a < th.currentA - kCurrentMargin);
  } else {
    on_[idx(AlarmKind::Current)] = false;
  }

  if (th.lowBatteryPct > 0.0f && t.has(F_BATTERY)) {
    latch(AlarmKind::LowBattery, t.batteryPct<th.lowBatteryPct, t.batteryPct> th.lowBatteryPct + kBatteryMargin);
  } else {
    on_[idx(AlarmKind::LowBattery)] = false;
  }

  const uint16_t wheelBits = (th.wheelAlarms && t.has(F_ALARMS)) ? t.alarmBits : 0;
  on_[idx(AlarmKind::Wheel)] = wheelBits != 0;
  const bool newWheelBits = (wheelBits & ~lastWheelBits_) != 0;
  lastWheelBits_ = wheelBits;

  // Priority order is the enum order (PwmHard first).
  AlarmKind kind = AlarmKind::None;
  for (size_t i = idx(AlarmKind::PwmHard); i < idx(AlarmKind::Count); ++i) {
    if (on_[i]) {
      kind = static_cast<AlarmKind>(i);
      break;
    }
  }

  AlarmEvent ev;
  ev.kind = kind;
  ev.wheelBits = kind == AlarmKind::Wheel ? wheelBits : 0;
  if (kind == AlarmKind::None) {
    active_ = AlarmKind::None;
    fired_ = false;
    ev.result = AlarmResult::Quiet;
    return ev;
  }

  const uint32_t repeat = kind == AlarmKind::PwmHard ? kRepeatHardMs : kRepeatMs;
  const bool changed = kind != active_;
  const bool due = !fired_ || (nowMs - lastFireMs_) >= repeat;
  active_ = kind;
  if (changed || due || (kind == AlarmKind::Wheel && newWheelBits)) {
    fired_ = true;
    lastFireMs_ = nowMs;
    ev.result = AlarmResult::Fire;
  } else {
    ev.result = AlarmResult::Active;
  }
  return ev;
}

const char* alarmKindName(const AlarmKind k) {
  switch (k) {
    case AlarmKind::None:
      return "none";
    case AlarmKind::PwmHard:
      return "PWM!";
    case AlarmKind::Pwm:
      return "PWM";
    case AlarmKind::Temperature:
      return "Temp";
    case AlarmKind::Speed:
      return "Speed";
    case AlarmKind::Current:
      return "Current";
    case AlarmKind::LowBattery:
      return "Battery";
    case AlarmKind::Wheel:
      return "Wheel";
    case AlarmKind::Count:
      break;
  }
  return "?";
}

}  // namespace euc
