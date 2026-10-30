#pragma once

#include <string>
#include <string_view>
#include <vector>

// The pages of a CBZ comic (Phase 1c.3), from its zip entry names. Pure text work, host-tested.
namespace cbz {

struct Chapter {
  std::string title;  // the folder's name; "" for pages at the top of the zip
  size_t firstPage = 0;
};

struct Book {
  std::vector<std::string> pages;  // zip entry names in reading order
  std::vector<Chapter> chapters;   // one per folder holding pages, in page order
  size_t skipped = 0;              // image entries in a format the reader can't show (WebP, GIF...)
  std::string comicInfo;           // the ComicInfo.xml entry, any case, or ""
};

// JPEG and PNG entries only; folders are chapters. Skips __MACOSX, "._*" resource forks,
// Thumbs.db, hidden files and anything with an unsafe path. Natural order ("2" before "10"),
// folders first by their own natural order, case ignored.
Book pages(const std::vector<std::string>& entries);

// True when ComicInfo.xml says <Manga>YesAndRightToLeft</Manga> (case and spaces ignored).
bool mangaRightToLeft(std::string_view comicInfoXml);

// "page2.jpg" < "page10.jpg"; letters compared without case.
bool naturalLess(std::string_view a, std::string_view b);

}  // namespace cbz
