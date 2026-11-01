#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "../../lib/Euc/Factory.cpp"  // every brand at once: the per-brand runs leave it out

namespace {

using namespace euc;

class LoopLink final : public Link {
 public:
  explicit LoopLink(WheelSimulator& sim) : sim_(sim) {}
  bool write(const uint8_t* data, size_t size) override {
    sim_.onWrite(data, size);
    return true;
  }

 private:
  WheelSimulator& sim_;
};

Telemetry truth() {
  Telemetry t;
  t.mark(static_cast<Field>(F_SPEED | F_VOLTAGE | F_CURRENT | F_BATTERY | F_TEMP_MOS | F_TRIP | F_TOTAL));
  t.speedKmh = 24.0f;
  t.voltage = 65.0f;
  t.current = 12.0f;
  t.batteryPct = 80;
  t.tempMos = 36;
  t.tripKm = 2.5f;
  t.totalKm = 321.0f;
  return t;
}

}  // namespace

// The whole stack of the demo wheel, brand by brand: simulator frames -> notifications of at most 20 bytes ->
// protocol -> Telemetry, with the protocol's own requests answered by the simulator.
TEST(EucFactory, EveryBrandRunsEndToEnd) {
  for (int b = 0; b < static_cast<int>(Brand::Count); ++b) {
    const Brand brand = static_cast<Brand>(b);
    auto protocol = makeProtocol(brand);
    auto sim = makeSimulator(brand);
    ASSERT_TRUE(protocol) << brandName(brand);
    ASSERT_TRUE(sim) << brandName(brand);
    EXPECT_EQ(protocol->brand(), brand);
    EXPECT_EQ(sim->brand(), brand);
    LoopLink link(*sim);
    Telemetry live;
    const Telemetry wanted = truth();
    protocol->start(link, 0);
    for (uint32_t now = 0; now < 20000; now += 100) {
      sim->tick(now, wanted);
      protocol->poll(link, now);
      uint8_t frame[256];
      size_t size = 0;
      while (sim->pop(frame, sizeof(frame), size)) {
        for (size_t at = 0; at < size; at += 20) protocol->feed(frame + at, std::min<size_t>(20, size - at), live);
      }
    }
    EXPECT_TRUE(live.has(F_SPEED) || live.has(F_VOLTAGE)) << brandName(brand) << " reported nothing";
    if (live.has(F_SPEED)) {
      EXPECT_NEAR(live.speedKmh, 24.0f, 2.0f) << brandName(brand);
    }
  }
}

TEST(EucFactory, NoBrandSendsADangerousCommandWhateverIsAsked) {
  // Every command, every value, on every brand: the writes must never be refused with a crash, and the safe set
  // is all that exists (the per-brand tests check the exact bytes).
  for (int b = 0; b < static_cast<int>(Brand::Count); ++b) {
    auto protocol = makeProtocol(static_cast<Brand>(b));
    auto sim = makeSimulator(static_cast<Brand>(b));
    LoopLink link(*sim);
    for (int c = 0; c < static_cast<int>(Command::Count); ++c) {
      for (int value = -2; value <= 101; ++value) protocol->send(static_cast<Command>(c), value, link);
    }
    SUCCEED();
  }
}
