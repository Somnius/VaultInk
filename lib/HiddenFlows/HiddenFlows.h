#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Hidden flows: chapters (spine items) the reader has hidden are left out of the book's size, so the percent,
// the book page number, Go to page and Go to % count only what is shown. Pure functions, no Arduino.
//
// `cumulative[i]` is the size of the book through spine item i (as BookMetadataCache keeps it). `hidden[i]` is
// non-zero when item i is hidden; a shorter or empty vector means the rest is shown. When nothing is hidden, or
// everything is (nothing would be left to count), the functions answer with the raw numbers.
namespace hiddenflows {

struct Location {
  int spineIndex = 0;
  float fraction = 0.0f;  // how far into that spine item, 0..1
};

// Size of spine item `index`; 0 when out of range or the totals go backwards.
uint32_t itemSize(const std::vector<uint32_t>& cumulative, int index);
bool isHidden(const std::vector<uint8_t>& hidden, int index);
// True when some bytes are hidden and some are left: only then do the visible numbers differ from the raw ones.
bool active(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden);
// Bytes that count: the shown items, or the whole book when not active().
uint32_t visibleSize(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden);
// Counted bytes before the place (spineIndex, fraction). Inside a hidden item this is the boundary where it
// was cut out, so the number stands still while a hidden chapter is read.
uint32_t visibleBytesAt(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden, int spineIndex,
                        float fraction);
// The same as a share of visibleSize(), 0..1; 0 for an empty book.
float visibleFraction(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden, int spineIndex,
                      float fraction);
// The inverse: the shown spine item that holds counted byte `visibleBytes`, and how far into it. A byte on the
// border of two items belongs to the earlier one (fraction 1), as the raw lookup does; past the end is the end
// of the last shown item. Never lands in a hidden item while active().
Location locate(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden, uint32_t visibleBytes);
Location locateFraction(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden,
                        float visibleFraction);

// The spine items a contents entry stands for: from its own spine item up to the one before the next entry
// that starts further on (entries that point into the same item share it). `spineOf(i)` is the spine index of
// contents entry i, -1 when it has none. False when the entry has no spine item.
template <typename SpineOf>
bool chapterRange(const int tocIndex, const int tocCount, const int spineCount, SpineOf spineOf, int& first,
                  int& last) {
  if (tocIndex < 0 || tocIndex >= tocCount) return false;
  first = spineOf(tocIndex);
  if (first < 0 || first >= spineCount) return false;
  last = spineCount - 1;
  for (int i = tocIndex + 1; i < tocCount; i++) {
    const int next = spineOf(i);
    if (next < 0 || next == first) continue;
    // An entry that points backwards ends the chapter at its own item.
    last = next > first ? next - 1 : first;
    break;
  }
  if (last >= spineCount) last = spineCount - 1;
  return true;
}

// The per-book file: a version byte, a 16-bit count, then each hidden spine href as a 16-bit length and its
// bytes (little-endian). Hrefs, not indexes, so a rebuilt book cache with another spine order keeps the choice.
constexpr uint8_t FILE_VERSION = 1;
constexpr size_t MAX_HREFS = 2000;
constexpr size_t MAX_HREF_LENGTH = 1024;
constexpr size_t MAX_FILE_SIZE = 256 * 1024;

// Hrefs that are empty or too long are left out, and the list is cut at MAX_HREFS.
std::string encode(const std::vector<std::string>& hrefs);
// False (and `hrefs` empty) for another version or a cut or damaged file.
bool decode(const uint8_t* data, size_t size, std::vector<std::string>& hrefs);

}  // namespace hiddenflows
