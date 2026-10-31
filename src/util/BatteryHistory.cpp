#include "BatteryHistory.h"

#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <algorithm>
#include <string>
#include <vector>

namespace batteryhistory {
namespace {

std::vector<batterylog::Record> readAll() {
  std::vector<batterylog::Record> records;
  HalFile file;
  if (!Storage.exists(batterylog::LOG_PATH) || !Storage.openFileForRead("BAT", batterylog::LOG_PATH, file)) {
    return records;
  }
  std::string current;
  uint8_t buf[256];
  int n = 0;
  const auto flushLine = [&]() {
    batterylog::Record record;
    if (batterylog::fromLine(current, record)) batterylog::append(records, record);
    current.clear();
  };
  while ((n = file.read(buf, sizeof(buf))) > 0) {
    for (int i = 0; i < n; ++i) {
      if (buf[i] == '\n') {
        flushLine();
      } else if (current.size() < 64) {
        current += static_cast<char>(buf[i]);
      }
    }
  }
  if (!current.empty()) flushLine();
  return records;
}

void add(const bool sleep) {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return;  // an undated line could not be paired
  batterylog::Record record;
  record.epoch = now;
  record.sleep = sleep;
  record.percent =
      static_cast<uint8_t>(std::min<unsigned>(powerManager.getBatteryPercentage(), 100));  // fromLine refuses above 100
  record.usb = gpio.isUsbConnected();

  auto records = readAll();
  if (!sleep && (records.empty() || !records.back().sleep)) return;
  const bool rewrite = records.size() >= batterylog::MAX_RECORDS;
  batterylog::append(records, record);
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file =
      Storage.open(batterylog::LOG_PATH, rewrite ? (O_WRONLY | O_CREAT | O_TRUNC) : (O_WRONLY | O_CREAT | O_APPEND));
  if (!file) {
    LOG_ERR("BAT", "Could not open the battery log");
    return;
  }
  std::string text;
  if (rewrite) {
    for (const auto& r : records) text += batterylog::toLine(r) + "\n";
  } else {
    text = batterylog::toLine(record) + "\n";
  }
  file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  file.close();
}

}  // namespace

void noteSleep() { add(true); }
void noteWake() { add(false); }
batterylog::Stats load() { return batterylog::compute(readAll()); }

}  // namespace batteryhistory
