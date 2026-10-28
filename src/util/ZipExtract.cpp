#include "ZipExtract.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ZipFile.h>

#include <algorithm>
#include <vector>

#include "util/TaskWatchdog.h"

namespace zipextract {
namespace {

// Streams an entry into a file and counts the bytes; every chunk feeds the watchdog.
class FileSink final : public Print {
 public:
  explicit FileSink(HalFile& file) : file(file) {}
  size_t write(const uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, const size_t len) override {
    resetTaskWatchdogIfSubscribed();
    const size_t n = file.write(data, len);
    if (n != len) failed = true;
    return n;
  }
  bool failed = false;

 private:
  HalFile& file;
};

}  // namespace

std::string safeEntryPath(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  if (path.empty() || path.front() == '/' || path.back() == '/') return "";
  std::string out;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    const std::string part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == ".." || part == "__MACOSX" || !FsHelpers::isSafePathComponent(part)) {
      return "";
    }
    if (std::any_of(part.begin(), part.end(), [](const char c) {
          return static_cast<unsigned char>(c) < 0x20 || std::string_view(":*?\"<>|").find(c) != std::string_view::npos;
        })) {
      return "";
    }
    if (!out.empty()) out += '/';
    out += part;
    start = end + 1;
  }
  return out;
}

Result extractHere(const std::string& zipPath, std::string& outFolder, Stats& stats) {
  const size_t dot = zipPath.find_last_of('.');
  outFolder = zipPath.substr(0, dot);
  if (Storage.exists(outFolder.c_str())) return Result::Exists;

  ZipFile zip(zipPath);
  std::vector<std::string> entries;
  if (!zip.enumerateFilePaths([&](std::string_view p) { entries.emplace_back(p); })) return Result::BadZip;
  if (!zip.open()) return Result::BadZip;
  if (!Storage.mkdir(outFolder.c_str())) {
    zip.close();
    return Result::Failed;
  }

  bool ok = true;
  for (const auto& entry : entries) {
    if (!entry.empty() && entry.back() == '/') continue;  // a folder: made with its files
    const std::string rel = safeEntryPath(entry);
    if (rel.empty()) {
      ++stats.skipped;
      continue;
    }
    const std::string target = outFolder + "/" + rel;
    const size_t slash = target.find_last_of('/');
    Storage.ensureDirectoryExists(target.substr(0, slash).c_str());
    if (Storage.exists(target.c_str())) {  // two entries with one name (case on FAT): keep the first
      ++stats.skipped;
      continue;
    }
    const std::string part = target + ".part";
    HalFile file;
    if (!Storage.openFileForWrite("ZIP", part, file)) {
      ok = false;
      break;
    }
    FileSink sink(file);
    const bool streamed = zip.readFileToStream(entry.c_str(), sink, 4096);
    file.close();
    if (!streamed || sink.failed || !Storage.rename(part.c_str(), target.c_str())) {
      LOG_ERR("ZIP", "Could not extract %s", entry.c_str());
      Storage.remove(part.c_str());
      ok = false;
      break;
    }
    ++stats.files;
  }
  zip.close();
  if (!ok) {
    Storage.removeDir(outFolder.c_str());  // nothing half-extracted stays behind
    return Result::Failed;
  }
  return Result::Ok;
}

}  // namespace zipextract
