#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "FsHelpers.h"

// How Browse Files orders a folder (0.69.0, kos25k #6). Folders always come first.
namespace filesort {

enum Mode : uint8_t {
  NAME_AZ = 0,
  NAME_ZA = 1,
  NEWEST = 2,
  OLDEST = 3,
  LARGEST = 4,
  SMALLEST = 5,
  COUNT = 6,
};

struct Entry {
  std::string name;       // a folder ends with '/'
  uint32_t size = 0;      // bytes; 0 for a folder
  uint32_t modified = 0;  // FAT packed date and time (date << 16 | time), 0 when unknown
};

// True when `a` comes before `b`.
inline bool before(const Mode mode, const Entry& a, const Entry& b) {
  const bool aDir = !a.name.empty() && a.name.back() == '/';
  const bool bDir = !b.name.empty() && b.name.back() == '/';
  if (aDir != bDir) return aDir;
  switch (mode) {
    case NAME_ZA:
      return FsHelpers::naturalLess(b.name, a.name);
    case NEWEST:
      if (a.modified != b.modified) return a.modified > b.modified;
      break;
    case OLDEST:
      if (a.modified != b.modified) return a.modified < b.modified;
      break;
    case LARGEST:
      if (!aDir && a.size != b.size) return a.size > b.size;
      break;
    case SMALLEST:
      if (!aDir && a.size != b.size) return a.size < b.size;
      break;
    case NAME_AZ:
    default:
      break;
  }
  return FsHelpers::naturalLess(a.name, b.name);  // ties, and every mode's fallback
}

}  // namespace filesort
