#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "BatteryCurve.h"
#include "Kingsong.h"
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

bool feedChunked(Protocol& p, const std::vector<uint8_t>& frame, const size_t chunk, Telemetry& t) {
  bool changed = false;
  for (size_t i = 0; i < frame.size(); i += chunk) {
    const size_t n = std::min(chunk, frame.size() - i);
    changed = p.feed(frame.data() + i, n, t) || changed;
  }
  return changed;
}

std::vector<uint8_t> request(const uint8_t type) {
  std::vector<uint8_t> f(20, 0);
  f[0] = 0xAA;
  f[1] = 0x55;
  f[16] = type;
  f[17] = 0x14;
  f[18] = 0x5A;
  f[19] = 0x5A;
  return f;
}

// Literal frames from the reference repos:
// WheelDash source/BLE/GarminEUCBleDelegate.mc:108-111 (model request, the bytes the Garmin app writes)
const std::vector<uint8_t> kRealModelRequest = {0xaa, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9b, 0x14, 0x5a, 0x5a};
// eucWatch v2/euc/eucKingsong/eucKingsong.js:18 (horn)
const std::vector<uint8_t> kRealHorn = {170, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 136, 20, 90, 90};
// eucWatch eucKingsong.js:77 pedal mode with val = 2 (soft) -- byte 17 is 0x14 there, 0x15 in WheelDash
const std::vector<uint8_t> kEucWatchPedalSoft = {170, 85, 2, 224, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 135, 20, 90, 90};

// A 0xBB name reply in the format both apps parse ("KS-S22-0231", frameDecoder.mc:384-407, eucKingsong.js:363-370).
std::vector<uint8_t> nameFrame(const char* name) {
  std::vector<uint8_t> f = request(0xBB);
  std::copy(name, name + std::strlen(name), f.begin() + 2);
  return f;
}

// Runs one tick of the simulator with the protocol's pending writes answered; delivers frames in `chunk` bytes.
void roundTrip(KingsongSimulator& sim, Protocol& proto, const Telemetry& in, Telemetry& out, FakeLink& link,
               const size_t chunk) {
  for (const auto& w : link.writes) {
    sim.onWrite(w.data(), w.size());
  }
  link.writes.clear();
  sim.tick(0, in);
  uint8_t buf[20];
  size_t n = 0;
  while (sim.pop(buf, sizeof(buf), n)) {
    feedChunked(proto, std::vector<uint8_t>(buf, buf + n), chunk, out);
  }
}

}  // namespace

TEST(KingsongProtocol, StartSendsTheRealRequests) {
  KingsongProtocol p;
  FakeLink link;
  p.start(link, 0);
  ASSERT_EQ(link.writes.size(), 3u);
  EXPECT_EQ(link.writes[0], kRealModelRequest);
  EXPECT_EQ(link.writes[1], request(0x63));
  EXPECT_EQ(link.writes[2], request(0x98));
  // The model request is repeated while no name came, at most 3 times, 2 s apart.
  p.poll(link, 1000);
  EXPECT_EQ(link.writes.size(), 3u);
  p.poll(link, 2000);
  EXPECT_EQ(link.writes.size(), 4u);
  EXPECT_EQ(link.writes[3], kRealModelRequest);
  p.poll(link, 4000);
  p.poll(link, 6000);
  p.poll(link, 8000);
  p.poll(link, 10000);
  EXPECT_EQ(link.writes.size(), 6u);
}

TEST(KingsongProtocol, NameFrameGivesModelFirmwareAndPack) {
  KingsongProtocol p;
  Telemetry t;
  const auto f = nameFrame("KS-S22-0231");
  ASSERT_TRUE(p.feed(f.data(), f.size(), t));
  EXPECT_TRUE(t.has(F_MODEL));
  EXPECT_TRUE(t.has(F_FIRMWARE));
  EXPECT_STREQ(t.model, "KS-S22");
  EXPECT_STREQ(t.firmware, "0231");
  // After the name, live frames use the 126 V curve.
  auto live = request(0xA9);
  live[2] = 0x3C;  // 120.12 V = 0x2EEC LE
  live[2] = 0xEC;
  live[3] = 0x2E;
  ASSERT_TRUE(p.feed(live.data(), live.size(), t));
  EXPECT_NEAR(t.voltage, 120.12f, 0.001f);
  EXPECT_NEAR(t.batteryPct, (120.12f - 99.75f) / 0.255f, 0.05f);
  // Model names without a dash are taken whole.
  KingsongProtocol p2;
  Telemetry t2;
  const auto f2 = nameFrame("ROCKWHEEL");
  ASSERT_TRUE(p2.feed(f2.data(), f2.size(), t2));
  EXPECT_STREQ(t2.model, "ROCKWHEEL");
  EXPECT_FALSE(t2.has(F_FIRMWARE));
}

TEST(KingsongProtocol, PackVoltsTable) {
  EXPECT_FLOAT_EQ(kingsongPackVolts("KS-18L"), 84.0f);
  EXPECT_FLOAT_EQ(kingsongPackVolts("KS-S18"), 84.0f);
  EXPECT_FLOAT_EQ(kingsongPackVolts("KS-S19"), 100.8f);
  EXPECT_FLOAT_EQ(kingsongPackVolts("KS-S20"), 126.0f);
  EXPECT_FLOAT_EQ(kingsongPackVolts("KS-16S"), 67.2f);
  EXPECT_FLOAT_EQ(kingsongPackVolts(""), 67.2f);
}

TEST(KingsongProtocol, LiveFrameFieldsAndWordSwappedTotal) {
  KingsongProtocol p;
  Telemetry t;
  auto f = request(0xA9);
  // 83.21 V, 25.50 km/h, total 1234567 m = 0x0012D687 -> b6 b7 = 0x12 0x00 (high word LE), b8 b9 = 0x87 0xD6
  f[2] = 0x81;
  f[3] = 0x20;
  f[4] = 0xF6;
  f[5] = 0x09;
  f[6] = 0x12;
  f[7] = 0x00;
  f[8] = 0x87;
  f[9] = 0xD6;
  f[10] = 0x30;  // -5.00 A = 0xFE0C LE
  f[10] = 0x0C;
  f[11] = 0xFE;
  f[12] = 0x10;  // 36.00 C = 0x0E10
  f[13] = 0x0E;
  f[14] = 0x01;
  f[15] = 0xE0;
  ASSERT_TRUE(p.feed(f.data(), f.size(), t));
  EXPECT_NEAR(t.voltage, 83.21f, 0.001f);
  EXPECT_NEAR(t.speedKmh, 25.50f, 0.001f);
  EXPECT_NEAR(t.totalKm, 1234.567f, 0.0005f);
  EXPECT_NEAR(t.current, -5.0f, 0.001f);
  EXPECT_NEAR(t.power, 83.21f * -5.0f, 0.1f);
  EXPECT_NEAR(t.tempMos, 36.0f, 0.001f);
  EXPECT_TRUE(t.has(F_RIDE_MODE));
  EXPECT_EQ(t.rideMode, 1);
  EXPECT_FALSE(t.has(F_PWM));
}

TEST(KingsongProtocol, TripCpuLimitsAndAlarmFrames) {
  KingsongProtocol p;
  Telemetry t;
  auto b9 = request(0xB9);
  b9[2] = 0x00;  // trip 12345 m = 0x00003039 -> high word 0x0000, low word 0x3039 LE = 39 30
  b9[3] = 0x00;
  b9[4] = 0x39;
  b9[5] = 0x30;
  b9[6] = 0x10;  // 3600 s = 0x0E10
  b9[7] = 0x0E;
  b9[8] = 0x4C;  // 45.00 km/h = 0x1194
  b9[8] = 0x94;
  b9[9] = 0x11;
  b9[10] = 0x14;  // auto
  b9[13] = 1;     // charging
  b9[14] = 0xD0;  // 50.00 C = 0x1388
  b9[14] = 0x88;
  b9[15] = 0x13;
  ASSERT_TRUE(p.feed(b9.data(), b9.size(), t));
  EXPECT_NEAR(t.tripKm, 12.345f, 0.0005f);
  EXPECT_EQ(t.rideSeconds, 3600u);
  EXPECT_NEAR(t.topSpeedKmh, 45.0f, 0.001f);
  EXPECT_TRUE(t.has(F_LIGHT));
  EXPECT_EQ(t.lightMode, 2);
  EXPECT_TRUE(t.charging);
  EXPECT_NEAR(t.tempMotor, 50.0f, 0.001f);

  auto f5 = request(0xF5);
  f5[15] = 67;
  ASSERT_TRUE(p.feed(f5.data(), f5.size(), t));
  EXPECT_TRUE(t.has(F_PWM));
  EXPECT_FLOAT_EQ(t.pwm, 67.0f);

  auto f6 = request(0xF6);
  f6[14] = 218 & 0xFF;  // overpower
  f6[15] = 0;
  ASSERT_TRUE(p.feed(f6.data(), f6.size(), t));
  EXPECT_EQ(t.faultCode, 218);
  EXPECT_EQ(t.alarmBits, ALARM_HIGH_POWER);
  f6[14] = 232;
  ASSERT_TRUE(p.feed(f6.data(), f6.size(), t));
  EXPECT_EQ(t.alarmBits, ALARM_LIFTED);
  f6[14] = 0;
  ASSERT_TRUE(p.feed(f6.data(), f6.size(), t));
  EXPECT_EQ(t.alarmBits, 0);

  auto b5 = request(0xB5);
  b5[4] = 30;
  b5[6] = 40;
  b5[8] = 45;
  b5[10] = 50;
  EXPECT_FALSE(p.feed(b5.data(), b5.size(), t));  // settings, not telemetry
  EXPECT_TRUE(p.alarms().valid);
  EXPECT_EQ(p.alarms().alarm1, 30);
  EXPECT_EQ(p.alarms().alarm2, 40);
  EXPECT_EQ(p.alarms().alarm3, 45);
  EXPECT_EQ(p.alarms().tiltback, 50);
  auto a4 = request(0xA4);
  a4[4] = 0xFF;  // alarm 1 off
  a4[10] = 55;
  p.feed(a4.data(), a4.size(), t);
  EXPECT_EQ(p.alarms().alarm1, 0);
  EXPECT_EQ(p.alarms().tiltback, 55);
}

TEST(KingsongProtocol, SerialFrame) {
  KingsongProtocol p;
  Telemetry t;
  auto b3 = request(0xB3);
  const char* serial = "KSS22A1B2C3D4E5F6";  // 17 chars: 14 at 2..15, 3 at 17..19
  std::copy(serial, serial + 14, b3.begin() + 2);
  std::copy(serial + 14, serial + 17, b3.begin() + 17);
  ASSERT_TRUE(p.feed(b3.data(), b3.size(), t));
  EXPECT_TRUE(t.has(F_SERIAL));
  EXPECT_STREQ(t.serial, serial);
}

TEST(KingsongProtocol, IgnoredFrames) {
  KingsongProtocol p;
  Telemetry t;
  for (const uint8_t type : {0xF1, 0xF2, 0xE1, 0xE2, 0xE5, 0xE6, 0x5F, 0x00}) {
    auto f = request(type);
    f[2] = 1;
    EXPECT_FALSE(p.feed(f.data(), f.size(), t)) << "type " << static_cast<int>(type);
  }
  // 0xBC frames (eucKingsong.js:157) never match the AA 55 header.
  std::vector<uint8_t> bc(20, 0xBC);
  EXPECT_FALSE(p.feed(bc.data(), bc.size(), t));
  EXPECT_EQ(t.valid, 0u);
}

TEST(KingsongProtocol, EveryChunkBoundaryAndGarbage) {
  auto live = request(0xA9);
  live[2] = 0x10;
  live[3] = 0x27;  // 100.00 V
  for (size_t chunk = 1; chunk <= 20; ++chunk) {
    KingsongProtocol p;
    Telemetry t;
    std::vector<uint8_t> stream = {0x00, 0xAA, 0x01, 0xAA, 0xAA};  // noise with false starts
    stream.insert(stream.end(), live.begin(), live.end());
    stream.insert(stream.end(), live.begin(), live.begin() + 7);  // truncated second frame
    feedChunked(p, stream, chunk, t);
    EXPECT_NEAR(t.voltage, 100.0f, 0.001f) << "chunk " << chunk;
  }
  // After a truncated frame the decoder resyncs within one frame length (it needs the next AA 55).
  KingsongProtocol p;
  Telemetry t;
  EXPECT_FALSE(p.feed(live.data(), 7, t));
  EXPECT_FALSE(p.feed(nullptr, 0, t));
  std::vector<uint8_t> twice = live;
  twice.insert(twice.end(), live.begin(), live.end());
  EXPECT_TRUE(p.feed(twice.data(), twice.size(), t));
  EXPECT_NEAR(t.voltage, 100.0f, 0.001f);
}

TEST(KingsongProtocol, RoundTripThroughSimulator) {
  struct Case {
    const char* name;
    float voltage, speed, total, current, temp, trip, top, motorTemp, pwm;
    uint32_t rideSeconds;
    uint8_t rideMode, light;
    bool charging;
    uint16_t fault;
  };
  const Case cases[] = {
      {"KS-S22-0231", 120.12f, 0.0f, 1234.567f, -5.0f, 36.0f, 12.345f, 45.0f, 50.0f, 67.0f, 3600, 1, 2, true, 0},
      {"KS-18L-1.07", 83.5f, 49.99f, 0.001f, 60.0f, 70.5f, 0.0f, 50.0f, 80.0f, 100.0f, 65535, 0, 1, false, 218},
      // temperatures are u16 in the frame (frameDecoder.mc:371, eucKingsong.js:233): no negatives
      {"KS-16S-2.00", 60.0f, 12.3f, 99999.999f, 0.01f, 1.0f, 999.999f, 1.0f, 0.0f, 0.0f, 0, 2, 0, false, 232},
      {"KS-S19-0100", 100.8f, 30.0f, 500.5f, 20.2f, 40.4f, 60.6f, 70.7f, 45.0f, 5.0f, 60, 1, 1, true, 202},
  };
  for (const Case& c : cases) {
    for (const size_t chunk : {size_t{20}, size_t{7}}) {
      KingsongSimulator sim;
      sim.setName(c.name);
      KingsongProtocol proto;
      FakeLink link;
      proto.start(link, 0);
      Telemetry in;
      in.voltage = c.voltage;
      in.speedKmh = c.speed;
      in.totalKm = c.total;
      in.current = c.current;
      in.tempMos = c.temp;
      in.tripKm = c.trip;
      in.topSpeedKmh = c.top;
      in.tempMotor = c.motorTemp;
      in.pwm = c.pwm;
      in.rideSeconds = c.rideSeconds;
      in.rideMode = c.rideMode;
      in.lightMode = c.light;
      in.charging = c.charging;
      in.faultCode = c.fault;
      Telemetry out;
      roundTrip(sim, proto, in, out, link, chunk);
      const std::string name(c.name);
      EXPECT_EQ(out.model, name.substr(0, name.rfind('-'))) << c.name;
      EXPECT_EQ(out.firmware, name.substr(name.rfind('-') + 1)) << c.name;
      EXPECT_TRUE(out.has(F_SERIAL));
      EXPECT_STREQ(out.serial, "KSS22A1B2C3D4E5F6");
      EXPECT_NEAR(out.voltage, c.voltage, 0.005f) << c.name;
      EXPECT_NEAR(out.speedKmh, c.speed, 0.005f) << c.name;
      EXPECT_NEAR(out.totalKm, c.total, 0.0005f) << c.name;
      EXPECT_NEAR(out.current, c.current, 0.005f) << c.name;
      EXPECT_NEAR(out.tempMos, c.temp, 0.005f) << c.name;
      EXPECT_NEAR(out.tripKm, c.trip, 0.0005f) << c.name;
      EXPECT_NEAR(out.topSpeedKmh, c.top, 0.005f) << c.name;
      EXPECT_NEAR(out.tempMotor, c.motorTemp, 0.005f) << c.name;
      EXPECT_NEAR(out.pwm, c.pwm, 0.5f) << c.name;
      EXPECT_EQ(out.rideSeconds, c.rideSeconds) << c.name;
      EXPECT_EQ(out.rideMode, c.rideMode) << c.name;
      EXPECT_EQ(out.lightMode, c.light) << c.name;
      EXPECT_EQ(out.charging, c.charging) << c.name;
      EXPECT_EQ(out.faultCode, c.fault) << c.name;
      EXPECT_TRUE(out.has(F_BATTERY));
      EXPECT_NEAR(out.batteryPct, batteryPercentFromVoltage(out.voltage, kingsongPackVolts(out.model)), 0.01f)
          << c.name;
      EXPECT_TRUE(proto.alarms().valid);
    }
  }
}

TEST(KingsongProtocol, CommandsEncodeTheDocumentedFrames) {
  KingsongProtocol p;
  FakeLink link;
  auto one = [&](const Command c, const int v) {
    link.writes.clear();
    return p.send(c, v, link);
  };
  ASSERT_TRUE(one(Command::Horn, 0));
  EXPECT_EQ(link.writes.at(0), kRealHorn);
  ASSERT_TRUE(one(Command::Beep, 0));
  EXPECT_EQ(link.writes.at(0), request(0x7C));

  auto lights = request(0x73);
  lights[3] = 1;
  ASSERT_TRUE(one(Command::Lights, 1));
  lights[2] = 0x12;  // on
  EXPECT_EQ(link.writes.at(0), lights);
  ASSERT_TRUE(one(Command::Lights, 0));
  lights[2] = 0x13;  // off
  EXPECT_EQ(link.writes.at(0), lights);
  ASSERT_TRUE(one(Command::Lights, 2));
  lights[2] = 0x14;  // auto
  EXPECT_EQ(link.writes.at(0), lights);
  ASSERT_TRUE(one(Command::Lights, 3));
  auto strobe = request(0x53);
  strobe[2] = 1;
  EXPECT_EQ(link.writes.at(0), strobe);
  EXPECT_FALSE(one(Command::Lights, 4));

  auto leds = request(0x6C);
  leds[2] = 1;
  ASSERT_TRUE(one(Command::RideLeds, 1));
  EXPECT_EQ(link.writes.at(0), leds);
  EXPECT_FALSE(one(Command::RideLeds, 2));

  // Pedal: Command 0 soft -> wheel 2; byte 3 = 0xE0, byte 17 = kKingsongPedalByte17 (WheelDash / WheelLog).
  ASSERT_TRUE(one(Command::PedalMode, 0));
  auto pedal = kEucWatchPedalSoft;
  pedal[17] = kKingsongPedalByte17;
  EXPECT_EQ(link.writes.at(0), pedal);
  ASSERT_TRUE(one(Command::PedalMode, 2));
  EXPECT_EQ(link.writes.at(0).at(2), 0);  // hard
  EXPECT_EQ(link.writes.at(0).at(3), 0xE0);
  EXPECT_FALSE(one(Command::PedalMode, 3));
  EXPECT_FALSE(one(Command::PedalMode, -1));

  ASSERT_TRUE(one(Command::RequestInfo, 0));
  ASSERT_EQ(link.writes.size(), 3u);
  EXPECT_EQ(link.writes[0], kRealModelRequest);

  EXPECT_FALSE(p.supports(Command::Volume));
  EXPECT_FALSE(p.supports(Command::ResetTrip));
  EXPECT_FALSE(p.supports(Command::Miles));
  EXPECT_FALSE(one(Command::Volume, 50));
  EXPECT_FALSE(one(Command::ResetTrip, 0));
  EXPECT_FALSE(one(Command::Miles, 1));
}

TEST(KingsongProtocol, NeverWritesForbiddenTypes) {
  // Lock, password, power-off, calibration, factory reset, firmware update, alarm write: never in any write.
  const uint8_t forbidden[] = {0x5D, 0x47, 0x41, 0x42, 0x44, 0x5F, 0x40, 0x89, 0x65, 0x66, 0x67, 0x70, 0x85};
  KingsongProtocol p;
  FakeLink link;
  p.start(link, 0);
  for (int c = 0; c < static_cast<int>(Command::Count); ++c) {
    for (int v = -1; v <= 5; ++v) {
      p.send(static_cast<Command>(c), v, link);
    }
  }
  for (uint32_t t = 0; t < 20000; t += 500) {
    p.poll(link, t);
  }
  for (const auto& w : link.writes) {
    ASSERT_EQ(w.size(), 20u);
    EXPECT_EQ(std::count(std::begin(forbidden), std::end(forbidden), w[16]), 0) << "type " << static_cast<int>(w[16]);
  }
}

TEST(KingsongProtocol, ProfileAndFactory) {
  auto p = makeKingsong();
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->brand(), Brand::Kingsong);
  EXPECT_STREQ(p->profile().notifyService, "0000ffe0-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().notifyChar, "0000ffe1-0000-1000-8000-00805f9b34fb");
  EXPECT_STREQ(p->profile().scanName, "KS");
  auto s = makeKingsongSim();
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->brand(), Brand::Kingsong);
}

}  // namespace euc
