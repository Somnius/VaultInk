#pragma once

#include <HalStorage.h>

#include <cstdint>

// When a file was last added or changed (kos25k #6), packed like HalFile::modificationTime(). The device's HalFile
// has addedTime() (the later of the created and modified time); the simulator's own HalFile only has the modified time.
namespace filetime {
inline uint32_t added(HalFile& file) {
#ifdef SIMULATOR
  return file.modificationTime();
#else
  return file.addedTime();
#endif
}
}  // namespace filetime
