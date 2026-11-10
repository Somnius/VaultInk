#pragma once

#include "BookmarkEntry.h"
#include "activities/ActivityResult.h"

namespace bookmarkjump {

// The jump to a bookmark, for EpubReaderActivity::applyProgressChange(): its exact text offset when it has one,
// with the page it was made on as a hint. Shared by the bookmarks screen and the book map.
inline ProgressChangeResult resultFor(const BookmarkEntry& bookmark, const int spineCount) {
  ProgressChangeResult result{};
  result.xpath = bookmark.xpath;
  result.percentage = bookmark.percentage;
  result.hasSavedProgress = true;
  result.hasVisibleTextOffset = bookmark.hasVisibleTextOffset;
  result.visibleTextOffset = bookmark.visibleTextOffset;
  // The offset is spine-relative, so carry its spine even when the legacy page hints below
  // are stale. The reader validates the index.
  result.spineIndex = bookmark.computedSpineIndex;
  if (bookmark.computedChapterPageCount > 0 && bookmark.computedChapterProgress < bookmark.computedChapterPageCount &&
      bookmark.computedSpineIndex < spineCount) {
    result.page = bookmark.computedChapterProgress;
    result.totalPages = bookmark.computedChapterPageCount;
  }
  return result;
}

}  // namespace bookmarkjump
