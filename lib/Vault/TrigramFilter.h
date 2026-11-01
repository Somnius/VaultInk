#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A small Bloom filter of the three-letter pieces ("trigrams") of each chapter's text, kept in the book's
// cache by the search index (kos25k #24). A word cannot be in a chapter unless every three-letter piece of
// it is, so a chapter whose filter lacks one is skipped without being read. A filter can only wrongly say
// "maybe", never wrongly say "no", so the hits are the same with the index and without it.
//
// The pieces are taken from the same folded text the matcher sees (case and accents gone, soft hyphens
// ignored) and never run across white space.
namespace trigramfilter {

inline constexpr size_t kBytes = 8192;  // per chapter; 65536 bits, two of them set per trigram

// Fills a filter while the folded codepoints of a chapter go by.
class Collector {
 public:
  explicit Collector(uint8_t* bits) : bits_(bits) {}
  void push(uint32_t foldedCodepoint);
  void endWord() { count_ = 0; }  // white space

 private:
  uint8_t* bits_;
  uint32_t last_[2] = {0, 0};
  int count_ = 0;
};

// The trigram hashes of the words in a prepared (folded) query. Words under three letters have none: an
// empty result means "the filter cannot tell", so every chapter has to be read.
std::vector<uint32_t> queryTrigrams(const std::string& preparedQuery);

// True when the filter may hold a chapter that contains all of `trigrams` (always true for none).
bool mayContain(const uint8_t* bits, const std::vector<uint32_t>& trigrams);

}  // namespace trigramfilter
