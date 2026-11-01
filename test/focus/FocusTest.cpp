#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Focus.h"

using namespace focus;

namespace {

constexpr int64_t T0 = 1'760'000'000;  // some epoch in 2025
constexpr int64_t MIN = 60;

Config defaults() { return Config{}; }

}  // namespace

TEST(FocusConfig, ClampsEveryField) {
  Config c;
  c.focusMin = 1;
  c.shortMin = 0;
  c.longMin = 200;
  c.cyclesBeforeLong = 1;
  const Config k = clampConfig(c);
  EXPECT_EQ(k.focusMin, 5);
  EXPECT_EQ(k.shortMin, 1);
  EXPECT_EQ(k.longMin, 60);
  EXPECT_EQ(k.cyclesBeforeLong, 2);
  c.focusMin = 120;
  c.shortMin = 31;
  c.longMin = 4;
  c.cyclesBeforeLong = 9;
  const Config k2 = clampConfig(c);
  EXPECT_EQ(k2.focusMin, 90);
  EXPECT_EQ(k2.shortMin, 30);
  EXPECT_EQ(k2.longMin, 5);
  EXPECT_EQ(k2.cyclesBeforeLong, 8);
  EXPECT_EQ(clampConfig(defaults()).focusMin, 25);
}

TEST(FocusPhase, KeysRoundTrip) {
  for (Phase p : {Phase::Idle, Phase::Focus, Phase::ShortBreak, Phase::LongBreak}) {
    Phase back = Phase::Idle;
    ASSERT_TRUE(phaseFromKey(phaseKey(p), back));
    EXPECT_EQ(back, p);
  }
  Phase junk = Phase::Idle;
  EXPECT_FALSE(phaseFromKey("nap", junk));
}

TEST(FocusTimer, FreshTimerIsIdle) {
  const Timer t;
  EXPECT_EQ(t.phase(), Phase::Idle);
  EXPECT_EQ(t.nextPhase(), Phase::Focus);
  EXPECT_FALSE(t.running());
  EXPECT_FALSE(t.paused());
  EXPECT_EQ(t.remainingSec(T0), 0u);
  EXPECT_EQ(t.elapsedSec(T0), 0u);
  EXPECT_EQ(t.cycleIndex(), 0);
}

TEST(FocusTimer, FullCycleOfFourFocusesEndsInLongBreak) {
  Timer t(defaults());
  int64_t now = T0;
  for (int i = 0; i < 4; ++i) {
    ASSERT_EQ(t.nextPhase(), Phase::Focus) << "focus " << i;
    t.start(t.nextPhase(), now);
    EXPECT_TRUE(t.running());
    EXPECT_EQ(t.remainingSec(now), 25u * MIN);
    EXPECT_EQ(t.update(now + 25 * MIN - 1), Event::None);
    EXPECT_EQ(t.update(now + 25 * MIN), Event::PhaseEnded);
    EXPECT_EQ(t.phase(), Phase::Idle);
    EXPECT_EQ(t.focusDoneToday(), i + 1);
    EXPECT_EQ(t.focusDoneTotal(), static_cast<uint32_t>(i + 1));
    now += 25 * MIN;
    const Phase expectedBreak = i == 3 ? Phase::LongBreak : Phase::ShortBreak;
    ASSERT_EQ(t.nextPhase(), expectedBreak) << "after focus " << i;
    EXPECT_EQ(t.cycleIndex(), i + 1);
    t.start(t.nextPhase(), now);
    const int64_t len = (expectedBreak == Phase::LongBreak ? 15 : 5) * MIN;
    EXPECT_EQ(t.remainingSec(now), static_cast<uint32_t>(len));
    EXPECT_EQ(t.update(now + len), Event::PhaseEnded);
    now += len;
  }
  EXPECT_EQ(t.nextPhase(), Phase::Focus);
  EXPECT_EQ(t.cycleIndex(), 0);
  EXPECT_EQ(t.focusDoneTotal(), 4u);
}

TEST(FocusTimer, PauseResumeShiftsEndExactly) {
  Timer t(defaults());
  t.start(Phase::Focus, T0);
  EXPECT_EQ(t.elapsedSec(T0 + 10 * MIN), 10u * MIN);
  t.pause(T0 + 10 * MIN);
  EXPECT_TRUE(t.paused());
  EXPECT_FALSE(t.running());
  // Time passes while paused: remaining is frozen, update does nothing.
  EXPECT_EQ(t.remainingSec(T0 + 30 * MIN), 15u * MIN);
  EXPECT_EQ(t.elapsedSec(T0 + 30 * MIN), 10u * MIN);
  EXPECT_EQ(t.update(T0 + 60 * MIN), Event::None);
  // Pause/resume while not in the right state are no-ops.
  t.pause(T0 + 31 * MIN);
  t.resume(T0 + 33 * MIN);
  EXPECT_TRUE(t.running());
  EXPECT_EQ(t.remainingSec(T0 + 33 * MIN), 15u * MIN);
  t.resume(T0 + 34 * MIN);
  EXPECT_EQ(t.remainingSec(T0 + 34 * MIN), 14u * MIN);
  EXPECT_EQ(t.update(T0 + 48 * MIN - 1), Event::None);
  EXPECT_EQ(t.update(T0 + 48 * MIN), Event::PhaseEnded);
  EXPECT_EQ(t.focusDoneTotal(), 1u);
}

TEST(FocusTimer, SkipEndsNowAndCountsFocus) {
  Timer t(defaults());
  EXPECT_EQ(t.skip(T0), Event::None);
  t.start(Phase::Focus, T0);
  EXPECT_EQ(t.skip(T0 + 60), Event::PhaseEnded);
  EXPECT_EQ(t.phase(), Phase::Idle);
  EXPECT_EQ(t.focusDoneToday(), 1);
  EXPECT_EQ(t.nextPhase(), Phase::ShortBreak);
  t.start(Phase::ShortBreak, T0 + 60);
  t.pause(T0 + 90);
  EXPECT_EQ(t.skip(T0 + 100), Event::PhaseEnded);  // skipping while paused works too
  EXPECT_EQ(t.nextPhase(), Phase::Focus);
  EXPECT_EQ(t.focusDoneToday(), 1);  // a break does not count
}

TEST(FocusTimer, StopKeepsCountersAndOffersAbortedPhase) {
  Timer t(defaults());
  t.start(Phase::Focus, T0);
  t.skip(T0 + 1);
  t.start(Phase::ShortBreak, T0 + 1);
  t.stop();
  EXPECT_EQ(t.phase(), Phase::Idle);
  EXPECT_EQ(t.nextPhase(), Phase::ShortBreak);
  EXPECT_EQ(t.focusDoneTotal(), 1u);
}

TEST(FocusTimer, AutoNextStartsFollowingPhaseAtOnce) {
  Config c = defaults();
  c.autoNext = true;
  Timer t(c);
  t.start(Phase::Focus, T0);
  EXPECT_EQ(t.update(T0 + 25 * MIN), Event::PhaseEnded);
  EXPECT_EQ(t.phase(), Phase::ShortBreak);
  EXPECT_TRUE(t.running());
  EXPECT_EQ(t.remainingSec(T0 + 25 * MIN), 5u * MIN);
  EXPECT_EQ(t.update(T0 + 30 * MIN), Event::PhaseEnded);
  EXPECT_EQ(t.phase(), Phase::Focus);
  EXPECT_EQ(t.update(T0 + 30 * MIN + 1), Event::None);
}

TEST(FocusTimer, HoursLongJumpEndsOnlyOnePhase) {
  Timer t(defaults());
  t.start(Phase::Focus, T0);
  EXPECT_EQ(t.update(T0 + 6 * 3600), Event::PhaseEnded);
  EXPECT_EQ(t.focusDoneTotal(), 1u);
  EXPECT_EQ(t.phase(), Phase::Idle);
  EXPECT_EQ(t.update(T0 + 7 * 3600), Event::None);
  EXPECT_EQ(t.focusDoneTotal(), 1u);

  Config c = defaults();
  c.autoNext = true;
  Timer a(c);
  a.start(Phase::Focus, T0);
  EXPECT_EQ(a.update(T0 + 6 * 3600), Event::PhaseEnded);
  EXPECT_EQ(a.phase(), Phase::ShortBreak);
  // The break began at the wake-up time, not six hours ago.
  EXPECT_EQ(a.remainingSec(T0 + 6 * 3600), 5u * MIN);
  EXPECT_EQ(a.update(T0 + 6 * 3600 + 1), Event::None);
  EXPECT_EQ(a.focusDoneTotal(), 1u);
}

TEST(FocusTimer, BackwardsAndNegativeTimeNeverBreak) {
  Timer t(defaults());
  t.start(Phase::Focus, T0);
  EXPECT_EQ(t.update(T0 - 100'000), Event::None);
  EXPECT_EQ(t.remainingSec(T0 - 100'000), 25u * MIN);
  EXPECT_EQ(t.elapsedSec(T0 - 100'000), 0u);
  EXPECT_EQ(t.remainingSec(-5), 25u * MIN);
  t.pause(T0 - 50);  // clamped to the start
  EXPECT_EQ(t.remainingSec(T0), 25u * MIN);
  t.resume(T0 - 200);  // earlier than the pause: no shift
  EXPECT_EQ(t.remainingSec(T0), 25u * MIN);
  EXPECT_EQ(t.remainingSec(T0 + 3600), 0u);
  EXPECT_EQ(t.elapsedSec(T0 + 3600), 25u * MIN);
  Timer n(defaults());
  n.start(Phase::Focus, -1'000'000);
  EXPECT_EQ(n.update(-1'000'000 + 25 * MIN), Event::PhaseEnded);
}

TEST(FocusTimer, SerializeRoundTripRunning) {
  Config c = defaults();
  c.focusMin = 30;
  c.autoNext = true;
  Timer t(c);
  t.start(Phase::Focus, T0);
  t.skip(T0 + 1);
  t.start(Phase::ShortBreak, T0 + 1);
  char buf[Timer::SAVE_CAPACITY];
  const size_t n = t.serialize(buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  EXPECT_EQ(std::strlen(buf), n);
  EXPECT_NE(std::strstr(buf, "v=1\n"), nullptr);
  EXPECT_NE(std::strstr(buf, "phase=short\n"), nullptr);

  Timer back;
  ASSERT_TRUE(back.deserialize(buf, n));
  EXPECT_EQ(back.phase(), Phase::ShortBreak);
  EXPECT_TRUE(back.running());
  EXPECT_EQ(back.remainingSec(T0 + 61), 5u * MIN - 60);
  EXPECT_EQ(back.focusDoneToday(), 1);
  EXPECT_EQ(back.focusDoneTotal(), 1u);
  EXPECT_EQ(back.cycleIndex(), 1);
  EXPECT_EQ(back.config().focusMin, 30);
  EXPECT_TRUE(back.config().autoNext);
  // The restored timer keeps ticking where the old one would have.
  EXPECT_EQ(back.update(T0 + 1 + 5 * MIN), Event::PhaseEnded);
  EXPECT_EQ(back.phase(), Phase::Focus);
}

TEST(FocusTimer, SerializeRoundTripPaused) {
  Timer t(defaults());
  t.start(Phase::Focus, T0);
  t.pause(T0 + 120);
  char buf[Timer::SAVE_CAPACITY];
  const size_t n = t.serialize(buf, sizeof(buf));
  ASSERT_GT(n, 0u);
  Timer back;
  ASSERT_TRUE(back.deserialize(buf, n));
  EXPECT_TRUE(back.paused());
  EXPECT_EQ(back.remainingSec(T0 + 99'999), 25u * MIN - 120);
  back.resume(T0 + 1000);
  EXPECT_EQ(back.remainingSec(T0 + 1000), 25u * MIN - 120);
}

TEST(FocusTimer, SerializeTooSmallBufferReturnsZero) {
  const Timer t;
  char tiny[8];
  EXPECT_EQ(t.serialize(tiny, sizeof(tiny)), 0u);
  EXPECT_EQ(tiny[0], '\0');
  EXPECT_EQ(t.serialize(nullptr, 0), 0u);
}

TEST(FocusTimer, DeserializeToleratesGarbage) {
  Timer t;
  const std::string messy =
      "junk line without equals\n"
      "v=1\r\n"
      "=novalue\n"
      "phase=focus\n"
      "next=zzz\n"
      "paused=abc\n"
      "start=" +
      std::to_string(T0) +
      "\n"
      "end=" +
      std::to_string(T0 + 1500) +
      "\n"
      "total=12\n"
      "today=-7\n"
      "cycle=99\n"
      "mystery=42\n"
      "focusMin=250\n";
  ASSERT_TRUE(t.deserialize(messy.c_str(), messy.size()));
  EXPECT_EQ(t.phase(), Phase::Focus);
  EXPECT_TRUE(t.running());
  EXPECT_EQ(t.focusDoneTotal(), 12u);
  EXPECT_EQ(t.focusDoneToday(), 0);
  EXPECT_EQ(t.cycleIndex(), 0);
  EXPECT_EQ(t.config().focusMin, 90);
  EXPECT_EQ(t.remainingSec(T0), 1500u);
}

TEST(FocusTimer, DeserializeRejectsBadVersionAndImpossibleState) {
  Timer t;
  t.start(Phase::Focus, T0);
  EXPECT_FALSE(t.deserialize("v=7\nphase=focus\n", 16));
  EXPECT_EQ(t.phase(), Phase::Idle);

  const char* reversed = "v=1\nphase=focus\nstart=100\nend=50\ntotal=3\n";
  EXPECT_FALSE(t.deserialize(reversed, std::strlen(reversed)));
  EXPECT_EQ(t.phase(), Phase::Idle);
  EXPECT_EQ(t.focusDoneTotal(), 3u);  // counters survive even then

  const char* binary = "\x00\xff\xfe=\n\n\n";
  EXPECT_FALSE(t.deserialize(binary, 7));
  EXPECT_FALSE(t.deserialize(nullptr, 0));
  EXPECT_FALSE(t.deserialize("", 0));
  EXPECT_EQ(t.phase(), Phase::Idle);
}

TEST(FocusLog, FormatLogLine) {
  char buf[64];
  const size_t n = formatLogLine(buf, sizeof(buf), 1'700'000'000, Phase::Focus, 25, true);
  EXPECT_EQ(std::string(buf, n), "1700000000\tfocus\t25\t1\n");
  const size_t m = formatLogLine(buf, sizeof(buf), -5, Phase::LongBreak, 15, false);
  EXPECT_EQ(std::string(buf, m), "-5\tlong\t15\t0\n");
  char tiny[4];
  EXPECT_EQ(formatLogLine(tiny, sizeof(tiny), 1, Phase::Idle, 0, false), 0u);
}
