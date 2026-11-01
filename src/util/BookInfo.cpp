#include "BookInfo.h"

#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>

#include <algorithm>
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

// ASCII-lowercased copy, enough to compare names that differ only in capitals.
std::string lowered(const std::string& text) {
  std::string out = text;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
  return out;
}

// True when the file name only repeats the title (and the author): "Title - Author.epub", "title.epub".
bool fileNameRepeats(const std::string& fileName, const std::string& title, const std::string& author) {
  const size_t dot = fileName.find_last_of('.');
  const std::string stem = lowered(dot == std::string::npos ? fileName : fileName.substr(0, dot));
  const std::string t = lowered(title);
  if (stem.empty() || t.empty()) return false;
  // Too short a part would match inside any name ("It" in "Switch").
  constexpr size_t MIN_LEN = 5;
  if (t.size() >= MIN_LEN && stem.find(t) != std::string::npos) return true;
  if (stem.size() >= MIN_LEN && t.find(stem) != std::string::npos) return true;
  // "Author - Title": both parts are in the name.
  const std::string a = lowered(author);
  return a.size() >= MIN_LEN && stem.find(a) != std::string::npos &&
         stem.find(t.substr(0, std::min<size_t>(t.size(), 12))) != std::string::npos;
}

}  // namespace

std::string modifiedText(const uint32_t packed) {
  const unsigned date = packed >> 16;
  const unsigned time = packed & 0xFFFF;
  if (date == 0) return "";
  char buf[24];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u", 1980 + (date >> 9), (date >> 5) & 0xF, date & 0x1F, time >> 11,
           (time >> 5) & 0x3F);
  return buf;
}

std::string fileText(const std::string& path) {
  const bool folder = !path.empty() && path.back() == '/';
  const std::string clean = folder ? path.substr(0, path.size() - 1) : path;
  const size_t slash = clean.find_last_of('/');
  std::string out = slash == std::string::npos ? clean : clean.substr(slash + 1);
  HalFile file;
  if (Storage.openFileForRead("INFO", clean.c_str(), file)) {
    if (!file.isDirectory())
      out += " · " + std::string(tr(STR_INFO_SIZE)) + " " + sizeText(static_cast<uint32_t>(file.fileSize()));
    const std::string modified = modifiedText(file.modificationTime());
    if (!modified.empty()) out += " · " + std::string(tr(STR_INFO_MODIFIED)) + " " + modified;
    file.close();
  }
  return out;
}

std::string text(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const std::string fileName = slash == std::string::npos ? path : path.substr(slash + 1);

  uint32_t fileSize = 0;
  std::string modified;
  HalFile file;
  if (Storage.openFileForRead("INFO", path.c_str(), file)) {
    fileSize = static_cast<uint32_t>(file.fileSize());
    modified = modifiedText(file.modificationTime());
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
  // The file name only when it says something the title does not: "Title - Author.epub" would repeat both (kos25k #15).
  if (!title.empty() && !fileNameRepeats(fileName, title, author)) out += " · " + fileName;
  out += " · " + std::string(tr(STR_INFO_SIZE)) + " " + sizeText(fileSize);
  const int progress = loadBookProgress(path);
  if (progress >= 0) out += " · " + std::string(tr(STR_INFO_READ)) + " " + std::to_string(progress) + "%";
  if (!modified.empty()) out += " · " + std::string(tr(STR_INFO_MODIFIED)) + " " + modified;
  if (FAVOURITES.contains(path)) out += " · " + std::string(tr(STR_FAVOURITES));
  return out;
}

}  // namespace bookinfo
