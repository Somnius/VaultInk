#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "Begode.h"
#include "Protocols.h"

namespace euc {
namespace {

class FakeLink : public Link {
 public:
  bool write(const uint8_t* data, const size_t size) override {
    writes.emplace_back(data, data + size);
    return true;
  }
  std::vector<std::vector<uint8_t>> writes;
};

// Feeds `frame` in pieces of `chunk` bytes.
bool feedChunked(Protocol& p, const std::vector<uint8_t>& frame, const size_t chunk, Telemetry& t) {
  bool changed = false;
  for (size_t i = 0; i < frame.size(); i += chunk) {
    const size_t n = std::min(chunk, frame.size() - i);
    changed = p.feed(frame.data() + i, n, t) || changed;
  }
  return changed;
}

// Runs one simulator tick, answers the protocol's writes, delivers every frame in 20-byte notifications.
void roundTrip(BegodeSimulator& sim, Protocol& proto, const Telemetry& in, Telemetry& out, FakeLink& link) {
  for (const auto& w : link.writes) {
    sim.onWrite(w.data(), w.size());
  }
  link.writes.clear();
  sim.tick(0, in);
  uint8_t buf[64];
  size_t n = 0;
  while (sim.pop(buf, sizeof(buf), n)) {
    feedChunked(proto, std::vector<uint8_t>(buf, buf + n), 20, out);
  }
}

// Real frames from eucWatch v2/euc/eucBegode/begexample.txt (an 84 V class wheel, 24-byte frames split at 20).
const std::vector<uint8_t> kRealFrameA = {0x55, 0xAA, 0x17, 0xE1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50,
                                          0xFE, 0x4D, 0x00, 0x01, 0x00, 0x09, 0x00, 0x18, 0x5A, 0x5A, 0x5A, 0x5A};
const std::vector<uint8_t> kRealFrameB = {0x55, 0xAA, 0x00, 0x00, 0xC2, 0x5A, 0x28, 0x00, 0x06, 0xF8, 0x00, 0x63,
                                          0x00, 0x01, 0x00, 0x03, 0x00, 0xC3, 0x04, 0x18, 0x5A, 0x5A, 0x5A, 0x5A};

}  // namespace

TEST(BegodeProtocol, DecodesRealFrameA) {
  BegodeProtocol p;
  Telemetry t;
  ASSERT_TRUE(p.feed(kRealFrameA.data(), kRealFrameA.size(), t));
  EXPECT_TRUE(t.has(F_VOLTAGE));
  EXPECT_NEAR(t.voltage, 61.13f, 0.001f);
  EXPECT_NEAR(t.speedKmh, 0.0f, 0.001f);
  EXPECT_NEAR(t.tripKm, 0.0f, 0.001f);
  EXPECT_NEAR(t.current, 0x50 * kBegodeCurrentScale, 0.001f);
  EXPECT_NEAR(t.tempMos, -435 / 340.0f + 36.53f, 0.01f);
  EXPECT_TRUE(t.has(F_BATTERY));
  EXPECT_FALSE(t.has(F_PWM));  // never set for Begode
  EXPECT_FALSE(t.has(F_TOTAL));
}

TEST(BegodeProtocol, DecodesRealFrameB) {
  BegodeProtocol p;
  Telemetry t;
  ASSERT_TRUE(p.feed(kRealFrameB.data(), kRealFrameB.size(), t));
  EXPECT_TRUE(t.has(F_TOTAL));
  EXPECT_NEAR(t.totalKm, 49.754f, 0.0005f);
  EXPECT_TRUE(t.has(F_RIDE_MODE));
  EXPECT_EQ(t.rideMode, 1);  // 0x2800 >> 13 = medium
  EXPECT_TRUE(t.has(F_ALARMS));
  EXPECT_EQ(t.alarmBits, 0);
  EXPECT_FALSE(t.has(F_LIGHT));
  EXPECT_FALSE(t.has(F_VOLTAGE));
}

TEST(BegodeProtocol, RealChunkingFromTheDumpWorks) {
  // begexample.txt delivers: [5A 5A 5A 5A + 16 bytes of B] [8 bytes of B] [20 bytes of A] [4 x 5A + 16 of B] ...
  BegodeProtocol p;
  Telemetry t;
  std::vector<uint8_t> stream = {0x5A, 0x5A, 0x5A, 0x5A};
  stream.insert(stream.end(), kRealFrameB.begin(), kRealFrameB.end());
  stream.insert(stream.end(), kRealFrameA.begin(), kRealFrameA.end());
  const size_t cuts[] = {20, 28, 48, 52};  // A's trailer arrives at the start of the next notification
  size_t at = 0;
  int frames = 0;
  for (const size_t cut : cuts) {
    frames += p.feed(stream.data() + at, cut - at, t) ? 1 : 0;
    at = cut;
  }
  EXPECT_EQ(frames, 2);
  EXPECT_NEAR(t.totalKm, 49.754f, 0.0005f);
  EXPECT_NEAR(t.voltage, 61.13f, 0.001f);
}

TEST(BegodeProtocol, EveryChunkBoundary) {
  for (size_t chunk = 1; chunk <= 24; ++chunk) {
    BegodeProtocol p;
    Telemetry t;
    std::vector<uint8_t> both = kRealFrameA;
    both.insert(both.end(), kRealFrameB.begin(), kRealFrameB.end());
    feedChunked(p, both, chunk, t);
    EXPECT_NEAR(t.voltage, 61.13f, 0.001f) << "chunk " << chunk;
    EXPECT_NEAR(t.totalKm, 49.754f, 0.0005f) << "chunk " << chunk;
  }
}

TEST(BegodeProtocol, GarbageAndTruncatedInputDoNotCorruptLaterFrames) {
  BegodeProtocol p;
  Telemetry t;
  const uint8_t garbage[] = {0x55, 0xAA, 0x01, 0x02, 0x55, 0x00, 0xAA, 0x5A, 0x5A, 0xFF, 0x18};
  EXPECT_FALSE(p.feed(garbage, sizeof(garbage), t));
  EXPECT_FALSE(p.feed(kRealFrameA.data(), 17, t));  // truncated: no trailer
  EXPECT_FALSE(p.feed(nullptr, 0, t));
  // A frame with a wrong length byte (19) is dropped (WheelLog rule).
  std::vector<uint8_t> badLen = kRealFrameA;
  badLen[19] = 0x17;
  EXPECT_FALSE(p.feed(badLen.data(), badLen.size(), t));
  // A frame with a broken trailer is dropped.
  std::vector<uint8_t> badTrailer = kRealFrameA;
  badTrailer[22] = 0x00;
  EXPECT_FALSE(p.feed(badTrailer.data(), badTrailer.size(), t));
  EXPECT_EQ(t.valid, 0u);
  EXPECT_TRUE(p.feed(kRealFrameA.data(), kRealFrameA.size(), t));
  EXPECT_NEAR(t.voltage, 61.13f, 0.001f);
}

TEST(BegodeProtocol, ModelAndFirmwareTextReplies) {
  BegodeProtocol p;
  Telemetry t;
  const char* model = "NAME:Msuper Pro";
  EXPECT_TRUE(p.feed(reinterpret_cast<const uint8_t*>(model), std::strlen(model), t));
  EXPECT_TRUE(t.has(F_MODEL));
  EXPECT_STREQ(t.model, "Msuper Pro");
  EXPECT_FLOAT_EQ(p.voltageMultiplier(), 1.5f);
  const char* fw = "GW2.03\r\n";
  EXPECT_TRUE(p.feed(reinterpret_cast<const uint8_t*>(fw), std::strlen(fw), t));
  EXPECT_TRUE(t.has(F_FIRMWARE));
  EXPECT_STREQ(t.firmware, "GW2.03");
  // From now on frame A voltage is multiplied (eucBegode.js:179).
  ASSERT_TRUE(p.feed(kRealFrameA.data(), kRealFrameA.size(), t));
  EXPECT_NEAR(t.voltage, 61.13f * 1.5f, 0.01f);
}

TEST(BegodeProtocol, VoltageMultiplierTable) {
  EXPECT_FLOAT_EQ(begodeVoltageMultiplier("Mten3"), 1.25f);
  EXPECT_FLOAT_EQ(begodeVoltageMultiplier("Master"), 2.0f);
  EXPECT_FLOAT_EQ(begodeVoltageMultiplier("EX20S C38"), 1.5f);
  EXPECT_FLOAT_EQ(begodeVoltageMultiplier("Tesla"), 1.0f);
  EXPECT_FLOAT_EQ(begodeVoltageMultiplier(""), 1.0f);
}

TEST(BegodeProtocol, RoundTripThroughSimulator) {
  struct Case {
    const char* model;
    float voltage, speed, trip, current, temp, total;
    uint8_t rideMode;
    uint16_t alarms;
  };
  const Case cases[] = {
      {"Tesla", 61.13f, 0.0f, 0.0f, 0.8f, 35.25f, 49.754f, 1, 0},
      {"Mten3", 82.5f, 25.3f, 12.345f, -4.2f, 41.0f, 1234.567f, 0, ALARM_SPEED1 | ALARM_HIGH_TEMP},
      {"Msuper Pro", 95.0f, 45.7f, 60.0f, 38.0f, 70.5f, 9999.999f, 2, ALARM_HIGH_POWER},
      {"Master", 130.2f, 60.1f, 3.2f, 120.0f, 25.0f, 0.001f, 1, ALARM_TRANSPORT | ALARM_HALL_ERROR},
  };
  for (const Case& c : cases) {
    BegodeSimulator sim;
    sim.setModel(c.model);
    auto proto = makeBegode();
    FakeLink link;
    proto->start(link, 0);
    ASSERT_EQ(link.writes.size(), 2u);  // 'N' then 'V'
    Telemetry in;
    in.voltage = c.voltage;
    in.speedKmh = c.speed;
    in.tripKm = c.trip;
    in.current = c.current;
    in.tempMos = c.temp;
    in.totalKm = c.total;
    in.rideMode = c.rideMode;
    in.alarmBits = c.alarms;
    Telemetry out;
    roundTrip(sim, *proto, in, out, link);
    const float mult = begodeVoltageMultiplier(c.model);
    EXPECT_STREQ(out.model, c.model);
    EXPECT_NEAR(out.voltage, c.voltage, 0.01f * mult) << c.model;
    EXPECT_NEAR(out.speedKmh, c.speed, 0.04f) << c.model;
    EXPECT_NEAR(out.tripKm, c.trip, 0.001f) << c.model;
    EXPECT_NEAR(out.current, c.current, kBegodeCurrentScale) << c.model;
    EXPECT_NEAR(out.tempMos, c.temp, 0.01f) << c.model;
    EXPECT_NEAR(out.totalKm, c.total, 0.001f) << c.model;
    EXPECT_EQ(out.rideMode, c.rideMode) << c.model;
    EXPECT_EQ(out.alarmBits, c.alarms) << c.model;
    EXPECT_TRUE(out.has(F_BATTERY));
    EXPECT_FALSE(out.has(F_PWM));
  }
}

TEST(BegodeProtocol, CommandsEncodeTheDocumentedBytes) {
  BegodeProtocol p;
  FakeLink link;
  auto one = [&](const Command c, const int v) {
    link.writes.clear();
    const bool ok = p.send(c, v, link);
    std::string s;
    for (const auto& w : link.writes) {
      s.append(w.begin(), w.end());
    }
    return ok ? s : std::string("REFUSED");
  };
  EXPECT_EQ(one(Command::Beep, 0), "b");
  EXPECT_EQ(one(Command::Lights, 1), "Q");
  EXPECT_EQ(one(Command::Lights, 0), "E");
  EXPECT_EQ(one(Command::Lights, 3), "T");
  EXPECT_EQ(one(Command::Lights, 2), "REFUSED");  // no auto headlight command
  EXPECT_EQ(one(Command::PedalMode, 0), "s");
  EXPECT_EQ(one(Command::PedalMode, 1), "f");
  EXPECT_EQ(one(Command::PedalMode, 2), "h");
  EXPECT_EQ(one(Command::PedalMode, 3), "REFUSED");
  EXPECT_EQ(one(Command::Miles, 0), "g");
  EXPECT_EQ(one(Command::Miles, 1), "m");
  EXPECT_EQ(one(Command::Miles, 2), "REFUSED");
  EXPECT_EQ(one(Command::RideLeds, 0), "WM0");
  EXPECT_EQ(one(Command::RideLeds, 4), "WM4");
  EXPECT_EQ(one(Command::RideLeds, 5), "REFUSED");
  EXPECT_EQ(one(Command::RequestInfo, 0), "NV");
  // Each byte of a multi-write command is its own GATT write (WheelDash sends 'W', 'M', digit separately).
  link.writes.clear();
  p.send(Command::RideLeds, 2, link);
  EXPECT_EQ(link.writes.size(), 3u);
  // Unsupported here: horn, volume, reset trip.
  EXPECT_FALSE(p.supports(Command::Horn));
  EXPECT_FALSE(p.supports(Command::Volume));
  EXPECT_FALSE(p.supports(Command::ResetTrip));
  EXPECT_EQ(one(Command::Horn, 0), "REFUSED");
  EXPECT_EQ(one(Command::Volume, 5), "REFUSED");
  EXPECT_EQ(one(Command::ResetTrip, 0), "REFUSED");
  // Nothing ever writes the dangerous bytes.
  for (const auto& w : link.writes) {
    for (const uint8_t b : w) {
      EXPECT_NE(b, '!');
      EXPECT_NE(b, '@');
      EXPECT_NE(b, '"');
      EXPECT_NE(b, 'c');
    }
  }
}

TEST(BegodeProtocol, ProfileAndFactory) {
  auto p = makeBegode();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->brand(), Brand::Begode);
  EXPECT_STREQ(p->profile().notifyService, "0000ffe0-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().notifyChar, "0000ffe1-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().writeChar, p->profile().notifyChar);
  EXPECT_FALSE(p->profile().writeWithResponse);
  auto s = makeBegodeSim();
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->brand(), Brand::Begode);
}

}  // namespace euc
