#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Looks for a word or a short phrase in the words of one book page (kos25k #24). Case and accents
// don't matter (textfold: Latin, Greek, Cyrillic). The words are the page's words in reading order.
namespace wordsearch {

// Part of a word: letters and digits of any script; spaces, ASCII punctuation, Latin-1 punctuation and
// the general punctuation block are not.
inline bool isWordChar(const uint32_t cp) {
  if (cp < 0x80) return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
  if (cp <= 0xBF) return cp == 0xAA || cp == 0xB5 || cp == 0xBA;
  if (cp == 0xD7 || cp == 0xF7) return false;
  if (cp >= 0x2000 && cp <= 0x206F) return false;
  if (cp == 0x037E || cp == 0x0387) return false;  // Greek question mark and ano teleia
  if ((cp >= 0x2E00 && cp <= 0x2E7F) || (cp >= 0x3000 && cp <= 0x303F)) return false;  // more punctuation, CJK
  if ((cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20)) return false;  // full-width punctuation
  return true;
}

// The query folded and with its spaces collapsed; "" when nothing is left to look for.
std::string prepareQuery(std::string_view query);

// Indexes of the words where the (prepared) query starts, in order, without overlaps: a query of
// two words found at words 3-4 is one hit at 3. A query matches inside a word too ("read" finds
// "reading"), and across a word boundary only when it contains the space.
// With exactWords the match must also start and end at a word edge (no letter or digit touching it).
std::vector<size_t> find(const std::vector<std::string>& words, const std::string& preparedQuery, size_t maxHits = 50,
                         bool exactWords = false);

// A few words of context around word `at`, e.g. "… the quick brown fox jumps over …".
std::string snippet(const std::vector<std::string>& words, size_t at, size_t before = 4, size_t after = 5);

}  // namespace wordsearch
