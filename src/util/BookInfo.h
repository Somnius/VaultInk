#pragma once

#include <cstdint>
#include <string>

// The lines of the book info popup (kos25k #15): title and author from the library index (the
// file name when the book isn't indexed), file name, size, reading progress and favourite.
// Opens the index briefly; don't call it while the caller holds the index open.
namespace bookinfo {

std::string text(const std::string& path);

// Any other file or folder: name, size and when it was last changed.
std::string fileText(const std::string& path);

// FAT date and time as packed by HalFile::addedTime(), as "2026-11-01 01:46"; "" when unknown.
std::string modifiedText(uint32_t packed);

// When to show the info popup opened from another popup's choice: a little after that popup closed,
// so the screen behind it is drawn again first.
constexpr unsigned long SHOW_AFTER_MS = 300;

}  // namespace bookinfo
