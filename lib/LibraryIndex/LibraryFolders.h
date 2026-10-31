#pragma once

#include <cstdint>

namespace library {

// Groups the books of one sort order by the folder they sit in (the Library's Folders tab).
//
// `folderIds[i]` is the folder of the book at position i of the order being bucketed (i < n).
// Writes, stably (positions inside a folder keep their order):
//   rows[k]        the position of the k-th book once grouped by folder (n entries);
//   starts[g]      where group g begins in rows (one entry per non-empty folder);
//   groupFolder[g] the folder id of group g.
// Groups come in ascending folder id. `counts` is scratch for folderCount entries.
// Returns the group count, or 0 when n is 0 or any folder id is >= folderCount (a damaged index).
// `starts` and `groupFolder` need min(n, folderCount) entries.
uint16_t bucketByFolder(const uint16_t* folderIds, uint16_t n, uint16_t folderCount, uint16_t* counts, uint16_t* rows,
                        uint16_t* starts, uint16_t* groupFolder);

}  // namespace library
