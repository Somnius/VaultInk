#include "BookInfo.h"

#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>

#include <cstdio>

#include "FavouritesStore.h"
#include "RecentBooksStore.h"
#include "util/BookProgress.h"

namespace bookinfo {
namespace {

std::string sizeText(const uint32_t bytes) {
  char buf[24];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
  } else {
    snprintf(buf, sizeof(buf), "%u KB", static_cast<unsigned>((bytes + 1023) / 1024));
  }
  return buf;
}

}  // namespace

std::string text(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const std::string fileName = slash == std::string::npos ? path : path.substr(slash + 1);

  uint32_t fileSize = 0;
  HalFile file;
  if (Storage.openFileForRead("INFO", path.c_str(), file)) {
    fileSize = static_cast<uint32_t>(file.fileSize());
    file.close();
  }

  std::string title;
  std::string author;
  library::LibraryIndexFile index;
  if (index.open(library::libraryIndexPath())) {
    const library::BookIdentity id{library::clixPathHash(path.data(), path.size()), fileSize};
    uint16_t row = 0xFFFF;
    if (index.recentRowsFor(&id, 1, &row) && row != 0xFFFF) {
      const uint16_t ordinal = index.ordinalForRow(library::SortOrder::RecentAsc, row);
      library::ClixRecord record{};
      if (ordinal != 0xFFFF && index.readRecord(ordinal, record)) {
        if (!index.readTitle(record, title)) title.clear();
        if (!index.readAuthor(record, author)) author.clear();
      }
    }
    index.close();
  }
  // Not indexed yet (just copied, or the index is being rebuilt): the recents and favourites lists
  // keep a title too.
  if (title.empty()) {
    for (const auto& book : RECENT_BOOKS.getBooks()) {
      if (book.path != path) continue;
      title = book.title;
      if (author.empty()) author = book.author;
      break;
    }
  }
  if (title.empty()) {
    for (const auto& fav : FAVOURITES.getAll()) {
      if (fav.path != path) continue;
      title = fav.title;
      if (author.empty()) author = fav.author;
      break;
    }
  }

  // One wrapped paragraph (the popup has no line breaks), parts kept apart by " · ".
  std::string out = title.empty() ? fileName : title;
  if (!author.empty()) out += " · " + author;
  if (!title.empty() && title != fileName) out += " · " + fileName;
  out += " · " + std::string(tr(STR_INFO_SIZE)) + " " + sizeText(fileSize);
  const int progress = loadBookProgress(path);
  if (progress >= 0) out += " · " + std::string(tr(STR_INFO_READ)) + " " + std::to_string(progress) + "%";
  if (FAVOURITES.contains(path)) out += " · " + std::string(tr(STR_FAVOURITES));
  return out;
}

}  // namespace bookinfo
