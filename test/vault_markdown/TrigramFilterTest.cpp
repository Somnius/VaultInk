#include <gtest/gtest.h>

#include <vector>

#include "BookTextSearch.h"
#include "TrigramFilter.h"
#include "WordSearch.h"

namespace {

const char* kHead =
    "<?xml version=\"1.0\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>t</title></head><body>";

// The filter of a chapter whose body is `body`, read the way the search reads it.
std::vector<uint8_t> filterOf(const std::string& body) {
  std::vector<uint8_t> bits(trigramfilter::kBytes, 0);
  ChapterTextSearcher searcher(wordsearch::prepareQuery("zzzzqq"), 10);
  searcher.collectTrigrams(bits.data());
  const std::string xml = std::string(kHead) + body + "</body></html>";
  searcher.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  searcher.finish();
  EXPECT_TRUE(searcher.trigramsComplete());
  return bits;
}

bool mayHold(const std::vector<uint8_t>& bits, const std::string& query) {
  return trigramfilter::mayContain(bits.data(), trigramfilter::queryTrigrams(wordsearch::prepareQuery(query)));
}

}  // namespace

TEST(TrigramFilter, NeverRejectsAWordThatIsThere) {
  const auto bits = filterOf("<p>The quick brown fox, Jumps over the lazy dog.</p><p>Caf\xC3\xA9 au lait</p>");
  for (const char* query :
       {"quick", "QUICK", "jumps", "lazy dog", "brown fox", "fox", "uic", "cafe", "au lait", "ick"}) {
    EXPECT_TRUE(mayHold(bits, query)) << query;
  }
}

TEST(TrigramFilter, RejectsAWordThatIsNotThere) {
  const auto bits = filterOf("<p>The quick brown fox jumps over the lazy dog.</p>");
  EXPECT_FALSE(mayHold(bits, "zebra"));
  EXPECT_FALSE(mayHold(bits, "quack"));
}

TEST(TrigramFilter, ShortWordsCannotBeJudged) {
  const auto bits = filterOf("<p>nothing here</p>");
  EXPECT_TRUE(trigramfilter::queryTrigrams(wordsearch::prepareQuery("go to")).empty());
  EXPECT_TRUE(mayHold(bits, "go to"));
}

TEST(TrigramFilter, PiecesDoNotRunAcrossSpaces) {
  const auto bits = filterOf("<p>ab cd</p>");
  // "b c" would be a trigram only if spaces were part of the text; the query never makes one either.
  EXPECT_TRUE(mayHold(bits, "ab cd"));
}

TEST(TrigramFilter, WordsSplitByTagsStayOneWord) {
  const auto bits = filterOf("<p>zephy<i>rine</i> wind</p>");
  EXPECT_TRUE(mayHold(bits, "zephyrine"));
}

TEST(TrigramFilter, AGreekChapterIsFoundWithoutAccents) {
  const auto bits = filterOf(
      "<p>\xCE\x9A\xCE\xB1\xCE\xBB\xCE\xB7\xCE\xBC\xCE\xAD\xCF\x81\xCE\xB1 "
      "\xCE\xBA\xCF\x8C\xCF\x83\xCE\xBC\xCE\xB5</p>");
  EXPECT_TRUE(mayHold(bits, "\xCE\xBA\xCE\xBF\xCF\x83\xCE\xBC\xCE\xB5"));  // "κοσμε" without the accent
  EXPECT_TRUE(mayHold(bits, "\xCE\xBA\xCE\xB1\xCE\xBB\xCE\xB7\xCE\xBC\xCE\xB5\xCF\x81\xCE\xB1"));  // "καλημερα"
}

TEST(TrigramFilter, AStoppedChapterGivesNoFilter) {
  std::vector<uint8_t> bits(trigramfilter::kBytes, 0);
  ChapterTextSearcher searcher(wordsearch::prepareQuery("a"), 1);  // stops at the first hit
  searcher.collectTrigrams(bits.data());
  const std::string xml = std::string(kHead) + "<p>a a a a a a a a</p></body></html>";
  searcher.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  searcher.finish();
  EXPECT_FALSE(searcher.trigramsComplete());
}
