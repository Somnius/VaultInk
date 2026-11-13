#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "DevLink.h"
#include "DevLinkBench.h"

// The pure parts of the SD bench logger of the developer build (lib/DevLink/DevLinkBench.h).

namespace {

devlink::BenchRow sampleRow() {
  devlink::BenchRow row;
  row.epoch = 1794571200;
  row.uptimeMs = 300123;
  row.batteryMv = 3987;
  row.batteryPercent = 76;
  row.usb = false;
  row.lightOn = true;
  row.lightBrightness = 40;
  row.lightWarmth = 128;
  row.cpuMhz = 80;
  row.heapFree = 143000;
  row.heapMin = 91000;
  row.psramFree = 6100000;
  row.activity = "Reader";
  row.wifi = false;
  row.ble = true;
  row.wakeCause = 0;
  return row;
}

size_t count(const std::string& text, const char c) {
  return static_cast<size_t>(std::count(text.begin(), text.end(), c));
}

}  // namespace

TEST(Bench, CommandParses) {
  const std::string bare = "CMD:BENCH";
  EXPECT_EQ(devlink::parse(bare.data(), bare.size()).command, devlink::Command::Bench);
  const std::string line = "CMD:BENCH 5";
  const auto parsed = devlink::parse(line.data(), line.size());
  EXPECT_EQ(parsed.command, devlink::Command::Bench);
  EXPECT_EQ(std::string(parsed.args, parsed.argsLength), "5");
}

TEST(Bench, RowHasTheColumnsOfTheHeader) {
  char out[devlink::BENCH_ROW_MAX];
  const size_t length = devlink::formatBenchRow(sampleRow(), out, sizeof(out));
  ASSERT_GT(length, 0u);
  EXPECT_EQ(std::string(out, length), "1794571200,300123,3987,76,0,1,40,128,80,143000,91000,6100000,Reader,0,1,0\n");
  EXPECT_EQ(out[length], '\0');
  const std::string header = devlink::benchHeader();
  EXPECT_EQ(header.back(), '\n');
  EXPECT_EQ(count(header, '\n'), 1u);
  EXPECT_EQ(count(header, ','), count(std::string(out, length), ','));
  EXPECT_EQ(header,
            "epoch,uptime_ms,battery_mv,battery_percent,usb,light_on,light_brightness,light_warmth,cpu_mhz,"
            "heap_free,heap_min,psram_free,activity,wifi,ble,wake_cause\n");
}

TEST(Bench, UntrustedClockAndUnknownWakeCause) {
  devlink::BenchRow row = sampleRow();
  row.epoch = 0;
  row.wakeCause = -1;
  row.activity = nullptr;
  char out[devlink::BENCH_ROW_MAX];
  const size_t length = devlink::formatBenchRow(row, out, sizeof(out));
  EXPECT_EQ(std::string(out, length), "0,300123,3987,76,0,1,40,128,80,143000,91000,6100000,,0,1,-1\n");
}

TEST(Bench, ActivityNameCannotBreakTheRow) {
  devlink::BenchRow row = sampleRow();
  row.activity = "A,b\"c\nd\xCE\xB1 e";
  char out[devlink::BENCH_ROW_MAX];
  size_t length = devlink::formatBenchRow(row, out, sizeof(out));
  std::string text(out, length);
  EXPECT_NE(text.find(",A_b_c_d__ e,"), std::string::npos) << text;
  EXPECT_EQ(count(text, ','), count(devlink::benchHeader(), ','));
  EXPECT_EQ(count(text, '\n'), 1u);

  const std::string longName(200, 'x');
  row.activity = longName.c_str();
  length = devlink::formatBenchRow(row, out, sizeof(out));
  text.assign(out, length);
  EXPECT_NE(text.find("," + std::string(devlink::BENCH_ACTIVITY_MAX, 'x') + ","), std::string::npos);
}

TEST(Bench, TheLargestRowFitsTheBuffer) {
  devlink::BenchRow row;
  row.epoch = INT64_MIN;
  row.uptimeMs = row.batteryMv = row.batteryPercent = row.lightBrightness = row.lightWarmth = UINT32_MAX;
  row.cpuMhz = row.heapFree = row.heapMin = row.psramFree = UINT32_MAX;
  row.usb = row.lightOn = row.wifi = row.ble = true;
  row.wakeCause = INT32_MIN;
  const std::string longName(200, 'W');
  row.activity = longName.c_str();
  char out[devlink::BENCH_ROW_MAX];
  const size_t length = devlink::formatBenchRow(row, out, sizeof(out));
  ASSERT_GT(length, 0u);
  EXPECT_LT(length, sizeof(out));
  EXPECT_EQ(out[length - 1], '\n');
}

TEST(Bench, ABufferThatIsTooSmallGivesNothing) {
  char out[20] = "untouched";
  EXPECT_EQ(devlink::formatBenchRow(sampleRow(), out, sizeof(out)), 0u);
  EXPECT_EQ(out[0], '\0');
  EXPECT_EQ(devlink::formatBenchRow(sampleRow(), nullptr, 100), 0u);
  EXPECT_EQ(devlink::formatBenchRow(sampleRow(), out, 0), 0u);
  // The row without room for its terminator is too small too.
  char full[devlink::BENCH_ROW_MAX];
  const size_t length = devlink::formatBenchRow(sampleRow(), full, sizeof(full));
  char exact[devlink::BENCH_ROW_MAX];
  EXPECT_EQ(devlink::formatBenchRow(sampleRow(), exact, length), 0u);
  EXPECT_EQ(devlink::formatBenchRow(sampleRow(), exact, length + 1), length);
}

TEST(Bench, RotatesBeforeTheCap) {
  using devlink::BenchFileAction;
  EXPECT_EQ(devlink::benchFileAction(0, 80), BenchFileAction::Append);
  EXPECT_EQ(devlink::benchFileAction(1000, 80), BenchFileAction::Append);
  EXPECT_EQ(devlink::benchFileAction(devlink::BENCH_FILE_CAP - 80, 80), BenchFileAction::Append);
  EXPECT_EQ(devlink::benchFileAction(devlink::BENCH_FILE_CAP - 79, 80), BenchFileAction::Rotate);
  EXPECT_EQ(devlink::benchFileAction(devlink::BENCH_FILE_CAP, 80), BenchFileAction::Rotate);
  EXPECT_EQ(devlink::benchFileAction(UINT64_MAX - 10, 80), BenchFileAction::Rotate);  // no wrap back to Append
  EXPECT_EQ(devlink::BENCH_FILE_CAP, 262144u);
}

TEST(Bench, DueAfterTheInterval) {
  EXPECT_FALSE(devlink::benchDue(1000000, 0, false, 0));  // off
  EXPECT_FALSE(devlink::benchDue(1000000, 0, true, 0));
  EXPECT_TRUE(devlink::benchDue(10, 0, false, 5));  // the first row of a boot comes at once
  EXPECT_FALSE(devlink::benchDue(1000 + 299999, 1000, true, 5));
  EXPECT_TRUE(devlink::benchDue(1000 + 300000, 1000, true, 5));
  EXPECT_TRUE(devlink::benchDue(1000 + 60000, 1000, true, 1));
  EXPECT_FALSE(devlink::benchDue(1000, 1000, true, 1));
}

TEST(Bench, DueAcrossTheMillisWrap) {
  const uint32_t last = 0xFFFFFFFFu - 100000u;  // 100,000 ms before the last value of the counter
  EXPECT_FALSE(devlink::benchDue(0xFFFFFFFFu, last, true, 5));
  EXPECT_FALSE(devlink::benchDue(199998u, last, true, 5));  // 299,999 ms after
  EXPECT_TRUE(devlink::benchDue(199999u, last, true, 5));   // 300,000 ms after
  // The longest interval still fits 32 bits of milliseconds.
  EXPECT_FALSE(devlink::benchDue(86399999u, 0, true, devlink::BENCH_MAX_MINUTES));
  EXPECT_TRUE(devlink::benchDue(86400000u, 0, true, devlink::BENCH_MAX_MINUTES));
  EXPECT_TRUE(devlink::benchDue(86400000u, 0, true, 100000));  // a value over the cap counts as the cap
}

TEST(Bench, MinutesArgument) {
  uint32_t minutes = 99;
  const auto read = [&](const std::string& text) {
    return devlink::parseBenchMinutes(text.data(), text.size(), minutes);
  };
  EXPECT_TRUE(read("0"));
  EXPECT_EQ(minutes, 0u);
  EXPECT_TRUE(read("5"));
  EXPECT_EQ(minutes, 5u);
  EXPECT_TRUE(read("1440"));
  EXPECT_EQ(minutes, 1440u);
  EXPECT_TRUE(read("15\n"));  // as the saved file holds it
  EXPECT_EQ(minutes, 15u);
  EXPECT_TRUE(read("7 \r\n"));
  EXPECT_EQ(minutes, 7u);
  minutes = 99;
  for (const char* bad : {"", "\n", "1441", "-1", "5m", "1.5", "five", "99999999999", " 5"}) {
    EXPECT_FALSE(read(bad)) << bad;
    EXPECT_EQ(minutes, 99u);
  }
  EXPECT_FALSE(devlink::parseBenchMinutes(nullptr, 0, minutes));
  EXPECT_EQ(devlink::BENCH_DEFAULT_MINUTES, 5u);
}
