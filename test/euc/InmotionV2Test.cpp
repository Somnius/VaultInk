#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Euc.h"
#include "InmotionV2.h"
#include "Protocols.h"

using namespace euc;
using namespace euc::inmotionv2;

namespace {

using Bytes = std::vector<uint8_t>;

struct FakeLink : Link {
  std::vector<Bytes> writes;
  bool write(const uint8_t* d, const size_t n) override {
    writes.emplace_back(d, d + n);
    return true;
  }
};

// Hands every pending simulator frame to the protocol in `chunk`-byte notifications.
bool deliver(WheelSimulator& sim, Protocol& p, Telemetry& t, const size_t chunk = 20) {
  uint8_t buf[512];
  size_t n = 0;
  bool changed = false;
  while (sim.pop(buf, sizeof buf, n)) {
    for (size_t o = 0; o < n; o += chunk) {
      changed = p.feed(buf + o, std::min(chunk, n - o), t) || changed;
    }
  }
  return changed;
}

void pump(FakeLink& link, WheelSimulator& sim) {
  for (const Bytes& w : link.writes) {
    sim.onWrite(w.data(), w.size());
  }
  link.writes.clear();
}

Bytes frame(const uint8_t flag, const uint8_t cmd, const Bytes& data) {
  uint8_t out[2 * kMaxBody + 3];
  const size_t n = buildFrame(flag, cmd, data.data(), data.size(), out, sizeof out);
  return Bytes(out, out + n);
}

Telemetry sample(const float speed) {
  Telemetry t;
  t.voltage = 84.37f;
  t.current = -12.5f;
  t.speedKmh = speed;
  t.pwm = 42.5f;
  t.power = -1050;
  t.batteryPct = 73.5f;
  t.tempMos = 41;
  t.tempMotor = 55;
  t.tempBattery = 22;
  t.tempBoard = 38;
  t.pitch = -1.25f;
  t.roll = 2.5f;
  t.tripKm = 12.34f;
  t.rangeKm = 45.6f;
  t.totalKm = 1234.56f;
  t.rideSeconds = 3700;
  t.charging = false;
  t.lightMode = 1;
  for (const Field f :
       {F_VOLTAGE, F_CURRENT, F_SPEED, F_PWM, F_POWER, F_BATTERY, F_TEMP_MOS, F_TEMP_MOTOR, F_TEMP_BATTERY,
        F_TEMP_BOARD, F_PITCH, F_ROLL, F_TRIP, F_RANGE, F_TOTAL, F_RIDE_TIME, F_LIGHT}) {
    t.mark(f);
  }
  return t;
}

// Runs the connection sequence (type, serial, version, settings, useless, stats) and leaves the protocol in the
// live loop with the simulator's first live and stats replies decoded. Returns the time reached.
uint32_t connect(Protocol& p, InmotionV2Sim& sim, FakeLink& link, Telemetry& t, const Telemetry& wheel) {
  uint32_t now = 1000;
  sim.tick(now, wheel);
  p.start(link, now);
  for (int i = 0; i < 12; ++i) {
    pump(link, sim);
    deliver(sim, p, t);
    now += 260;
    p.poll(link, now);
  }
  pump(link, sim);
  deliver(sim, p, t);
  return now;
}

}  // namespace

TEST(InmotionV2Protocol, RequestBytesMatchEucWatchConstants) {
  // eucInmotionV2.js:21-27.
  EXPECT_EQ(frame(kFlagData, kCmdLive, {}), Bytes({0xAA, 0xAA, 0x14, 0x01, 0x04, 0x11}));
  EXPECT_EQ(frame(kFlagData, kCmdStats, {}), Bytes({0xAA, 0xAA, 0x14, 0x01, 0x11, 0x04}));
  EXPECT_EQ(frame(kFlagData, kCmdSettings, {0x20}), Bytes({0xAA, 0xAA, 0x14, 0x02, 0x20, 0x20, 0x16}));
  EXPECT_EQ(frame(kFlagInfo, kCmdMainInfo, {0x06}), Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x06, 0x17}));
  EXPECT_EQ(frame(kFlagInfo, kCmdMainInfo, {0x02}), Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x02, 0x13}));
  EXPECT_EQ(frame(kFlagInfo, kCmdMainInfo, {0x01}), Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x01, 0x10}));
  EXPECT_EQ(frame(kFlagData, kCmdSomething1, {0x00, 0x01}), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x10, 0x00, 0x01, 0x06}));
  // :28-31 drl / lights.
  EXPECT_EQ(frame(kFlagData, kCmdControl, {0x2D, 0x01}), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x2D, 0x01, 0x5B}));
  EXPECT_EQ(frame(kFlagData, kCmdControl, {0x2D, 0x00}), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x2D, 0x00, 0x5A}));
  EXPECT_EQ(frame(kFlagData, kCmdControl, {0x50, 0x01}), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x50, 0x01, 0x26}));
  EXPECT_EQ(frame(kFlagData, kCmdControl, {0x50, 0x00}), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x50, 0x00, 0x27}));
}

TEST(InmotionV2Protocol, EscapesAaAndA5InsideTheBody) {
  // REF:2394-2403: 0xAA and 0xA5 in the body get a 0xA5 in front; the check is over the unescaped body.
  const Bytes f = frame(kFlagData, kCmdControl, {0xAA, 0xA5});
  EXPECT_EQ(f, Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0xA5, 0xAA, 0xA5, 0xA5,
                      static_cast<uint8_t>(0x14 ^ 0x03 ^ 0x60 ^ 0xAA ^ 0xA5)}));
  Unpacker u;
  bool done = false;
  for (const uint8_t b : f) {
    done = u.push(b);
  }
  ASSERT_TRUE(done);
  EXPECT_EQ(u.dataLen(), 2u);
  EXPECT_EQ(u.data()[0], 0xAA);
  EXPECT_EQ(u.data()[1], 0xA5);
}

TEST(InmotionV2Protocol, RealTypeRepliesFromWheelLogComments) {
  // REF:559-561: "020601010100 -v11", "020701010100 -v12", "020801010100 -v13" are the type reply bytes after the
  // 0x01 sub-command. Frame: AA AA 11 08 02 01 <those> <xor>.
  struct Row {
    uint8_t series;
    const char* model;
  };
  for (const Row r : {Row{0x06, "Inmotion V11"}, Row{0x07, "Inmotion V12 HS"}, Row{0x08, "Inmotion V13"}}) {
    Bytes real = {0xAA, 0xAA, 0x11, 0x08, 0x02, 0x01, 0x02, r.series, 0x01, 0x01, 0x01, 0x00};
    uint8_t x = 0;
    for (size_t i = 2; i < real.size(); ++i) {
      x = static_cast<uint8_t>(x ^ real[i]);
    }
    real.push_back(x);
    if (r.series == 0x06) {
      EXPECT_EQ(x, 0x1F);
    }
    auto p = makeInmotionV2();
    Telemetry t;
    EXPECT_TRUE(p->feed(real.data(), real.size(), t));
    EXPECT_TRUE(t.has(F_MODEL));
    EXPECT_STREQ(t.model, r.model);
  }
}

TEST(InmotionV2Protocol, ConnectionSequenceThenLiveLoop) {
  auto p = makeInmotionV2();
  InmotionV2Sim sim;
  FakeLink link;
  Telemetry t;
  uint32_t now = 1000;
  sim.tick(now, sample(25.0f));
  p->start(link, now);
  ASSERT_EQ(link.writes.size(), 1u);
  EXPECT_EQ(link.writes[0], Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x01, 0x10}));  // type
  pump(link, sim);
  EXPECT_TRUE(deliver(sim, *p, t));
  EXPECT_STREQ(t.model, "Inmotion V11");
  // Serial is asked for as soon as the type is known.
  p->poll(link, now + 1);
  ASSERT_EQ(link.writes.size(), 1u);
  EXPECT_EQ(link.writes[0], Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x02, 0x13}));
  pump(link, sim);
  deliver(sim, *p, t);
  EXPECT_STREQ(t.serial, "2309A1B2C3D4E5F6");
  p->poll(link, now + 2);
  ASSERT_EQ(link.writes.size(), 1u);
  EXPECT_EQ(link.writes[0], Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x06, 0x17}));
  pump(link, sim);
  deliver(sim, *p, t);
  EXPECT_STREQ(t.firmware, "1.4.0");
  // settings, useless, stats, then live every 200 ms.
  const Bytes expected[] = {{0xAA, 0xAA, 0x14, 0x02, 0x20, 0x20, 0x16},
                            {0xAA, 0xAA, 0x14, 0x03, 0x10, 0x00, 0x01, 0x06},
                            {0xAA, 0xAA, 0x14, 0x01, 0x11, 0x04},
                            {0xAA, 0xAA, 0x14, 0x01, 0x04, 0x11},
                            {0xAA, 0xAA, 0x14, 0x01, 0x04, 0x11}};
  now += 3;
  for (const Bytes& e : expected) {
    p->poll(link, now);
    ASSERT_EQ(link.writes.size(), 1u);
    EXPECT_EQ(link.writes[0], e);
    pump(link, sim);
    deliver(sim, *p, t);
    p->poll(link, now + 100);  // not due yet
    EXPECT_TRUE(link.writes.empty());
    now += 200;
  }
  EXPECT_TRUE(t.has(F_TOTAL));
  EXPECT_NEAR(t.totalKm, 1234.56f, 0.011f);
  EXPECT_EQ(t.rideSeconds, 3700u);
  EXPECT_TRUE(t.has(F_SPEED));
  EXPECT_NEAR(t.speedKmh, 25.0f, 0.011f);
}

TEST(InmotionV2Protocol, StatsEvery48thLiveTick) {
  auto p = makeInmotionV2();
  InmotionV2Sim sim;
  FakeLink link;
  Telemetry t;
  uint32_t now = connect(*p, sim, link, t, sample(10.0f));
  const Bytes stats = {0xAA, 0xAA, 0x14, 0x01, 0x11, 0x04};
  const Bytes live = {0xAA, 0xAA, 0x14, 0x01, 0x04, 0x11};
  int statsCount = 0;
  int liveCount = 0;
  for (int i = 0; i < 96; ++i) {
    now += 200;
    p->poll(link, now);
    for (const Bytes& w : link.writes) {
      statsCount += (w == stats) ? 1 : 0;
      liveCount += (w == live) ? 1 : 0;
    }
    pump(link, sim);
    deliver(sim, *p, t);
  }
  EXPECT_EQ(statsCount, 2);
  EXPECT_EQ(liveCount, 94);
}

TEST(InmotionV2Protocol, RetriesTypeRequestEvery250msWhenUnanswered) {
  auto p = makeInmotionV2();
  FakeLink link;
  p->start(link, 1000);
  link.writes.clear();
  p->poll(link, 1200);
  EXPECT_TRUE(link.writes.empty());
  p->poll(link, 1260);
  ASSERT_EQ(link.writes.size(), 1u);
  EXPECT_EQ(link.writes[0], Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x01, 0x10}));
}

struct LayoutCase {
  uint8_t series;
  uint8_t type;
  uint8_t fw1;
  uint8_t fw2;
  const char* model;
  bool hasPitch;  // pitch/roll/trip/range only on the V11/V12 layouts
};

class InmotionV2Layouts : public ::testing::TestWithParam<LayoutCase> {};

TEST_P(InmotionV2Layouts, RoundTripSeveralValues) {
  const LayoutCase c = GetParam();
  for (const float speed : {0.0f, 7.25f, 33.3f, 59.99f}) {
    auto p = makeInmotionV2();
    InmotionV2Sim sim;
    sim.setModel(c.series, c.type, c.fw1, c.fw2, 7);
    FakeLink link;
    Telemetry t;
    Telemetry wheel = sample(speed);
    wheel.charging = speed == 0.0f;
    connect(*p, sim, link, t, wheel);
    EXPECT_STREQ(t.model, c.model);
    ASSERT_TRUE(t.has(F_SPEED)) << c.model;
    EXPECT_NEAR(t.speedKmh, speed, 0.011f);
    EXPECT_NEAR(t.voltage, 84.37f, 0.011f);
    EXPECT_NEAR(t.current, -12.5f, 0.011f);
    EXPECT_NEAR(t.pwm, 42.5f, 0.011f);
    EXPECT_NEAR(t.power, -1050.0f, 0.5f);
    EXPECT_NEAR(t.batteryPct, 73.5f, (c.fw1 < 2 && c.fw2 < 4) ? 0.51f : 0.011f);
    EXPECT_NEAR(t.tempMos, 41.0f, 0.1f);
    EXPECT_NEAR(t.tempMotor, 55.0f, 0.1f);
    EXPECT_NEAR(t.tempBattery, 22.0f, 0.1f);
    EXPECT_NEAR(t.tempBoard, 38.0f, 0.1f);
    EXPECT_EQ(t.charging, speed == 0.0f);
    EXPECT_EQ(t.lightMode, 1);
    EXPECT_TRUE(t.has(F_ALARMS));
    EXPECT_EQ(t.alarmBits, 0);
    if (c.hasPitch) {
      EXPECT_NEAR(t.pitch, -1.25f, 0.011f);
      EXPECT_NEAR(t.roll, 2.5f, 0.011f);
      EXPECT_NEAR(t.tripKm, 12.34f, 0.011f);
      EXPECT_NEAR(t.rangeKm, 45.6f, 0.011f);
    } else {
      EXPECT_FALSE(t.has(F_PITCH));
      EXPECT_FALSE(t.has(F_TRIP));
    }
  }
}

INSTANTIATE_TEST_SUITE_P(Models, InmotionV2Layouts,
                         ::testing::Values(LayoutCase{6, 1, 1, 4, "Inmotion V11", true},
                                           LayoutCase{6, 1, 1, 3, "Inmotion V11", true},  // < 1.4 layout
                                           LayoutCase{7, 1, 2, 0, "Inmotion V12 HS", true},
                                           LayoutCase{7, 3, 2, 0, "Inmotion V12 PRO", true},
                                           LayoutCase{8, 1, 1, 0, "Inmotion V13", false},
                                           LayoutCase{9, 2, 1, 0, "Inmotion V14 50S", false},
                                           LayoutCase{6, 2, 1, 0, "Inmotion V11y", false}));

TEST(InmotionV2Protocol, LiveFrameSplitAtEveryByteBoundary) {
  auto p = makeInmotionV2();
  InmotionV2Sim sim;
  FakeLink link;
  Telemetry t;
  connect(*p, sim, link, t, sample(18.5f));
  // Ask for one more live frame and keep its bytes.
  const Bytes live = {0xAA, 0xAA, 0x14, 0x01, 0x04, 0x11};
  Telemetry wheel = sample(31.75f);
  wheel.voltage = 424.10f;  // 42410 = 0xA5AA: both bytes escaped on the wire
  sim.tick(0, wheel);
  sim.onWrite(live.data(), live.size());
  uint8_t buf[512];
  size_t n = 0;
  ASSERT_TRUE(sim.pop(buf, sizeof buf, n));
  for (size_t split = 1; split < n; ++split) {
    Telemetry u;
    u.valid = 0;
    auto q = makeInmotionV2();
    // The decoder needs the model: replay the type reply first.
    const Bytes type = frame(kFlagInfo, kCmdMainInfo, {0x01, 0x02, 0x06, 0x01, 0x01, 0x01, 0x00});
    const Bytes ver = [] {
      Bytes d(25, 0);
      d[0] = 0x06;
      d[13] = 4;
      d[14] = 1;
      return frame(kFlagInfo, kCmdMainInfo, d);
    }();
    q->feed(type.data(), type.size(), u);
    q->feed(ver.data(), ver.size(), u);
    EXPECT_FALSE(q->feed(buf, split, u));
    EXPECT_TRUE(q->feed(buf + split, n - split, u)) << "split " << split;
    EXPECT_NEAR(u.speedKmh, 31.75f, 0.011f);
    EXPECT_NEAR(u.voltage, 424.10f, 0.011f);
  }
}

TEST(InmotionV2Protocol, GarbageTruncatedAndBadChecksumAreDroppedWithoutLosingTheNextFrame) {
  auto p = makeInmotionV2();
  Telemetry t;
  const Bytes type = frame(kFlagInfo, kCmdMainInfo, {0x01, 0x02, 0x07, 0x01, 0x01, 0x01, 0x00});
  const Bytes garbage = {0x00, 0xAA, 0x12, 0xA5, 0xA5, 0xAA, 0xFF, 0x55, 0x55, 0xAA};
  EXPECT_FALSE(p->feed(garbage.data(), garbage.size(), t));
  // A truncated frame (header + half the body) followed by a whole one: the whole one is decoded.
  EXPECT_FALSE(p->feed(type.data(), 6, t));
  EXPECT_TRUE(p->feed(type.data(), type.size(), t));
  EXPECT_STREQ(t.model, "Inmotion V12 HS");
  // Bad checksum: rejected, the model stays.
  Bytes bad = frame(kFlagInfo, kCmdMainInfo, {0x01, 0x02, 0x08, 0x01, 0x01, 0x01, 0x00});
  bad.back() = static_cast<uint8_t>(bad.back() ^ 0x01);
  EXPECT_FALSE(p->feed(bad.data(), bad.size(), t));
  EXPECT_STREQ(t.model, "Inmotion V12 HS");
  // A 0-length frame is dropped (eucInmotionV2.js:408).
  const Bytes empty = {0xAA, 0xAA, 0x14, 0x00, 0x14};
  EXPECT_FALSE(p->feed(empty.data(), empty.size(), t));
  // Long garbage never overflows.
  Bytes longGarbage(2000, 0xAA);
  longGarbage[3] = 0xFF;
  EXPECT_FALSE(p->feed(longGarbage.data(), longGarbage.size(), t));
  Bytes bigLen = {0xAA, 0xAA, 0x14, 0xFF};
  bigLen.insert(bigLen.end(), 300, 0x11);
  EXPECT_FALSE(p->feed(bigLen.data(), bigLen.size(), t));
}

TEST(InmotionV2Protocol, ErrorBitsMapToAlarms) {
  // V11 1.4 layout: error bytes start at data[61] (REF:1260); [61+3] bit0 under voltage, bit6 MOS temp;
  // [61+4] bit5 motor blocked; state byte 57 bit2 lifted.
  auto p = makeInmotionV2();
  Telemetry t;
  const Bytes type = frame(kFlagInfo, kCmdMainInfo, {0x01, 0x02, 0x06, 0x01, 0x01, 0x01, 0x00});
  Bytes ver(25, 0);
  ver[0] = 0x06;
  ver[13] = 4;
  ver[14] = 1;
  const Bytes verF = frame(kFlagInfo, kCmdMainInfo, ver);
  p->feed(type.data(), type.size(), t);
  p->feed(verF.data(), verF.size(), t);
  Bytes live(68, 0);
  live[57] = 0x04;
  live[64] = 0x41;
  live[65] = 0x20;
  const Bytes f = frame(kFlagData, kCmdLive, live);
  ASSERT_TRUE(p->feed(f.data(), f.size(), t));
  EXPECT_EQ(t.alarmBits, ALARM_LIFTED | ALARM_LOW_VOLTAGE | ALARM_HIGH_TEMP | ALARM_MOTOR_BLOCKED);
  // Zero temperature bytes mean "no sensor" and are not reported.
  EXPECT_FALSE(t.has(F_TEMP_MOS));
}

TEST(InmotionV2Protocol, HandFrameOffsetsMatchWheelLog) {
  // Independent of the simulator: V11 1.4 puts the MOS temperature at data[42], the state at 56 and the
  // battery at 28 (REF:1196-1239); V12 at 40, 54 and 24 (REF:1288-1329).
  struct Row {
    uint8_t series;
    size_t temp;
    size_t state;
    size_t battery;
    size_t len;
  };
  for (const Row r : {Row{6, 42, 56, 28, 68}, Row{7, 40, 54, 24, 66}}) {
    auto p = makeInmotionV2();
    Telemetry t;
    const Bytes type = frame(kFlagInfo, kCmdMainInfo, {0x01, 0x02, r.series, 0x01, 0x01, 0x01, 0x00});
    Bytes ver(25, 0);
    ver[0] = 0x06;
    ver[13] = 4;
    ver[14] = 1;
    const Bytes verF = frame(kFlagInfo, kCmdMainInfo, ver);
    p->feed(type.data(), type.size(), t);
    p->feed(verF.data(), verF.size(), t);
    Bytes live(r.len, 0);
    live[r.temp] = 221;  // 221 + 80 - 256 = 45 C
    live[r.state] = 0x80;
    live[r.battery] = 0x1C;  // 0x1C1C = 7196 -> 71.96 %
    live[r.battery + 1] = 0x1C;
    const Bytes f = frame(kFlagData, kCmdLive, live);
    ASSERT_TRUE(p->feed(f.data(), f.size(), t));
    EXPECT_FLOAT_EQ(t.tempMos, 45.0f);
    EXPECT_TRUE(t.charging);
    EXPECT_NEAR(t.batteryPct, 71.96f, 0.001f);
  }
}

TEST(InmotionV2Protocol, CommandsEncodeExactBytes) {
  auto p = makeInmotionV2();
  InmotionV2Sim sim;
  FakeLink link;
  Telemetry t;
  connect(*p, sim, link, t, sample(0.0f));
  link.writes.clear();

  EXPECT_TRUE(p->send(Command::RideLeds, 1, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x2D, 0x01, 0x5B}));
  EXPECT_TRUE(p->send(Command::RideLeds, 0, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x2D, 0x00, 0x5A}));
  EXPECT_TRUE(p->send(Command::Lights, 1, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x50, 0x01, 0x26}));
  EXPECT_TRUE(p->send(Command::Lights, 0, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x50, 0x00, 0x27}));
  // setVolume (eucInmotionV2.js:71-73): 14 03 60 26 <vol> <xor>.
  EXPECT_TRUE(p->send(Command::Volume, 50, link));
  EXPECT_EQ(link.writes.back(),
            Bytes({0xAA, 0xAA, 0x14, 0x03, 0x60, 0x26, 50, static_cast<uint8_t>(0x14 ^ 0x03 ^ 0x60 ^ 0x26 ^ 50)}));
  // playSound (REF:2025-2036): 60 51 <sound> 01; value 0 = WheelLog's default 0x18.
  EXPECT_TRUE(p->send(Command::Horn, 0, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x04, 0x60, 0x51, 0x18, 0x01,
                                       static_cast<uint8_t>(0x14 ^ 0x04 ^ 0x60 ^ 0x51 ^ 0x18 ^ 0x01)}));
  EXPECT_TRUE(p->send(Command::Horn, 5, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x04, 0x60, 0x51, 0x05, 0x01,
                                       static_cast<uint8_t>(0x14 ^ 0x04 ^ 0x60 ^ 0x51 ^ 0x05 ^ 0x01)}));
  EXPECT_TRUE(p->send(Command::Beep, 0, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x04, 0x60, 0x51, 0x18, 0x01,
                                       static_cast<uint8_t>(0x14 ^ 0x04 ^ 0x60 ^ 0x51 ^ 0x18 ^ 0x01)}));
  // The simulator saw the light switch.
  pump(link, sim);
  EXPECT_FALSE(sim.lightOn());
  EXPECT_TRUE(p->send(Command::Lights, 1, link));
  pump(link, sim);
  EXPECT_TRUE(sim.lightOn());
  // RequestInfo restarts the type/serial/version sequence.
  EXPECT_TRUE(p->send(Command::RequestInfo, 0, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x11, 0x02, 0x02, 0x01, 0x10}));
}

TEST(InmotionV2Protocol, V12HeadlightCarriesLowAndHighBeam) {
  auto p = makeInmotionV2();
  InmotionV2Sim sim;
  sim.setModel(7, 1, 2, 0, 0);
  FakeLink link;
  Telemetry t;
  connect(*p, sim, link, t, sample(0.0f));
  link.writes.clear();
  EXPECT_TRUE(p->send(Command::Lights, 1, link));
  EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x04, 0x60, 0x50, 0x01, 0x00,
                                       static_cast<uint8_t>(0x14 ^ 0x04 ^ 0x60 ^ 0x50 ^ 0x01)}));
}

TEST(InmotionV2Protocol, OldV11UsesTheOlderControlCodesAndV13Beeps) {
  {
    auto p = makeInmotionV2();
    InmotionV2Sim sim;
    sim.setModel(6, 1, 1, 3, 0);  // main board 1.3: REF:2028-2030 and 2060-2062 use 0x41/0x40
    FakeLink link;
    Telemetry t;
    connect(*p, sim, link, t, sample(0.0f));
    link.writes.clear();
    EXPECT_TRUE(p->send(Command::Lights, 1, link));
    EXPECT_EQ(link.writes.back()[5], kCtlHeadlightOldV11);
    EXPECT_TRUE(p->send(Command::Horn, 0, link));
    EXPECT_EQ(link.writes.back()[5], kCtlPlaySoundOldV11);
  }
  {
    auto p = makeInmotionV2();
    InmotionV2Sim sim;
    sim.setModel(8, 1, 1, 0, 0);
    FakeLink link;
    Telemetry t;
    connect(*p, sim, link, t, sample(0.0f));
    link.writes.clear();
    EXPECT_TRUE(p->send(Command::Beep, 0, link));  // playBeep(2) = 51 02 64 (REF:2038-2046)
    EXPECT_EQ(link.writes.back(), Bytes({0xAA, 0xAA, 0x14, 0x04, 0x60, 0x51, 0x02, 0x64,
                                         static_cast<uint8_t>(0x14 ^ 0x04 ^ 0x60 ^ 0x51 ^ 0x02 ^ 0x64)}));
  }
}

TEST(InmotionV2Protocol, RefusesUnsupportedCommandsAndBadValues) {
  auto p = makeInmotionV2();
  FakeLink link;
  for (const Command c : {Command::PedalMode, Command::ResetTrip, Command::Miles}) {
    EXPECT_FALSE(p->supports(c));
    EXPECT_FALSE(p->send(c, 0, link));
  }
  EXPECT_FALSE(p->send(Command::Lights, 2, link));
  EXPECT_FALSE(p->send(Command::Lights, 3, link));
  EXPECT_FALSE(p->send(Command::RideLeds, 2, link));
  EXPECT_FALSE(p->send(Command::Volume, 101, link));
  EXPECT_FALSE(p->send(Command::Volume, -1, link));
  EXPECT_FALSE(p->send(Command::Horn, -1, link));
  EXPECT_FALSE(p->send(Command::Horn, 256, link));
  EXPECT_TRUE(link.writes.empty());
  // Nothing dangerous is ever on the wire: no lock 0x31, transport 0x32, calibrate 0x42, speed limit 0x21,
  // pedal sensitivity 0x25.
  InmotionV2Sim sim;
  Telemetry t;
  connect(*p, sim, link, t, sample(0.0f));
  link.writes.clear();
  for (int c = 0; c < static_cast<int>(Command::Count); ++c) {
    for (const int v : {-1, 0, 1, 2, 50, 100, 255}) {
      p->send(static_cast<Command>(c), v, link);
    }
  }
  for (const Bytes& w : link.writes) {
    if (w.size() > 5 && w[4] == kCmdControl) {
      const uint8_t sub = w[5];
      EXPECT_TRUE(sub == kCtlDrl || sub == kCtlHeadlight || sub == kCtlVolume || sub == kCtlPlaySound) << sub;
    }
  }
}

TEST(InmotionV2Protocol, ProfileAndIdentity) {
  auto p = makeInmotionV2();
  EXPECT_EQ(p->brand(), Brand::InmotionV2);
  EXPECT_STREQ(p->profile().notifyService, "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
  EXPECT_STREQ(p->profile().writeChar, "6e400002-b5a3-f393-e0a9-e50e24dcca9e");
  EXPECT_STREQ(p->profile().notifyChar, "6e400003-b5a3-f393-e0a9-e50e24dcca9e");
  EXPECT_FALSE(p->profile().writeWithResponse);
  EXPECT_EQ(makeInmotionV2Sim()->brand(), Brand::InmotionV2);
}
