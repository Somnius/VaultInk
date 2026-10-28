#pragma once

#include <cstdint>
#include <string>

// Unpacking a .zip on the SD card (file browser → Extract here, kos25k #7). Everything goes into a
// new folder named after the zip next to it; nothing outside it is written. Entries with ".." or an
// absolute path, names the card can't hold, and macOS "__MACOSX" leftovers are skipped. A failure
// removes the half-made folder.
namespace zipextract {

enum class Result : uint8_t { Ok, Exists, BadZip, Failed };

struct Stats {
  int files = 0;
  int skipped = 0;
};

// `outFolder` is the folder made ("/books/Photos" for "/books/Photos.zip").
Result extractHere(const std::string& zipPath, std::string& outFolder, Stats& stats);

// The part of an entry path that is safe to write under the target folder, or "" to skip it.
std::string safeEntryPath(std::string path);

}  // namespace zipextract
