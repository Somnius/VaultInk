#pragma once

#include <cstdint>
#include <string>

// The search index of one book (kos25k #24, opt-in in Settings -> Reader -> Search word): a file in the
// book's cache with one Bloom filter of three-letter pieces per chapter (lib/Vault/TrigramFilter.h). Searches
// fill it as they read, one chapter at a time, so it grows with use and costs no waiting of its own; a chapter
// whose slot is filled and whose filter lacks the searched word is skipped.
//
// Layout: 16-byte header ("VSIX", version u16, chapter count u16, filter bytes u32, 4 spare), then per
// chapter a u32 stamp (the chapter's size + 1, 0 = not read yet) and the filter. A stamp that no longer
// matches the chapter's size means the book changed and the slot is ignored.
class SearchIndex {
 public:
  SearchIndex(std::string path, int chapterCount);

  // Opens the file, or makes an empty one when it is missing or made for another chapter count.
  bool open();
  // The filter of `chapter` into `bits` (trigramfilter::kBytes); false when the slot is empty or stale.
  bool read(int chapter, uint32_t stamp, uint8_t* bits) const;
  bool write(int chapter, uint32_t stamp, const uint8_t* bits) const;

 private:
  std::string path_;
  int chapters_;
};
