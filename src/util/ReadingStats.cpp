#include "ReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>

#include <ctime>

namespace readingstats {
namespace {

readinglog::Day localDay(const int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm tmv{};
  localtime_r(&t, &tmv);
  return readinglog::Day{tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, (tmv.tm_wday + 6) % 7};
}

}  // namespace

void addSession(const std::string& path, const int64_t start, const uint32_t seconds, const uint16_t progressBp) {
  if (start <= 0 || seconds == 0 || !readinglog::isBook(path)) return;
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file = Storage.open(readinglog::LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("RST", "Could not open the reading log");
    return;
  }
  const std::string line = readinglog::toLine({start, seconds, progressBp, path}) + "\n";
  file.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  file.close();
}

readinglog::Stats load(const int64_t now, const std::string& currentBook) {
  const auto lines = [](const std::function<bool(std::string_view)>& line) {
    HalFile file;
    if (!Storage.exists(readinglog::LOG_PATH) || !Storage.openFileForRead("RST", readinglog::LOG_PATH, file)) return;
    std::string current;
    uint8_t buf[512];
    int n = 0;
    while ((n = file.read(buf, sizeof(buf))) > 0) {
      for (int i = 0; i < n; ++i) {
        if (buf[i] == '\n') {
          if (!line(current)) return;
          current.clear();
        } else if (current.size() < 600) {
          current += static_cast<char>(buf[i]);
        }
      }
    }
    if (!current.empty()) line(current);
  };
  return readinglog::compute(lines, now, localDay, readinglog::isBook(currentBook) ? currentBook : std::string());
}

}  // namespace readingstats
