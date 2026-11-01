#include <gtest/gtest.h>

#include "Alarms.h"

using namespace euc;

namespace {
Telemetry base() {
  Telemetry t;
  t.speedKmh = 20.0f;
  t.mark(F_SPEED);
  t.pwm = 40.0f;
  t.mark(F_PWM);
  t.tempMos = 30.0f;
  t.mark(F_TEMP_MOS);
  t.batteryPct = 80.0f;
  t.mark(F_BATTERY);
  t.current = 10.0f;
  t.mark(F_CURRENT);
  return t;
}

AlarmThresholds thresholds() {
  AlarmThresholds th;
  th.pwmPct = 80.0f;
  th.speedKmh = 40.0f;
  th.tempC = 65.0f;
  th.currentA = 60.0f;
  th.lowBatteryPct = 20.0f;
  return th;
}
}  // namespace

TEST(Alarms, QuietBelowEveryThreshold) {
  Alarms a(thresholds());
  const AlarmEvent ev = a.evaluate(base(), 1000);
  EXPECT_EQ(ev.result, AlarmResult::Quiet);
  EXPECT_EQ(ev.kind, AlarmKind::None);
  EXPECT_EQ(a.active(), AlarmKind::None);
}

TEST(Alarms, SpeedFiresOnceThenRepeatsEverySecond) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.speedKmh = 45.0f;
  EXPECT_EQ(a.evaluate(t, 1000).result, AlarmResult::Fire);
  EXPECT_EQ(a.evaluate(t, 1000).kind, AlarmKind::Speed);
  EXPECT_EQ(a.evaluate(t, 1100).result, AlarmResult::Active);  // 100 ms later: no re-fire
  EXPECT_EQ(a.evaluate(t, 1900).result, AlarmResult::Active);
  EXPECT_EQ(a.evaluate(t, 2000).result, AlarmResult::Fire);  // WheelDash: 1000 ms repeat
  EXPECT_EQ(a.evaluate(t, 2500).result, AlarmResult::Active);
}

TEST(Alarms, SpeedHysteresis) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.speedKmh = 40.5f;
  EXPECT_EQ(a.evaluate(t, 0).result, AlarmResult::Fire);
  t.speedKmh = 39.5f;  // within the 1 km/h margin: still active, no new fire
  EXPECT_EQ(a.evaluate(t, 100).result, AlarmResult::Active);
  EXPECT_EQ(a.active(), AlarmKind::Speed);
  t.speedKmh = 38.9f;  // below threshold - margin: clears
  EXPECT_EQ(a.evaluate(t, 200).result, AlarmResult::Quiet);
  EXPECT_EQ(a.active(), AlarmKind::None);
  t.speedKmh = 40.5f;  // and fires again at once when it comes back
  EXPECT_EQ(a.evaluate(t, 250).result, AlarmResult::Fire);
}

TEST(Alarms, PwmTwoLevelsAndPriority) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.speedKmh = 60.0f;  // speed alarm on, but PWM wins
  t.tempMos = 80.0f;   // temp alarm on, PWM still wins
  t.pwm = 82.0f;
  AlarmEvent ev = a.evaluate(t, 0);
  EXPECT_EQ(ev.result, AlarmResult::Fire);
  EXPECT_EQ(ev.kind, AlarmKind::Pwm);
  t.pwm = 86.0f;  // > threshold + 5: hard level
  ev = a.evaluate(t, 100);
  EXPECT_EQ(ev.result, AlarmResult::Fire);  // kind changed: fires immediately
  EXPECT_EQ(ev.kind, AlarmKind::PwmHard);
  EXPECT_EQ(a.evaluate(t, 250).result, AlarmResult::Active);
  EXPECT_EQ(a.evaluate(t, 300).result, AlarmResult::Fire);  // hard level repeats every 200 ms
  t.pwm = 50.0f;
  ev = a.evaluate(t, 400);
  EXPECT_EQ(ev.kind, AlarmKind::Temperature);  // PWM gone: temperature outranks speed
  EXPECT_EQ(ev.result, AlarmResult::Fire);
  t.tempMos = 30.0f;
  ev = a.evaluate(t, 500);
  EXPECT_EQ(ev.kind, AlarmKind::Speed);
  EXPECT_EQ(ev.result, AlarmResult::Fire);
}

TEST(Alarms, PwmNeverEvaluatedWithoutFieldPwm) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.pwm = 99.0f;
  t.valid &= ~static_cast<uint32_t>(F_PWM);  // a Begode: no PWM in the frame
  const AlarmEvent ev = a.evaluate(t, 0);
  EXPECT_EQ(ev.result, AlarmResult::Quiet);
  EXPECT_EQ(ev.kind, AlarmKind::None);
  // and an active PWM alarm drops the moment the field disappears
  t.mark(F_PWM);
  EXPECT_EQ(a.evaluate(t, 10).kind, AlarmKind::PwmHard);
  t.valid &= ~static_cast<uint32_t>(F_PWM);
  EXPECT_EQ(a.evaluate(t, 20).result, AlarmResult::Quiet);
}

TEST(Alarms, TemperatureUsesMaxOfAvailable) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.tempMos = 40.0f;
  t.tempMotor = 70.0f;
  t.mark(F_TEMP_MOTOR);
  EXPECT_EQ(a.evaluate(t, 0).kind, AlarmKind::Temperature);
  float m = 0.0f;
  EXPECT_TRUE(Alarms::maxTemp(t, m));
  EXPECT_FLOAT_EQ(m, 70.0f);
  Telemetry none;
  EXPECT_FALSE(Alarms::maxTemp(none, m));
  // a wheel with no temperature at all never alarms
  Alarms b(thresholds());
  EXPECT_EQ(b.evaluate(none, 0).result, AlarmResult::Quiet);
}

TEST(Alarms, CurrentUsesMagnitude) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.current = -65.0f;  // hard regen braking
  EXPECT_EQ(a.evaluate(t, 0).kind, AlarmKind::Current);
  t.current = -56.0f;  // within 5 A margin
  EXPECT_EQ(a.evaluate(t, 10).result, AlarmResult::Active);
  t.current = -50.0f;
  EXPECT_EQ(a.evaluate(t, 20).result, AlarmResult::Quiet);
}

TEST(Alarms, LowBatteryWithHysteresis) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.batteryPct = 19.0f;
  EXPECT_EQ(a.evaluate(t, 0).kind, AlarmKind::LowBattery);
  t.batteryPct = 21.0f;  // a bounce up within 2 %
  EXPECT_EQ(a.evaluate(t, 10).result, AlarmResult::Active);
  t.batteryPct = 23.0f;
  EXPECT_EQ(a.evaluate(t, 20).result, AlarmResult::Quiet);
}

TEST(Alarms, WheelBitsFireOnNewBitsAndRepeat) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.alarmBits = ALARM_SPEED1;
  t.mark(F_ALARMS);
  AlarmEvent ev = a.evaluate(t, 0);
  EXPECT_EQ(ev.kind, AlarmKind::Wheel);
  EXPECT_EQ(ev.result, AlarmResult::Fire);
  EXPECT_EQ(ev.wheelBits, ALARM_SPEED1);
  EXPECT_EQ(a.evaluate(t, 100).result, AlarmResult::Active);
  t.alarmBits = ALARM_SPEED1 | ALARM_HIGH_TEMP;  // a new bit fires at once
  ev = a.evaluate(t, 200);
  EXPECT_EQ(ev.result, AlarmResult::Fire);
  EXPECT_EQ(ev.wheelBits, ALARM_SPEED1 | ALARM_HIGH_TEMP);
  t.alarmBits = 0;
  EXPECT_EQ(a.evaluate(t, 300).result, AlarmResult::Quiet);
  // disabled: ignored
  AlarmThresholds th = thresholds();
  th.wheelAlarms = false;
  Alarms b(th);
  t.alarmBits = ALARM_TILTBACK;
  EXPECT_EQ(b.evaluate(t, 0).result, AlarmResult::Quiet);
}

TEST(Alarms, ZeroThresholdDisables) {
  AlarmThresholds th;
  th.pwmPct = 0.0f;
  th.speedKmh = 0.0f;
  th.tempC = 0.0f;
  th.currentA = 0.0f;
  th.lowBatteryPct = 0.0f;
  th.wheelAlarms = false;
  Alarms a(th);
  Telemetry t = base();
  t.pwm = 100.0f;
  t.speedKmh = 90.0f;
  t.tempMos = 120.0f;
  t.current = 300.0f;
  t.batteryPct = 1.0f;
  EXPECT_EQ(a.evaluate(t, 0).result, AlarmResult::Quiet);
}

TEST(Alarms, SlowScreenStillGetsEveryFire) {
  // The screen draws every 30 s, telemetry comes every 200 ms: evaluate() is driven by telemetry and the fire
  // decision depends only on nowMs, so a 30 s gap between evaluations still yields a Fire.
  Alarms a(thresholds());
  Telemetry t = base();
  t.speedKmh = 50.0f;
  EXPECT_EQ(a.evaluate(t, 0).result, AlarmResult::Fire);
  EXPECT_EQ(a.evaluate(t, 30000).result, AlarmResult::Fire);
  EXPECT_EQ(a.evaluate(t, 30200).result, AlarmResult::Active);
}

TEST(Alarms, MillisWrapAround) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.speedKmh = 50.0f;
  EXPECT_EQ(a.evaluate(t, 0xFFFFFF00u).result, AlarmResult::Fire);
  EXPECT_EQ(a.evaluate(t, 0xFFFFFFF0u).result, AlarmResult::Active);
  EXPECT_EQ(a.evaluate(t, 0x00000400u).result, AlarmResult::Fire);  // 1280 ms later across the wrap
}

TEST(Alarms, ResetClearsState) {
  Alarms a(thresholds());
  Telemetry t = base();
  t.speedKmh = 50.0f;
  a.evaluate(t, 0);
  a.reset();
  EXPECT_EQ(a.active(), AlarmKind::None);
  EXPECT_EQ(a.evaluate(t, 10).result, AlarmResult::Fire);
}

TEST(Alarms, Names) {
  EXPECT_STREQ(alarmKindName(AlarmKind::PwmHard), "PWM!");
  EXPECT_STREQ(alarmKindName(AlarmKind::None), "none");
  EXPECT_STREQ(alarmKindName(AlarmKind::Count), "?");
}
