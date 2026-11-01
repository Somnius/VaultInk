#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "BatteryCurve.h"
#include "Protocols.h"
#include "Veteran.h"

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

bool feedChunked(Protocol& p, const std::vector<uint8_t>& frame, const size_t chunk, Telemetry& t) {
  bool changed = false;
  for (size_t i = 0; i < frame.size(); i += chunk) {
    const size_t n = std::min(chunk, frame.size() - i);
    changed = p.feed(frame.data() + i, n, t) || changed;
  }
  return changed;
}

// Sherman S, eucWatch v2/euc/eucVeteran/info.txt lines 3-5: three notifications (20 + 20 + 11) of one 51-byte frame
// with payload length 0x2F = 47 and a trailing CRC32.
const std::vector<uint8_t> kReal47 = {0xdc, 0x5a, 0x5c, 0x2f, 0x22, 0xd3, 0x00, 0x00, 0xaa, 0x7e, 0x00, 0x01, 0x42,
                                      0x18, 0x00, 0x12, 0x00, 0x00, 0x11, 0x49, 0x0a, 0xd7, 0x00, 0x00, 0x02, 0x46,
                                      0x0a, 0xf0, 0x0b, 0xc4, 0x00, 0x01, 0x00, 0x06, 0x00, 0x30, 0x00, 0x6f, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xcd, 0x89, 0xf1, 0xfa};
// info.txt lines 6-8: payload length 0x35 = 53 (20 + 20 + 17), the next frame kind of the same wheel.
const std::vector<uint8_t> kReal53 = {
    0xdc, 0x5a, 0x5c, 0x35, 0x22, 0xd4, 0x00, 0x00, 0xaa, 0x7e, 0x00, 0x01, 0x42, 0x18, 0x00, 0x12, 0x00, 0x01, 0x11,
    0x4b, 0x0a, 0xd7, 0x00, 0x00, 0x02, 0x46, 0x0a, 0xf0, 0x0b, 0xc4, 0x00, 0x01, 0x00, 0x06, 0x00, 0x30, 0x00, 0x6f,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x56, 0xa0, 0xbb, 0x89};

}  // namespace

TEST(VeteranProtocol, Crc32MatchesTheRealDumps) {
  // The last 4 bytes of each real frame are the CRC32 of the first `len` bytes (frameDecoder.mc:210-219).
  EXPECT_EQ(veteranCrc32(kReal47.data(), 47), 0xcd89f1fau);
  EXPECT_EQ(veteranCrc32(kReal53.data(), 53), 0x56a0bb89u);
  // Known vector: CRC32("123456789") = CBF43926
  const char* s = "123456789";
  EXPECT_EQ(veteranCrc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
}

TEST(VeteranProtocol, DecodesRealFrame47) {
  VeteranProtocol p;
  Telemetry t;
  ASSERT_TRUE(feedChunked(p, kReal47, 20, t));
  EXPECT_NEAR(t.voltage, 89.15f, 0.001f);
  EXPECT_NEAR(t.speedKmh, 0.0f, 0.001f);
  EXPECT_NEAR(t.tripKm, 109.182f, 0.0005f);
  EXPECT_NEAR(t.totalKm, 1196.568f, 0.0005f);
  EXPECT_NEAR(t.current, 0.0f, 0.001f);
  EXPECT_NEAR(t.tempMos, 44.25f, 0.001f);
  EXPECT_STREQ(t.firmware, "3.012");
  EXPECT_EQ(t.rideMode, 1);
  EXPECT_NEAR(t.pwm, 0.48f, 0.001f);
  EXPECT_TRUE(t.has(F_PWM));
  EXPECT_TRUE(t.has(F_BATTERY));
  // 89.15 V on the 100.8 V curve (firmware 3.012 < 4): (89.15 - 80.7) / 0.195
  EXPECT_NEAR(t.batteryPct, (89.15f - 80.7f) / 0.195f, 0.05f);
}

TEST(VeteranProtocol, DecodesRealFrame53) {
  VeteranProtocol p;
  Telemetry t;
  ASSERT_TRUE(feedChunked(p, kReal53, 20, t));
  EXPECT_NEAR(t.voltage, 89.16f, 0.001f);
  EXPECT_NEAR(t.current, 1 * kVeteranCurrentScale, 0.001f);
  EXPECT_NEAR(t.tempMos, 44.27f, 0.001f);
  EXPECT_NEAR(t.totalKm, 1196.568f, 0.0005f);
}

TEST(VeteranProtocol, EveryChunkBoundaryAcrossTwoFrames) {
  std::vector<uint8_t> both = kReal47;
  both.insert(both.end(), kReal53.begin(), kReal53.end());
  for (size_t chunk = 1; chunk <= both.size(); ++chunk) {
    VeteranProtocol p;
    Telemetry t;
    int frames = 0;
    for (size_t i = 0; i < both.size(); i += chunk) {
      const size_t n = std::min(chunk, both.size() - i);
      frames += p.feed(both.data() + i, n, t) ? 1 : 0;
    }
    EXPECT_GE(frames, 1) << "chunk " << chunk;
    EXPECT_NEAR(t.voltage, 89.16f, 0.001f) << "chunk " << chunk;
  }
}

TEST(VeteranProtocol, BadCrcIsRejectedAndLaterFramesStillDecode) {
  VeteranProtocol p;
  Telemetry t;
  std::vector<uint8_t> bad = kReal47;
  bad[4] = 0x23;  // voltage byte changed, CRC now wrong
  EXPECT_FALSE(feedChunked(p, bad, 20, t));
  EXPECT_EQ(t.valid, 0u);
  bad = kReal47;
  bad[50] ^= 0x01;  // CRC itself changed
  EXPECT_FALSE(feedChunked(p, bad, 20, t));
  EXPECT_TRUE(feedChunked(p, kReal47, 20, t));
  EXPECT_NEAR(t.voltage, 89.15f, 0.001f);
}

TEST(VeteranProtocol, GarbageAndTruncatedInput) {
  VeteranProtocol p;
  Telemetry t;
  const uint8_t garbage[] = {0xdc, 0x5a, 0xdc, 0x5a, 0x5c, 0x10, 0xaa, 0xdc, 0x5a, 0x5c, 0xff, 0x00};
  EXPECT_FALSE(p.feed(garbage, sizeof(garbage), t));
  EXPECT_FALSE(p.feed(nullptr, 0, t));
  EXPECT_FALSE(p.feed(kReal47.data(), 40, t));  // truncated, then abandoned
  // A new header resyncs: truncated frame garbage followed by a complete one.
  std::vector<uint8_t> resync(kReal53.begin(), kReal53.begin() + 30);
  resync.insert(resync.end(), kReal47.begin(), kReal47.end());
  // The truncated start eats bytes until its (wrong) length is reached, so the first full frame may be lost; the
  // one after must decode.
  feedChunked(p, resync, 20, t);
  EXPECT_TRUE(feedChunked(p, kReal47, 20, t));
  EXPECT_NEAR(t.voltage, 89.15f, 0.001f);
}

TEST(VeteranProtocol, OldFirmwareFrameWithoutCrc) {
  VeteranSimulator sim;
  sim.setNewFormat(false);
  sim.setFirmware(2015);
  VeteranProtocol p;
  Telemetry in;
  in.voltage = 95.5f;
  in.speedKmh = 30.0f;
  in.tripKm = 1.5f;
  in.totalKm = 500.25f;
  in.current = 12.0f;
  in.tempMos = 38.0f;
  in.rideMode = 2;
  in.pwm = 33.0f;
  sim.tick(0, in);
  uint8_t buf[300];
  size_t n = 0;
  ASSERT_TRUE(sim.pop(buf, sizeof(buf), n));
  EXPECT_EQ(n, 36u);
  Telemetry out;
  ASSERT_TRUE(feedChunked(p, std::vector<uint8_t>(buf, buf + n), 20, out));
  EXPECT_NEAR(out.voltage, 95.5f, 0.005f);
  EXPECT_NEAR(out.speedKmh, 30.0f, 0.05f);
  EXPECT_NEAR(out.totalKm, 500.25f, 0.0005f);
  EXPECT_STREQ(out.firmware, "2.015");
  EXPECT_NEAR(out.pwm, 33.0f, 0.005f);
}

TEST(VeteranProtocol, RoundTripThroughSimulator) {
  struct Case {
    uint16_t firmware;
    float voltage, speed, trip, total, current, temp, pwm;
    uint8_t rideMode;
  };
  const Case cases[] = {
      {3012, 89.15f, 0.0f, 109.182f, 1196.568f, 0.0f, 44.25f, 0.48f, 1},
      {3012, 100.8f, 55.5f, 0.001f, 12345.678f, -15.3f, -5.5f, 85.0f, 0},
      {4001, 125.0f, 72.3f, 65.535f, 70000.0f, 150.0f, 80.0f, 99.99f, 2},
      {4100, 100.0f, 10.0f, 3000.0f, 0.0f, 3.3f, 20.0f, 0.0f, 1},
  };
  for (const Case& c : cases) {
    VeteranSimulator sim;
    sim.setFirmware(c.firmware);
    auto proto = makeVeteran();
    FakeLink link;
    proto->start(link, 0);
    EXPECT_TRUE(link.writes.empty());  // nothing to request
    Telemetry in;
    in.voltage = c.voltage;
    in.speedKmh = c.speed;
    in.tripKm = c.trip;
    in.totalKm = c.total;
    in.current = c.current;
    in.tempMos = c.temp;
    in.pwm = c.pwm;
    in.rideMode = c.rideMode;
    sim.tick(0, in);
    uint8_t buf[300];
    size_t n = 0;
    ASSERT_TRUE(sim.pop(buf, sizeof(buf), n));
    EXPECT_EQ(n, 51u);
    Telemetry out;
    ASSERT_TRUE(feedChunked(*proto, std::vector<uint8_t>(buf, buf + n), 20, out)) << c.firmware;
    EXPECT_NEAR(out.voltage, c.voltage, 0.005f);
    EXPECT_NEAR(out.speedKmh, c.speed, 0.05f);
    EXPECT_NEAR(out.tripKm, c.trip, 0.0005f);
    EXPECT_NEAR(out.totalKm, c.total, 0.0005f);
    EXPECT_NEAR(out.current, c.current, kVeteranCurrentScale / 2);
    EXPECT_NEAR(out.tempMos, c.temp, 0.005f);
    EXPECT_NEAR(out.pwm, c.pwm, 0.005f);
    EXPECT_EQ(out.rideMode, c.rideMode);
    EXPECT_TRUE(out.has(F_BATTERY));
    const float pack = c.firmware >= kVeteranPattonFirmware ? 126.0f : 100.8f;
    EXPECT_NEAR(out.batteryPct, batteryPercentFromVoltage(out.voltage, pack), 0.01f);
  }
}

TEST(VeteranProtocol, CommandsEncodeTheDocumentedStrings) {
  VeteranProtocol p;
  FakeLink link;
  auto one = [&](const Command c, const int v) {
    link.writes.clear();
    const bool ok = p.send(c, v, link);
    if (!ok) {
      return std::string("REFUSED");
    }
    EXPECT_EQ(link.writes.size(), 1u);
    return std::string(link.writes[0].begin(), link.writes[0].end());
  };
  EXPECT_EQ(one(Command::Beep, 0), "b");
  EXPECT_EQ(one(Command::Lights, 1), "SetLightON");
  EXPECT_EQ(one(Command::Lights, 0), "SetLightOFF");
  EXPECT_EQ(one(Command::Lights, 2), "REFUSED");
  EXPECT_EQ(one(Command::Lights, 3), "REFUSED");
  EXPECT_EQ(one(Command::PedalMode, 0), "SETs");
  EXPECT_EQ(one(Command::PedalMode, 1), "SETm");
  EXPECT_EQ(one(Command::PedalMode, 2), "SETh");
  EXPECT_EQ(one(Command::PedalMode, 3), "REFUSED");
  EXPECT_EQ(one(Command::ResetTrip, 0), "CLEARMETER");
  EXPECT_FALSE(p.supports(Command::Horn));
  EXPECT_FALSE(p.supports(Command::Volume));
  EXPECT_FALSE(p.supports(Command::Miles));
  EXPECT_FALSE(p.supports(Command::RideLeds));
  EXPECT_FALSE(p.supports(Command::RequestInfo));
  EXPECT_EQ(one(Command::Horn, 0), "REFUSED");
  EXPECT_EQ(one(Command::Volume, 50), "REFUSED");
  EXPECT_EQ(one(Command::RequestInfo, 0), "REFUSED");
  // The simulator sees the text.
  VeteranSimulator sim;
  p.send(Command::PedalMode, 2, link);
  sim.onWrite(link.writes.back().data(), link.writes.back().size());
  EXPECT_STREQ(sim.lastCommand(), "SETh");
}

TEST(VeteranProtocol, ProfileAndFactory) {
  auto p = makeVeteran();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->brand(), Brand::Veteran);
  EXPECT_STREQ(p->profile().notifyService, "0000ffe0-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().writeChar, "0000ffe1-0000-1000-8000-00805f9b34fb");
  auto s = makeVeteranSim();
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->brand(), Brand::Veteran);
}

}  // namespace euc
