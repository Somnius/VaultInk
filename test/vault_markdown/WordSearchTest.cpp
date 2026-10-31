#include <gtest/gtest.h>

#include "WordSearch.h"

using wordsearch::find;
using wordsearch::prepareQuery;
using wordsearch::snippet;

namespace {
std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < s.size()) {
    const size_t e = s.find(' ', i);
    out.push_back(s.substr(i, e == std::string::npos ? std::string::npos : e - i));
    if (e == std::string::npos) break;
    i = e + 1;
  }
  return out;
}
}  // namespace

TEST(WordSearch, QueryIsFoldedAndTrimmed) {
  EXPECT_EQ(prepareQuery("  Quick   BROWN \n"), "quick brown");
  EXPECT_EQ(prepareQuery("   "), "");
}

TEST(WordSearch, CaseDoesNotMatter) {
  const auto words = split("The Quick brown fox");
  EXPECT_EQ(find(words, prepareQuery("QUICK")), (std::vector<size_t>{1}));
}

TEST(WordSearch, InsideAWordAndEveryHit) {
  const auto words = split("reading read Reader ready");
  EXPECT_EQ(find(words, prepareQuery("read")), (std::vector<size_t>{0, 1, 2, 3}));
}

TEST(WordSearch, PhraseAcrossWords) {
  const auto words = split("a quick brown fox and another quick brown dog");
  EXPECT_EQ(find(words, prepareQuery("Quick Brown")), (std::vector<size_t>{1, 6}));
  EXPECT_TRUE(find(words, prepareQuery("brown quick")).empty());
}

TEST(WordSearch, GreekIgnoresAccentsAndCase) {
  const auto words = split("Η Καλημέρα του κόσμου");
  EXPECT_EQ(find(words, prepareQuery("ΚΑΛΗΜΕΡΑ")), (std::vector<size_t>{1}));
  EXPECT_EQ(find(words, prepareQuery("κοσμου")), (std::vector<size_t>{3}));
}

TEST(WordSearch, NothingToFind) {
  EXPECT_TRUE(find(split("one two"), "").empty());
  EXPECT_TRUE(find({}, "x").empty());
  EXPECT_TRUE(find(split("one two"), prepareQuery("three")).empty());
}

TEST(WordSearch, HitsAreCapped) { EXPECT_EQ(find(split("a a a a a"), "a", 3).size(), 3u); }

TEST(WordSearch, Snippet) {
  const auto words = split("one two three four five six seven eight nine ten eleven twelve");
  EXPECT_EQ(snippet(words, 0, 2, 2), "one two three ...");
  EXPECT_EQ(snippet(words, 6, 2, 2), "... five six seven eight nine ...");
  EXPECT_EQ(snippet(words, 11, 1, 3), "... eleven twelve");
  EXPECT_EQ(snippet(words, 99), "");
}
