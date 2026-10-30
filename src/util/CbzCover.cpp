#include "CbzCover.h"

#include <CbzPages.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <ZipFile.h>

#include <functional>
#include <vector>

namespace cbzcover {

std::string cachePathFor(const std::string& bookPath) {
  return "/.crosspoint/cbz_" + std::to_string(std::hash<std::string>{}(bookPath));
}

std::string thumbTemplate(const std::string& bookPath) { return cachePathFor(bookPath) + "/thumb_[HEIGHT].bmp"; }

bool generate(const std::string& bookPath, const int height) {
  const std::string cache = cachePathFor(bookPath);
  const std::string thumb = cache + "/thumb_" + std::to_string(height) + ".bmp";
  if (Storage.exists(thumb.c_str())) return true;
  ZipFile zip(bookPath);
  std::vector<std::string> entries;
  if (!zip.enumerateFilePaths([&entries](std::string_view path) { entries.emplace_back(path); })) return false;
  const auto book = cbz::pages(entries);
  if (book.pages.empty()) return false;
  Storage.ensureDirectoryExists(cache.c_str());
  const std::string& first = book.pages.front();
  const std::string image = cache + (FsHelpers::hasJpgExtension(first) ? "/cover.jpg" : "/cover.png");
  if (!zip.open()) return false;
  HalFile out;
  bool ok = Storage.openFileForWrite("CBZ", image, out);
  ok = ok && zip.readFileToStream(first.c_str(), out, 1024);
  out.close();
  zip.close();
  ok = ok && Txt::convertCoverImageToBmp(image, thumb, height);
  Storage.remove(image.c_str());
  if (!ok) {
    Storage.remove(thumb.c_str());
    LOG_ERR("CBZ", "No cover for %s", bookPath.c_str());
  }
  return ok;
}

}  // namespace cbzcover
