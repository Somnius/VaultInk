#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Euc.h"
#include "Protocols.h"
#include "Vesc.h"

using namespace euc;

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
  uint8_t buf[300];
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

// VESCDash/source/BLE/GarminEUCBleDelegate.mc:48-64, a real COMM_GET_VALUES_SETUP reply in four notifications.
const Bytes kTestPacket = {0x02, 0x46, 0x2f, 0x01, 0x29, 0x00, 0xf9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xf0, 0x03, 0xdf, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                           0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0xea, 0x0a, 0x9f, 0x1d, 0xc0, 0x00, 0x1c, 0x01,
                           0x00, 0x08, 0x90, 0xd8, 0x00, 0x06, 0x65, 0xbf, 0x00, 0x02, 0x3e, 0x45, 0x48, 0xae, 0x03};
// :71-85 "ubox" reply, six notifications of 2/20/20/20/10/3 bytes.
const Bytes kUbox = {0x02, 0x46, 0x2f, 0x00, 0xd5, 0xff, 0x14, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x6c, 0x02, 0x56, 0x00,
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x01, 0x13, 0x00, 0xdb, 0xb9, 0xfa, 0x00, 0x05, 0x01,
                     0x00, 0x06, 0x10, 0xb3, 0x00, 0x00, 0x05, 0x6f, 0x00, 0x00, 0xf9, 0xdd, 0x6f, 0xc9, 0x03};

Telemetry sample(const float speed) {
  Telemetry t;
  t.speedKmh = speed;
  t.tempMos = 31.5f;
  t.tempMotor = 48.2f;
  t.current = -14.37f;
  t.pwm = 67.3f;
  t.voltage = 58.9f;
  t.batteryPct = 81.4f;
  t.faultCode = 0;
  t.totalKm = 321.987f;
  t.rideSeconds = 777;
  return t;
}

}  // namespace

TEST(VescProtocol, RealVescDashTestPacketDecodes) {
  auto p = makeVesc();
  Telemetry t;
  ASSERT_EQ(kTestPacket.size(), 75u);
  // Delivered as VESCDash got it: 20, 20, 20, 15 bytes.
  EXPECT_FALSE(p->feed(kTestPacket.data(), 20, t));
  EXPECT_FALSE(p->feed(kTestPacket.data() + 20, 20, t));
  EXPECT_FALSE(p->feed(kTestPacket.data() + 40, 20, t));
  EXPECT_TRUE(p->feed(kTestPacket.data() + 60, 15, t));
  EXPECT_NEAR(t.tempMos, 29.7f, 0.001f);
  EXPECT_NEAR(t.tempMotor, 24.9f, 0.001f);
  EXPECT_FLOAT_EQ(t.current, 0.0f);
  EXPECT_FLOAT_EQ(t.pwm, 0.0f);
  EXPECT_FLOAT_EQ(t.speedKmh, 0.0f);
  EXPECT_NEAR(t.voltage, 75.2f, 0.001f);
  EXPECT_NEAR(t.batteryPct, 99.1f, 0.001f);
  EXPECT_EQ(t.faultCode, 0);
  EXPECT_TRUE(t.has(F_FAULT));
  EXPECT_NEAR(t.totalKm, 419.263f, 0.0001f);
}

TEST(VescProtocol, RealUboxPacketDecodes) {
  auto p = makeVesc();
  Telemetry t;
  ASSERT_EQ(kUbox.size(), 75u);
  const size_t chunks[] = {2, 20, 20, 20, 10, 3};
  size_t o = 0;
  bool changed = false;
  for (const size_t c : chunks) {
    changed = p->feed(kUbox.data() + o, c, t);
    o += c;
  }
  EXPECT_TRUE(changed);
  EXPECT_NEAR(t.tempMos, 21.3f, 0.001f);
  EXPECT_NEAR(t.tempMotor, -23.6f, 0.001f);  // 0xFF14: no motor sensor
  EXPECT_FLOAT_EQ(t.current, 0.0f);
  EXPECT_NEAR(t.pwm, 0.6f, 0.001f);
  EXPECT_FLOAT_EQ(t.speedKmh, 0.0f);
  EXPECT_NEAR(t.voltage, 62.0f, 0.001f);
  EXPECT_NEAR(t.batteryPct, 59.8f, 0.001f);
  EXPECT_EQ(t.faultCode, 0);
  EXPECT_NEAR(t.totalKm, 1.391f, 0.0001f);
}

TEST(VescProtocol, StartSendsGetValuesSetupAndPolls200msAfterAReply) {
  auto p = makeVesc();
  FakeLink link;
  p->start(link, 1000);
  ASSERT_EQ(link.writes.size(), 1u);
  EXPECT_EQ(link.writes[0], Bytes({0x02, 0x01, 0x2f, 0xd5, 0x8d, 0x03}));  // GarminEUCBleDelegate.mc:129
  link.writes.clear();
  p->poll(link, 1500);
  EXPECT_TRUE(link.writes.empty());  // no reply yet, timeout not reached
  Telemetry t;
  p->feed(kTestPacket.data(), kTestPacket.size(), t);
  p->poll(link, 1199);
  EXPECT_TRUE(link.writes.empty());
  p->poll(link, 1200);
  EXPECT_EQ(link.writes.size(), 1u);
  link.writes.clear();
  p->poll(link, 2199);
  EXPECT_TRUE(link.writes.empty());
  p->poll(link, 2200);  // unanswered for 1 s: ask again
  EXPECT_EQ(link.writes.size(), 1u);
}

TEST(VescProtocol, RoundTripSeveralValues) {
  for (const float speed : {0.0f, 5.5f, 24.84f, 44.1f}) {
    auto p = makeVesc();
    auto sim = makeVescSim();
    FakeLink link;
    Telemetry t;
    sim->tick(0, sample(speed));
    p->start(link, 1000);
    pump(link, *sim);
    EXPECT_TRUE(deliver(*sim, *p, t));
    EXPECT_NEAR(t.speedKmh, speed, 0.004f);  // m/s in mm
    EXPECT_NEAR(t.tempMos, 31.5f, 0.051f);
    EXPECT_NEAR(t.tempMotor, 48.2f, 0.051f);
    EXPECT_NEAR(t.current, -14.37f, 0.006f);
    EXPECT_NEAR(t.pwm, 67.3f, 0.051f);
    EXPECT_NEAR(t.voltage, 58.9f, 0.051f);
    EXPECT_NEAR(t.batteryPct, 81.4f, 0.051f);
    EXPECT_NEAR(t.totalKm, 321.987f, 0.0006f);
    EXPECT_EQ(t.faultCode, 0);
    EXPECT_FALSE(t.has(F_TRIP));
  }
}

TEST(VescProtocol, FrameSplitAtEveryByteBoundary) {
  for (size_t split = 1; split < kTestPacket.size(); ++split) {
    auto p = makeVesc();
    Telemetry t;
    EXPECT_FALSE(p->feed(kTestPacket.data(), split, t));
    EXPECT_TRUE(p->feed(kTestPacket.data() + split, kTestPacket.size() - split, t)) << "split " << split;
    EXPECT_NEAR(t.voltage, 75.2f, 0.001f);
  }
}

TEST(VescProtocol, GarbageTruncatedAndBadCrcDropped) {
  auto p = makeVesc();
  Telemetry t;
  const Bytes garbage = {0x00, 0x03, 0x02, 0x05, 0x2f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x02};
  EXPECT_FALSE(p->feed(garbage.data(), garbage.size(), t));
  Bytes bad = kTestPacket;
  bad[30] = static_cast<uint8_t>(bad[30] ^ 0x01);
  EXPECT_FALSE(p->feed(bad.data(), bad.size(), t));
  EXPECT_FALSE(t.has(F_VOLTAGE));
  Bytes badEnd = kTestPacket;
  badEnd[74] = 0x00;
  EXPECT_FALSE(p->feed(badEnd.data(), badEnd.size(), t));
  // Truncated reply, then a whole one: the whole one decodes once its own 0x02 is seen after the bogus length
  // runs out.
  EXPECT_FALSE(p->feed(kTestPacket.data(), 40, t));
  Bytes filler(35, 0x00);  // finishes the swallowed frame (its CRC fails)
  EXPECT_FALSE(p->feed(filler.data(), filler.size(), t));
  EXPECT_TRUE(p->feed(kTestPacket.data(), kTestPacket.size(), t));
  EXPECT_NEAR(t.voltage, 75.2f, 0.001f);
  Bytes longGarbage(2000, 0x02);
  EXPECT_FALSE(p->feed(longGarbage.data(), longGarbage.size(), t));
  Bytes payload(255, 0x2f);
  uint8_t frame[300];
  const size_t n = vesc::buildFrame(payload.data(), payload.size(), frame, sizeof frame);
  ASSERT_EQ(n, 260u);
  EXPECT_FALSE(p->feed(frame, n, t));  // valid CRC but a payload that is not a values-setup reply: no change
}

TEST(VescProtocol, NoCommandsAndIdentity) {
  auto p = makeVesc();
  FakeLink link;
  for (int c = 0; c < static_cast<int>(Command::Count); ++c) {
    EXPECT_FALSE(p->supports(static_cast<Command>(c)));
    EXPECT_FALSE(p->send(static_cast<Command>(c), 1, link));
  }
  EXPECT_TRUE(link.writes.empty());
  EXPECT_EQ(p->brand(), Brand::Vesc);
  EXPECT_STREQ(p->profile().scanName, "VESC");
  EXPECT_STREQ(p->profile().writeChar, "6e400002-b5a3-f393-e0a9-e50e24dcca9e");
  EXPECT_EQ(makeVescSim()->brand(), Brand::Vesc);
}
