#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Looks for a word or a short phrase in the words of one book page (kos25k #24). Case and accents
// don't matter (textfold: Latin, Greek, Cyrillic). The words are the page's words in reading order.
namespace wordsearch {

// The query folded and with its spaces collapsed; "" when nothing is left to look for.
std::string prepareQuery(std::string_view query);

// Indexes of the words where the (prepared) query starts, in order, without overlaps: a query of
// two words found at words 3-4 is one hit at 3. A query matches inside a word too ("read" finds
// "reading"), and across a word boundary only when it contains the space.
std::vector<size_t> find(const std::vector<std::string>& words, const std::string& preparedQuery, size_t maxHits = 50);

// A few words of context around word `at`, e.g. "… the quick brown fox jumps over …".
std::string snippet(const std::vector<std::string>& words, size_t at, size_t before = 4, size_t after = 5);

}  // namespace wordsearch
