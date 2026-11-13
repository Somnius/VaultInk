#include "DevLinkBench.h"

#include <cstdio>

#include "DevLink.h"

namespace devlink {

const char* benchHeader() {
  return "epoch,uptime_ms,battery_mv,battery_percent,usb,light_on,light_brightness,light_warmth,cpu_mhz,heap_free,"
         "heap_min,psram_free,activity,wifi,ble,wake_cause\n";
}

size_t formatBenchRow(const BenchRow& row, char* out, const size_t capacity) {
  if (!out || capacity == 0) return 0;
  char activity[BENCH_ACTIVITY_MAX + 1];
  size_t used = 0;
  for (const char* at = row.activity; at && *at && used < BENCH_ACTIVITY_MAX; ++at) {
    const unsigned char c = static_cast<unsigned char>(*at);
    activity[used++] = (c < 0x20 || c > 126 || c == ',' || c == '"') ? '_' : static_cast<char>(c);
  }
  activity[used] = '\0';

  const int written =
      snprintf(out, capacity, "%lld,%lu,%lu,%lu,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%s,%d,%d,%d\n",
               static_cast<long long>(row.epoch), static_cast<unsigned long>(row.uptimeMs),
               static_cast<unsigned long>(row.batteryMv), static_cast<unsigned long>(row.batteryPercent),
               row.usb ? 1 : 0, row.lightOn ? 1 : 0, static_cast<unsigned long>(row.lightBrightness),
               static_cast<unsigned long>(row.lightWarmth), static_cast<unsigned long>(row.cpuMhz),
               static_cast<unsigned long>(row.heapFree), static_cast<unsigned long>(row.heapMin),
               static_cast<unsigned long>(row.psramFree), activity, row.wifi ? 1 : 0, row.ble ? 1 : 0, row.wakeCause);
  if (written <= 0 || static_cast<size_t>(written) >= capacity) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

BenchFileAction benchFileAction(const uint64_t fileBytes, const size_t rowBytes) {
  if (fileBytes == 0) return BenchFileAction::Append;
  // Written without a sum, so a huge size cannot wrap round to "fits".
  const bool over = fileBytes >= BENCH_FILE_CAP || rowBytes > BENCH_FILE_CAP - fileBytes;
  return over ? BenchFileAction::Rotate : BenchFileAction::Append;
}

bool benchDue(const uint32_t nowMs, const uint32_t lastMs, const bool hasLast, const uint32_t minutes) {
  if (minutes == 0) return false;
  if (!hasLast) return true;
  const uint32_t capped = minutes > BENCH_MAX_MINUTES ? BENCH_MAX_MINUTES : minutes;
  return static_cast<uint32_t>(nowMs - lastMs) >= capped * 60000u;
}

bool parseBenchMinutes(const char* text, size_t length, uint32_t& minutes) {
  if (!text) return false;
  while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r' ||
                        text[length - 1] == '\n')) {
    --length;
  }
  uint32_t value = 0;
  if (!parseUnsigned(text, length, value) || value > BENCH_MAX_MINUTES) return false;
  minutes = value;
  return true;
}

}  // namespace devlink
