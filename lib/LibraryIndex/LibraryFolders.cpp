#include "LibraryFolders.h"

namespace library {

uint16_t bucketByFolder(const uint16_t* folderIds, const uint16_t n, const uint16_t folderCount, uint16_t* counts,
                        uint16_t* rows, uint16_t* starts, uint16_t* groupFolder) {
  if (n == 0 || folderCount == 0 || !folderIds || !counts || !rows || !starts || !groupFolder) return 0;
  for (uint16_t f = 0; f < folderCount; f++) counts[f] = 0;
  for (uint16_t i = 0; i < n; i++) {
    if (folderIds[i] >= folderCount) return 0;
    counts[folderIds[i]]++;
  }
  // Turn the counts into start offsets, one group per non-empty folder.
  uint16_t groups = 0;
  uint16_t offset = 0;
  for (uint16_t f = 0; f < folderCount; f++) {
    const uint16_t size = counts[f];
    if (size == 0) continue;
    starts[groups] = offset;
    groupFolder[groups++] = f;
    counts[f] = offset;
    offset = static_cast<uint16_t>(offset + size);
  }
  for (uint16_t i = 0; i < n; i++) rows[counts[folderIds[i]]++] = i;
  return groups;
}

}  // namespace library
