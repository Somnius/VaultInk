#include "SearchIndex.h"

#include <HalStorage.h>
#include <Logging.h>
#include <TrigramFilter.h>

#include <cstring>

namespace {
constexpr size_t kHeader = 16;
constexpr uint16_t kVersion = 1;
constexpr size_t kSlot = 4 + trigramfilter::kBytes;

size_t slotAt(const int chapter) { return kHeader + static_cast<size_t>(chapter) * kSlot; }
}  // namespace

SearchIndex::SearchIndex(std::string path, const int chapterCount) : path_(std::move(path)), chapters_(chapterCount) {}

bool SearchIndex::open() {
  if (chapters_ <= 0 || chapters_ > 0xFFFF) return false;
  uint8_t header[kHeader] = {'V', 'S', 'I', 'X'};
  header[4] = kVersion & 0xFF;
  header[5] = kVersion >> 8;
  header[6] = static_cast<uint8_t>(chapters_ & 0xFF);
  header[7] = static_cast<uint8_t>(chapters_ >> 8);
  header[8] = trigramfilter::kBytes & 0xFF;
  header[9] = (trigramfilter::kBytes >> 8) & 0xFF;
  {
    HalFile file;
    uint8_t found[kHeader];
    if (Storage.openFileForRead("SIX", path_, file) && file.read(found, kHeader) == static_cast<int>(kHeader) &&
        std::memcmp(found, header, kHeader) == 0) {
      return true;
    }
  }
  // Missing or for another book layout: an empty one, sized for every chapter so slots can be rewritten in place.
  HalFile file;
  if (!Storage.openFileForWrite("SIX", path_, file)) {
    LOG_ERR("SIX", "cannot create %s", path_.c_str());
    return false;
  }
  bool ok = file.write(header, kHeader) == kHeader;
  uint8_t zeros[512] = {};
  size_t left = static_cast<size_t>(chapters_) * kSlot;
  while (ok && left > 0) {
    const size_t n = left < sizeof(zeros) ? left : sizeof(zeros);
    ok = file.write(zeros, n) == n;
    left -= n;
  }
  file.flush();
  if (!ok) {
    file.close();
    Storage.remove(path_.c_str());
    LOG_ERR("SIX", "cannot fill %s", path_.c_str());
  }
  return ok;
}

bool SearchIndex::read(const int chapter, const uint32_t stamp, uint8_t* bits) const {
  if (chapter < 0 || chapter >= chapters_) return false;
  HalFile file;
  if (!Storage.openFileForRead("SIX", path_, file) || !file.seekSet(slotAt(chapter))) return false;
  uint8_t head[4];
  if (file.read(head, 4) != 4) return false;
  const uint32_t stored = head[0] | (head[1] << 8) | (head[2] << 16) | (static_cast<uint32_t>(head[3]) << 24);
  if (stored == 0 || stored != stamp) return false;
  return file.read(bits, trigramfilter::kBytes) == static_cast<int>(trigramfilter::kBytes);
}

bool SearchIndex::write(const int chapter, const uint32_t stamp, const uint8_t* bits) const {
  if (chapter < 0 || chapter >= chapters_) return false;
  HalFile file = Storage.open(path_.c_str(), O_RDWR);
  if (!file) return false;
  // The filter first and the stamp last: a write cut short leaves the slot marked empty, never half a filter marked
  // valid.
  const uint8_t head[4] = {static_cast<uint8_t>(stamp), static_cast<uint8_t>(stamp >> 8),
                           static_cast<uint8_t>(stamp >> 16), static_cast<uint8_t>(stamp >> 24)};
  const uint8_t empty[4] = {};
  bool ok = file.seekSet(slotAt(chapter)) && file.write(empty, 4) == 4 &&
            file.write(bits, trigramfilter::kBytes) == trigramfilter::kBytes;
  file.flush();
  ok = ok && file.seekSet(slotAt(chapter)) && file.write(head, 4) == 4;
  file.flush();
  return ok;
}
