#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Xtc.h>

#include <functional>
#include <vector>

#include "ClippingStore.h"
#include "CrossPointState.h"
#include "FavouritesStore.h"
#include "RecentBooksStore.h"
#include "util/BookmarkUtil.h"

namespace {
std::string getBookCachePath(const std::string& path) {
  const char* prefix = nullptr;
  if (FsHelpers::hasReflowableBookExtension(path)) {
    prefix = "epub_";
  } else if (FsHelpers::hasXtcExtension(path)) {
    prefix = "xtc_";
  } else if (FsHelpers::hasCbzExtension(path)) {
    prefix = "cbz_";
  } else {
    return "";
  }
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(path));
}

bool moveStatePath(const std::string& oldPath, const std::string& newPath, bool& moved) {
  moved = false;
  if (oldPath.empty() || !Storage.exists(oldPath.c_str())) return true;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("BookCache", "Rename state target already exists: %s", newPath.c_str());
    return false;
  }
  moved = Storage.rename(oldPath.c_str(), newPath.c_str());
  if (!moved) LOG_ERR("BookCache", "Failed to move rename state: %s -> %s", oldPath.c_str(), newPath.c_str());
  return moved;
}

void rollBackStatePath(const std::string& oldPath, const std::string& newPath, const bool moved) {
  if (moved && !Storage.rename(newPath.c_str(), oldPath.c_str())) {
    LOG_ERR("BookCache", "Failed to roll back rename state: %s -> %s", newPath.c_str(), oldPath.c_str());
  }
}

// The books under a folder, as paths relative to it ("a/b.epub"), for carrying their state when the
// folder moves. Hidden folders are skipped; capped, so a huge tree moves without carrying the rest.
constexpr size_t MAX_CARRIED_BOOKS = 400;
constexpr size_t MAX_CARRY_DEPTH = 16;
void collectBooks(const std::string& root, std::vector<std::string>& out) {
  struct Level {
    HalFile file;
    std::string rel;
  };
  std::vector<Level> stack;
  HalFile top = Storage.open(root.c_str());
  if (!top || !top.isDirectory()) return;
  stack.push_back({std::move(top), ""});
  char name[500];
  while (!stack.empty() && out.size() < MAX_CARRIED_BOOKS) {
    HalFile entry = stack.back().file.openNextFile();
    if (!entry) {
      stack.back().file.close();
      stack.pop_back();
      continue;
    }
    const size_t length = entry.getName(name, sizeof(name));
    if (length == 0 || length >= sizeof(name) || name[0] == '.') continue;
    const std::string rel = stack.back().rel + name;
    if (entry.isDirectory()) {
      if (stack.size() < MAX_CARRY_DEPTH) stack.push_back({std::move(entry), rel + "/"});
    } else if (FsHelpers::hasReflowableBookExtension(rel) || FsHelpers::hasXtcExtension(rel) ||
               FsHelpers::hasCbzExtension(rel)) {
      out.push_back(rel);
    }
  }
}

bool moveFolderWithState(const std::string& oldPath, const std::string& newPath);
}  // namespace

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";
  constexpr char CBZ_PREFIX[] = "cbz_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0 ||
         strncmp(name, CBZ_PREFIX, std::size(CBZ_PREFIX) - 1) == 0;
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasReflowableBookExtension(path)) {
    Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasCbzExtension(path)) {
    Storage.removeDir(("/.crosspoint/cbz_" + std::to_string(std::hash<std::string>{}(path))).c_str());
  } else {
    return;
  }
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
}

bool removeBookFile(const std::string& path) {
  if (!Storage.remove(path.c_str())) return false;
  clearBookCache(path);
  return ClippingStore::deleteForFilePath(path, "epub");
}

// Renames or moves a file or folder on the card. A book's cache, bookmarks and clippings move with
// it and roll back on failure. A folder moves as one rename with its books' clippings; then each
// book's cache, bookmarks, recent and favourite entries follow (a failure there only costs that
// book its saved place).
bool moveBookWithState(const std::string& oldPath, const std::string& newPath) {
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("BookCache", "Move target already exists: %s", newPath.c_str());
    return false;
  }
  auto probe = Storage.open(oldPath.c_str());
  const bool isDirectory = probe && probe.isDirectory();
  if (probe) probe.close();
  if (isDirectory) return moveFolderWithState(oldPath, newPath);

  const std::string oldCachePath = getBookCachePath(oldPath);
  const std::string newCachePath = getBookCachePath(newPath);
  const bool hasBookmarks = FsHelpers::hasReflowableBookExtension(oldPath);
  const std::string oldBookmarkPath = hasBookmarks ? BookmarkUtil::getBookmarkPath(oldPath) : "";
  const std::string newBookmarkPath = hasBookmarks ? BookmarkUtil::getBookmarkPath(newPath) : "";
  bool cacheMoved = false;
  bool bookmarksMoved = false;
  if (!moveStatePath(oldCachePath, newCachePath, cacheMoved)) return false;
  if (!moveStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved)) {
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return false;
  }
  if (!ClippingStore::moveBook(oldPath, newPath)) {
    LOG_ERR("BookCache", "Failed to move file: %s -> %s", oldPath.c_str(), newPath.c_str());
    rollBackStatePath(oldBookmarkPath, newBookmarkPath, bookmarksMoved);
    rollBackStatePath(oldCachePath, newCachePath, cacheMoved);
    return false;
  }

  RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  FAVOURITES.updatePath(oldPath, newPath);
  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) LOG_ERR("BookCache", "Failed to save renamed open-book path");
  }
  return true;
}

namespace {
bool moveFolderWithState(const std::string& oldPath, const std::string& newPath) {
  std::vector<std::string> books;
  collectBooks(oldPath, books);
  if (!ClippingStore::moveBook(oldPath, newPath)) {
    LOG_ERR("BookCache", "Failed to move folder: %s -> %s", oldPath.c_str(), newPath.c_str());
    return false;
  }
  for (const auto& rel : books) {
    const std::string oldBook = oldPath + "/" + rel;
    const std::string newBook = newPath + "/" + rel;
    const std::string oldCachePath = getBookCachePath(oldBook);
    const std::string newCachePath = getBookCachePath(newBook);
    bool moved = false;
    moveStatePath(oldCachePath, newCachePath, moved);
    if (FsHelpers::hasReflowableBookExtension(oldBook)) {
      moveStatePath(BookmarkUtil::getBookmarkPath(oldBook), BookmarkUtil::getBookmarkPath(newBook), moved);
    }
    RECENT_BOOKS.updatePath(oldBook, newBook, oldCachePath, newCachePath);
    FAVOURITES.updatePath(oldBook, newBook);
    if (APP_STATE.openEpubPath == oldBook) {
      APP_STATE.openEpubPath = newBook;
      if (!APP_STATE.saveToFile()) LOG_ERR("BookCache", "Failed to save moved open-book path");
    }
  }
  LOG_DBG("BookCache", "Moved folder %s -> %s, %u books carried", oldPath.c_str(), newPath.c_str(),
          static_cast<unsigned>(books.size()));
  return true;
}
}  // namespace
