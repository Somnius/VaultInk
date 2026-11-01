#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Euc.h"
#include "InmotionV1.h"
#include "Protocols.h"

using namespace euc;
using namespace euc::inmotionv1;

namespace {

using Bytes = std::vector<uint8_t>;

struct FakeLink : Link {
  std::vector<Bytes> writes;
  bool write(const uint8_t* d, const size_t n) override {
    writes.emplace_back(d, d + n);
    return true;
  }
};

bool deliver(WheelSimulator& sim, Protocol& p, Telemetry& t, const size_t chunk = 20) {
  uint8_t buf[600];
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

Bytes request(const uint8_t cmd, const uint8_t flags, const Bytes& data8, const uint8_t type) {
  uint8_t out[64];
  const size_t n = buildRequest(cmd, flags, data8.data(), type, out, sizeof out);
  return Bytes(out, out + n);
}

const Bytes kFF = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const Bytes kEnd = {0x55, 0x55};

Telemetry sample(const float speed) {
  Telemetry t;
  t.speedKmh = speed;
  t.voltage = 83.21f;
  t.current = -7.35f;
  t.tempMos = 37;
  t.tempMotor = 61;
  t.totalKm = 2345.678f;
  t.rideMode = 1;
  return t;
}

}  // namespace

TEST(InmotionV1Protocol, RequestBytesMatchEucWatchConstants) {
  // eucInmotionV10.js:7-13.
  EXPECT_EQ(request(kCmdInfo, 0x01, kFF, 0x01), Bytes({0xAA, 0xAA, 0x14, 0x01, 0xA5, 0x55, 0x0F, 0xFF, 0xFF, 0xFF,
                                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x05, 0x00, 0x01, 0x7F}));
  EXPECT_EQ(request(kCmdLive, 0x01, kFF, 0x00), Bytes({0xAA, 0xAA, 0x13, 0x01, 0xA5, 0x55, 0x0F, 0xFF, 0xFF, 0xFF,
                                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x05, 0x00, 0x00, 0x7D}));
  EXPECT_EQ(request(kCmdPinCode, 0x03, {0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00}, 0x00),
            Bytes({0xAA, 0xAA, 0x07, 0x03, 0xA5, 0x55, 0x0F, 0x30, 0x30, 0x30,
                   0x30, 0x30, 0x30, 0x00, 0x00, 0x08, 0x05, 0x00, 0x00, 0x9B}));
  EXPECT_EQ(request(kCmdPlaySound, 0x06, {0x04, 0, 0, 0, 0, 0, 0, 0}, 0x00),
            Bytes({0xAA, 0xAA, 0x09, 0x06, 0xA5, 0x55, 0x0F, 0x04, 0x00, 0x00,
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0x00, 0x00, 0x84}));
  EXPECT_EQ(request(kCmdPlaySound, 0x06, {0x15, 0, 0, 0, 0, 0, 0, 0}, 0x00),
            Bytes({0xAA, 0xAA, 0x09, 0x06, 0xA5, 0x55, 0x0F, 0x15, 0x00, 0x00,
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0x00, 0x00, 0x95}));
}

TEST(InmotionV1Protocol, EucWatchChecksumFormulaAgreesOnItsOwnConstants) {
  // eucInmotionV10.js:64: check = (sum of every frame byte before it) + 7, which equals the sum of the unescaped
  // body because AA + AA + A5 + 7 = 0x200.
  for (const Bytes& f : {request(kCmdInfo, 0x01, kFF, 0x01), request(kCmdLive, 0x01, kFF, 0x00),
                         request(kCmdPlaySound, 0x06, {0x04, 0, 0, 0, 0, 0, 0, 0}, 0x00)}) {
    unsigned sum = 7;
    for (size_t i = 0; i + 1 < f.size(); ++i) {
      sum += f[i];
    }
    EXPECT_EQ(sum & 0xFF, f.back());
  }
}

TEST(InmotionV1Protocol, StartSendsInitThenPollsLiveAndInfoWithTerminators) {
  auto p = makeInmotionV1();
  FakeLink link;
  p->start(link, 1000);
  ASSERT_EQ(link.writes.size(), 2u);
  EXPECT_EQ(link.writes[0][2], kCmdPinCode);
  EXPECT_EQ(link.writes[1], kEnd);
  link.writes.clear();
  p->poll(link, 1400);
  EXPECT_TRUE(link.writes.empty());
  p->poll(link, 1500);
  ASSERT_EQ(link.writes.size(), 4u);  // live, 55 55, info, 55 55 (eucInmotionV10.js:309-316)
  EXPECT_EQ(link.writes[0], request(kCmdLive, 0x01, kFF, 0x00));
  EXPECT_EQ(link.writes[1], kEnd);
  EXPECT_EQ(link.writes[2], request(kCmdInfo, 0x01, kFF, 0x01));
  EXPECT_EQ(link.writes[3], kEnd);
}

TEST(InmotionV1Protocol, RoundTripSeveralValues) {
  for (const float speed : {0.0f, 4.5f, 22.75f, 39.9f}) {
    auto p = makeInmotionV1();
    auto sim = makeInmotionV1Sim();
    FakeLink link;
    Telemetry t;
    sim->tick(0, sample(speed));
    p->start(link, 1000);
    pump(link, *sim);
    EXPECT_TRUE(static_cast<InmotionV1Sim&>(*sim).initSeen());
    p->poll(link, 1500);
    pump(link, *sim);
    EXPECT_TRUE(deliver(*sim, *p, t));
    EXPECT_NEAR(t.speedKmh, speed, 0.001f);
    EXPECT_NEAR(t.voltage, 83.21f, 0.011f);
    EXPECT_NEAR(t.current, -7.35f, 0.011f);
    EXPECT_FLOAT_EQ(t.tempMos, 37);
    EXPECT_FLOAT_EQ(t.tempMotor, 61);
    EXPECT_NEAR(t.totalKm, 2345.678f, 0.0011f);
    EXPECT_EQ(t.rideMode, 1);
    EXPECT_TRUE(t.has(F_MODEL));
    EXPECT_STREQ(t.model, "InMotion V10");
    EXPECT_STREQ(t.firmware, "1.2.123");
    EXPECT_STREQ(t.serial, "EFCDAB8967452301");  // byte 23 first (eucInmotionV10.js:129-136)
    EXPECT_EQ(t.lightMode, 0);
  }
}

TEST(InmotionV1Protocol, FrameSplitAtEveryByteBoundary) {
  auto sim = makeInmotionV1Sim();
  sim->tick(0, sample(17.25f));
  const Bytes live = request(kCmdLive, 0x01, kFF, 0x00);
  sim->onWrite(live.data(), live.size());
  sim->onWrite(kEnd.data(), kEnd.size());
  uint8_t buf[600];
  size_t n = 0;
  ASSERT_TRUE(sim->pop(buf, sizeof buf, n));
  EXPECT_EQ(buf[n - 1], 0x55);
  EXPECT_EQ(buf[n - 2], 0x55);
  for (size_t split = 1; split < n; ++split) {
    auto p = makeInmotionV1();
    Telemetry t;
    EXPECT_FALSE(p->feed(buf, split, t));
    EXPECT_TRUE(p->feed(buf + split, n - split, t)) << "split " << split;
    EXPECT_NEAR(t.speedKmh, 17.25f, 0.001f);
  }
}

TEST(InmotionV1Protocol, EscapedBytesInsideTheBodyRoundTrip) {
  // Voltage 0xAA55 hundredths = 436.05 V exercises both escapes; the body A5 too via the current.
  auto sim = makeInmotionV1Sim();
  Telemetry w = sample(1.0f);
  w.voltage = 436.05f;
  w.current = -0.91f;  // 0xFFA5
  sim->tick(0, w);
  const Bytes live = request(kCmdLive, 0x01, kFF, 0x00);
  sim->onWrite(live.data(), live.size());
  sim->onWrite(kEnd.data(), kEnd.size());
  auto p = makeInmotionV1();
  Telemetry t;
  EXPECT_TRUE(deliver(*sim, *p, t, 7));
  EXPECT_NEAR(t.voltage, 436.05f, 0.011f);
  EXPECT_NEAR(t.current, -0.91f, 0.011f);
}

TEST(InmotionV1Protocol, GarbageTruncatedAndBadChecksumDropped) {
  auto sim = makeInmotionV1Sim();
  sim->tick(0, sample(9.0f));
  const Bytes live = request(kCmdLive, 0x01, kFF, 0x00);
  sim->onWrite(live.data(), live.size());
  sim->onWrite(kEnd.data(), kEnd.size());
  uint8_t buf[600];
  size_t n = 0;
  ASSERT_TRUE(sim->pop(buf, sizeof buf, n));
  auto p = makeInmotionV1();
  Telemetry t;
  const Bytes garbage = {0x55, 0x55, 0xAA, 0x00, 0xA5, 0x55, 0x55, 0xAA, 0xAA, 0x13, 0x55, 0x55, 0x01};
  EXPECT_FALSE(p->feed(garbage.data(), garbage.size(), t));
  EXPECT_FALSE(p->feed(buf, 30, t));  // truncated
  EXPECT_TRUE(p->feed(buf, n, t));    // the whole frame after it still decodes
  EXPECT_NEAR(t.speedKmh, 9.0f, 0.001f);
  Bytes bad(buf, buf + n);
  bad[20] = static_cast<uint8_t>(bad[20] ^ 0x01);
  Telemetry u;
  EXPECT_FALSE(p->feed(bad.data(), bad.size(), u));
  EXPECT_FALSE(u.has(F_SPEED));
  Bytes longGarbage(3000, 0x13);
  longGarbage[0] = 0xAA;
  longGarbage[1] = 0xAA;
  EXPECT_FALSE(p->feed(longGarbage.data(), longGarbage.size(), u));
}

TEST(InmotionV1Protocol, CommandsEncodeExactBytesAndRefuseBadValues) {
  auto p = makeInmotionV1();
  FakeLink link;
  EXPECT_TRUE(p->send(Command::Horn, 0, link));
  ASSERT_EQ(link.writes.size(), 2u);
  EXPECT_EQ(link.writes[0], request(kCmdPlaySound, 0x06, {0x04, 0, 0, 0, 0, 0, 0, 0}, 0x00));
  EXPECT_EQ(link.writes[1], kEnd);
  link.writes.clear();
  EXPECT_TRUE(p->send(Command::Beep, 0, link));
  EXPECT_EQ(link.writes[0], request(kCmdPlaySound, 0x06, {0x15, 0, 0, 0, 0, 0, 0, 0}, 0x00));
  link.writes.clear();
  // setLights (eucInmotionV10.js:24-27): cmd 0D flags 01 data[0] = 0|1.
  EXPECT_TRUE(p->send(Command::Lights, 1, link));
  EXPECT_EQ(link.writes[0], request(kCmdLights, 0x01, {0x01, 0, 0, 0, 0, 0, 0, 0}, 0x00));
  link.writes.clear();
  // setVolume (:20-23): cmd 0A flags 06, u16 LE value*100: 85 -> 8500 = 0x2134.
  EXPECT_TRUE(p->send(Command::Volume, 85, link));
  EXPECT_EQ(link.writes[0], request(kCmdVolume, 0x06, {0x34, 0x21, 0, 0, 0, 0, 0, 0}, 0x00));
  link.writes.clear();
  EXPECT_FALSE(p->send(Command::Lights, 2, link));
  EXPECT_FALSE(p->send(Command::Volume, 101, link));
  EXPECT_FALSE(p->send(Command::Horn, 24, link));
  EXPECT_FALSE(p->send(Command::Horn, -1, link));
  for (const Command c : {Command::RideLeds, Command::PedalMode, Command::ResetTrip, Command::Miles}) {
    EXPECT_FALSE(p->supports(c));
    EXPECT_FALSE(p->send(c, 0, link));
  }
  EXPECT_TRUE(link.writes.empty());
  // The simulator sees the light command.
  auto sim = makeInmotionV1Sim();
  EXPECT_TRUE(p->send(Command::Lights, 1, link));
  pump(link, *sim);
  EXPECT_TRUE(static_cast<InmotionV1Sim&>(*sim).lightOn());
}

TEST(InmotionV1Protocol, ProfileAndIdentity) {
  auto p = makeInmotionV1();
  EXPECT_EQ(p->brand(), Brand::InmotionV1);
  EXPECT_STREQ(p->profile().writeService, "0000ffe5-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().writeChar, "0000ffe9-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().notifyService, "0000ffe0-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().notifyChar, "0000ffe4-0000-1000-8000-00805f9b34fb");
  EXPECT_EQ(makeInmotionV1Sim()->brand(), Brand::InmotionV1);
  EXPECT_STREQ(modelName(8, 6), "V8F");
  EXPECT_STREQ(modelName(5, 0), "V5");
  EXPECT_STREQ(modelName(9, 9), "");
}
