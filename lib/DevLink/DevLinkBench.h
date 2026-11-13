#pragma once

#include <cstddef>
#include <cstdint>

// The pure parts of the SD bench logger of the developer build (docs/devlink.md): the CSV row, when
// the file is rotated, and when the next row is due. Platform-free and heap-free; the host tests are
// in test/dev_link. Only the x4pro-dev build refers to this file, so no other build links it.
namespace devlink {

struct BenchRow {
  int64_t epoch = 0;  // 0: the clock is not trusted
  uint32_t uptimeMs = 0;
  uint32_t batteryMv = 0;
  uint32_t batteryPercent = 0;
  bool usb = false;
  bool lightOn = false;
  uint32_t lightBrightness = 0;
  uint32_t lightWarmth = 0;
  uint32_t cpuMhz = 0;
  uint32_t heapFree = 0;
  uint32_t heapMin = 0;
  uint32_t psramFree = 0;
  const char* activity = "";  // may be null
  bool wifi = false;
  bool ble = false;
  int wakeCause = -1;
};

// The longest activity name a row keeps; the rest is cut.
constexpr size_t BENCH_ACTIVITY_MAX = 32;
// A buffer of this size holds any row with its line end and terminator.
constexpr size_t BENCH_ROW_MAX = 192;
// bench.csv is moved to bench.old.csv before a row would take it past this.
constexpr uint64_t BENCH_FILE_CAP = 256 * 1024;
// The longest interval CMD:BENCH takes (a day).
constexpr uint32_t BENCH_MAX_MINUTES = 1440;
constexpr uint32_t BENCH_DEFAULT_MINUTES = 5;

// The first line of the file, with its '\n'.
const char* benchHeader();

// Writes one CSV line with its '\n' and a terminator into `out`. Returns the length without the
// terminator, or 0 when `out` is null or too small (nothing usable is written then). The activity
// name is cut to BENCH_ACTIVITY_MAX bytes; a comma, a quote, a control byte or a byte over 126 in it
// becomes '_', so a row always has the same number of fields.
size_t formatBenchRow(const BenchRow& row, char* out, size_t capacity);

enum class BenchFileAction : uint8_t {
  Append,  // the row fits under the cap
  Rotate,  // move the file to bench.old.csv first and start a new one with the header
};
// fileBytes: the size of bench.csv now (0 when it is not there).
BenchFileAction benchFileAction(uint64_t fileBytes, size_t rowBytes);

// True when a row is due. minutes 0 is off. With no row yet this boot (hasLast false) one is due at
// once. The subtraction is unsigned, so it stays right when millis() wraps after 49.7 days.
bool benchDue(uint32_t nowMs, uint32_t lastMs, bool hasLast, uint32_t minutes);

// Reads the argument of CMD:BENCH: a whole number of minutes from 0 (off) to BENCH_MAX_MINUTES.
// Blanks and a line end after the number are allowed (the saved file ends in one).
bool parseBenchMinutes(const char* text, size_t length, uint32_t& minutes);

}  // namespace devlink
