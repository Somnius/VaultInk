#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Euc.h"
#include "NinebotCommon.h"
#include "NinebotE.h"
#include "NinebotS.h"
#include "NinebotZ.h"
#include "Protocols.h"

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
  uint8_t buf[128];
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

Bytes read(const uint8_t addr, const uint8_t reg, const uint8_t n) {
  uint8_t out[16];
  const size_t size = ninebot::buildRead(addr, reg, n, out, sizeof out);
  return Bytes(out, out + size);
}

Telemetry sample(const float speed) {
  Telemetry t;
  t.speedKmh = speed;
  t.current = -3.21f;
  t.tempMos = 36.5f;
  t.voltage = 58.73f;
  t.tripKm = 4.56f;
  t.rideSeconds = 1234;
  t.totalKm = 987.654f;
  t.rangeKm = 21.5f;
  return t;
}

// Runs the whole 21-step loop twice (so both the total and the remaining register are asked).
void runLoop(Protocol& p, WheelSimulator& sim, FakeLink& link, Telemetry& t, const Telemetry& wheel) {
  uint32_t now = 1000;
  sim.tick(now, wheel);
  p.start(link, now);
  for (int i = 0; i < 44; ++i) {
    pump(link, sim);
    deliver(sim, p, t);
    now += 60;
    p.poll(link, now);
  }
  pump(link, sim);
  deliver(sim, p, t);
}

}  // namespace

TEST(NinebotProtocol, ReadRequestsMatchEucWatchConstants) {
  // eucNinebotS.js:6-16 (address 17) and eucNinebotE.js:6-15 (address 9), decimal in the source.
  EXPECT_EQ(read(0x11, ninebot::kRegCurrent, 2), Bytes({85, 170, 3, 17, 1, 80, 2, 152, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegSpeed, 2), Bytes({85, 170, 3, 17, 1, 38, 2, 194, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegTemp, 2), Bytes({85, 170, 3, 17, 1, 62, 2, 170, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegVoltage, 2), Bytes({85, 170, 3, 17, 1, 71, 2, 161, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegTrip, 2), Bytes({85, 170, 3, 17, 1, 185, 2, 47, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegRideTime, 2), Bytes({85, 170, 3, 17, 1, 58, 2, 174, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegTotal, 4), Bytes({85, 170, 3, 17, 1, 41, 4, 189, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegRemaining, 2), Bytes({85, 170, 3, 17, 1, 37, 2, 195, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegAvgSpeed, 2), Bytes({85, 170, 3, 17, 1, 182, 2, 50, 255}));
  EXPECT_EQ(read(0x11, ninebot::kRegLock, 2), Bytes({85, 170, 3, 17, 1, 112, 2, 120, 255}));
  EXPECT_EQ(read(0x09, ninebot::kRegCurrent, 2), Bytes({85, 170, 3, 9, 1, 80, 2, 160, 255}));
  EXPECT_EQ(read(0x09, ninebot::kRegSpeed, 2), Bytes({85, 170, 3, 9, 1, 38, 2, 202, 255}));
  EXPECT_EQ(read(0x09, ninebot::kRegTotal, 4), Bytes({85, 170, 3, 9, 1, 41, 4, 197, 255}));
  EXPECT_EQ(read(0x09, ninebot::kRegLock, 2), Bytes({85, 170, 3, 9, 1, 112, 2, 128, 255}));
}

TEST(NinebotProtocol, LoopOrderMatchesEucWatch) {
  // eucNinebotS.js:5-17: current on 0,3,..,18; speed on 1,4,..,19; temp 2; voltage 5; trip 8; runtime 11;
  // total/remaining 14; average 17; lock 20.
  auto p = makeNinebotS();
  FakeLink link;
  uint32_t now = 1000;
  p->start(link, now);
  for (int i = 0; i < 20; ++i) {
    now += 60;
    p->poll(link, now);
  }
  ASSERT_EQ(link.writes.size(), 21u);
  const uint8_t expected[21] = {0x50, 0x26, 0x3E, 0x50, 0x26, 0x47, 0x50, 0x26, 0xB9, 0x50, 0x26,
                                0x3A, 0x50, 0x26, 0x29, 0x50, 0x26, 0xB6, 0x50, 0x26, 0x70};
  for (size_t i = 0; i < 21; ++i) {
    EXPECT_EQ(link.writes[i][5], expected[i]) << "step " << i;
    EXPECT_EQ(link.writes[i][3], 0x11);
  }
  EXPECT_EQ(link.writes[14][6], 4);  // total is a u32 read
  // Not before 50 ms, and the step after 20 wraps to 0 and the alternate register 0x25 on the next step 14.
  p->poll(link, now + 20);
  EXPECT_EQ(link.writes.size(), 21u);
  for (int i = 0; i < 15; ++i) {
    now += 60;
    p->poll(link, now);
  }
  EXPECT_EQ(link.writes.back()[5], 0x25);
}

TEST(NinebotProtocol, RoundTripSAndE) {
  struct Variant {
    std::unique_ptr<Protocol> p;
    std::unique_ptr<WheelSimulator> sim;
    uint8_t addr;
  };
  for (const float speed : {0.0f, 3.141f, 18.5f}) {
    Variant variants[2] = {{makeNinebotS(), makeNinebotSSim(), 0x11}, {makeNinebotE(), makeNinebotESim(), 0x09}};
    for (Variant& v : variants) {
      FakeLink link;
      Telemetry t;
      runLoop(*v.p, *v.sim, link, t, sample(speed));
      EXPECT_NEAR(t.speedKmh, speed, 0.001f);
      EXPECT_NEAR(t.current, -3.21f, 0.011f);
      EXPECT_NEAR(t.tempMos, 36.5f, 0.1f);
      EXPECT_NEAR(t.voltage, 58.73f, 0.011f);
      EXPECT_NEAR(t.tripKm, 4.56f, 0.011f);
      EXPECT_EQ(t.rideSeconds, 1234u);
      EXPECT_NEAR(t.totalKm, 987.654f, 0.0011f);
      EXPECT_NEAR(t.rangeKm, 21.5f, 0.011f);
      EXPECT_FALSE(t.has(F_BATTERY));  // the sources derive the percent from the voltage, the wheel is not asked
    }
  }
}

TEST(NinebotProtocol, ReplySplitAtEveryByteBoundaryAndBadChecksumRejected) {
  uint8_t data[2] = {0x10, 0x27};  // 10000 = 10.000 km/h
  uint8_t reply[32];
  const size_t n = ninebot::buildReply(0x11, ninebot::kRegSpeed, data, 2, reply, sizeof reply);
  ASSERT_EQ(n, 10u);
  EXPECT_EQ(reply[2], 4);  // len = n + 2
  for (size_t split = 1; split < n; ++split) {
    auto p = makeNinebotS();
    Telemetry t;
    EXPECT_FALSE(p->feed(reply, split, t));
    EXPECT_TRUE(p->feed(reply + split, n - split, t)) << "split " << split;
    EXPECT_NEAR(t.speedKmh, 10.0f, 0.0001f);
  }
  auto p = makeNinebotE();
  Telemetry t;
  Bytes bad(reply, reply + n);
  bad[8] = static_cast<uint8_t>(bad[8] ^ 0x01);
  EXPECT_FALSE(p->feed(bad.data(), bad.size(), t));
  EXPECT_FALSE(t.has(F_SPEED));
  const Bytes garbage = {0x55, 0x00, 0xAA, 0x55, 0xAA, 0xFF, 0x11, 0x04, 0x26};
  EXPECT_FALSE(p->feed(garbage.data(), garbage.size(), t));
  Bytes longGarbage(1000, 0x55);
  EXPECT_FALSE(p->feed(longGarbage.data(), longGarbage.size(), t));
  // The real frame after the garbage still decodes (the oversized-length frame was dropped).
  EXPECT_TRUE(p->feed(reply, n, t));
}

TEST(NinebotProtocol, NoCommands) {
  FakeLink link;
  std::unique_ptr<Protocol> protocols[3] = {makeNinebotS(), makeNinebotE(), makeNinebotZ()};
  for (const auto& p : protocols) {
    for (int c = 0; c < static_cast<int>(Command::Count); ++c) {
      EXPECT_FALSE(p->supports(static_cast<Command>(c)));
      EXPECT_FALSE(p->send(static_cast<Command>(c), 1, link));
    }
  }
  EXPECT_TRUE(link.writes.empty());
}

TEST(NinebotProtocol, ProfilesAndIdentity) {
  EXPECT_EQ(makeNinebotS()->brand(), Brand::NinebotS);
  EXPECT_EQ(makeNinebotE()->brand(), Brand::NinebotE);
  EXPECT_EQ(makeNinebotZ()->brand(), Brand::NinebotZ);
  EXPECT_EQ(makeNinebotSSim()->brand(), Brand::NinebotS);
  EXPECT_EQ(makeNinebotESim()->brand(), Brand::NinebotE);
  EXPECT_EQ(makeNinebotZSim()->brand(), Brand::NinebotZ);
  EXPECT_STREQ(makeNinebotS()->profile().notifyService, "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
  EXPECT_STREQ(makeNinebotE()->profile().notifyService, "0000ffe0-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(makeNinebotE()->profile().writeChar, "0000ffe1-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(makeNinebotE()->profile().notifyChar, "0000ffe1-0000-1000-8000-00805f9b34fb");
}

TEST(NinebotProtocol, ZRequestMatchesEucWatchAndIsPolledEvery50ms) {
  auto p = makeNinebotZ();
  FakeLink link;
  p->start(link, 1000);
  ASSERT_EQ(link.writes.size(), 1u);
  EXPECT_EQ(link.writes[0], Bytes({0x5a, 0xa5, 0x01, 0x3e, 0x14, 0x01, 0xb0, 0x20, 0xdb, 0xfe}));  // eucNinebotZ.js:6
  link.writes.clear();
  p->poll(link, 1040);
  EXPECT_TRUE(link.writes.empty());  // no reply yet and no timeout
  auto sim = makeNinebotZSim();
  sim->tick(0, sample(12.5f));
  sim->onWrite(Bytes({0x5a, 0xa5, 0x01, 0x3e, 0x14, 0x01, 0xb0, 0x20, 0xdb, 0xfe}).data(), 10);
  Telemetry t;
  EXPECT_TRUE(deliver(*sim, *p, t));
  p->poll(link, 1049);
  EXPECT_TRUE(link.writes.empty());
  p->poll(link, 1050);
  EXPECT_EQ(link.writes.size(), 1u);
  link.writes.clear();
  p->poll(link, 1549);
  EXPECT_TRUE(link.writes.empty());  // unanswered: wait for the timeout
  p->poll(link, 1550);
  EXPECT_EQ(link.writes.size(), 1u);
}

TEST(NinebotProtocol, ZRoundTripInTwentyByteChunksAndEveryBoundary) {
  for (const float speed : {0.0f, 8.25f, 27.5f}) {
    auto p = makeNinebotZ();
    auto sim = makeNinebotZSim();
    FakeLink link;
    Telemetry t;
    sim->tick(0, sample(speed));
    p->start(link, 1000);
    pump(link, *sim);
    EXPECT_TRUE(deliver(*sim, *p, t));
    EXPECT_NEAR(t.speedKmh, speed, 0.011f);
    EXPECT_NEAR(t.current, -3.21f, 0.011f);
    EXPECT_NEAR(t.tempMos, 36.5f, 0.1f);
    EXPECT_NEAR(t.voltage, 58.73f, 0.011f);
    EXPECT_EQ(t.rideSeconds, 1234u);
    EXPECT_NEAR(t.totalKm, 987.654f, 0.0011f);
  }
  auto sim = makeNinebotZSim();
  sim->tick(0, sample(19.0f));
  sim->onWrite(ninebotz::kRequest, sizeof ninebotz::kRequest);
  uint8_t buf[128];
  size_t n = 0;
  ASSERT_TRUE(sim->pop(buf, sizeof buf, n));
  EXPECT_EQ(n, 41u);  // 7 header + 32 data + 2 check: eucWatch's two 20-byte chunks and one more byte
  EXPECT_EQ(buf[0], 0x5A);
  // eucWatch reads the speed from chunk 1 byte 17 and the voltage from chunk 2 byte 11 (eucNinebotZ.js:41,57).
  EXPECT_EQ(buf[17] | (buf[18] << 8), 1900);
  EXPECT_EQ(buf[20 + 11] | (buf[20 + 12] << 8), 5873);
  for (size_t split = 1; split < n; ++split) {
    auto p = makeNinebotZ();
    Telemetry t;
    EXPECT_FALSE(p->feed(buf, split, t));
    EXPECT_TRUE(p->feed(buf + split, n - split, t)) << "split " << split;
    EXPECT_NEAR(t.speedKmh, 19.0f, 0.011f);
  }
  auto p = makeNinebotZ();
  Telemetry t;
  Bytes bad(buf, buf + n);
  bad[25] = static_cast<uint8_t>(bad[25] ^ 0x80);
  EXPECT_FALSE(p->feed(bad.data(), bad.size(), t));
  EXPECT_FALSE(t.has(F_SPEED));
  Bytes garbage(500, 0x5A);
  garbage[1] = 0xA5;
  garbage[2] = 0xFF;
  EXPECT_FALSE(p->feed(garbage.data(), garbage.size(), t));
  EXPECT_TRUE(p->feed(buf, n, t));
}
