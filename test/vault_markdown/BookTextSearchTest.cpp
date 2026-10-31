#include <gtest/gtest.h>

#include <algorithm>

#include "BookTextSearch.h"
#include "WordSearch.h"

namespace {

const char* kHead =
    "<?xml version=\"1.0\"?>\n<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" "
    "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n<html xmlns=\"http://www.w3.org/1999/xhtml\">";

std::string doc(const std::string& head, const std::string& body) {
  return std::string(kHead) + "<head>" + head + "</head><body>" + body + "</body></html>";
}

struct Result {
  std::vector<ChapterTextSearcher::Hit> hits;
  uint32_t total = 0;
  bool ok = false;
};

Result run(const std::string& xml, const std::string& query, const size_t chunk = 0, const size_t maxHits = 50) {
  ChapterTextSearcher searcher(wordsearch::prepareQuery(query), maxHits);
  const size_t step = chunk ? chunk : xml.size();
  for (size_t i = 0; i < xml.size(); i += step) {
    const size_t n = std::min(step, xml.size() - i);
    if (searcher.write(reinterpret_cast<const uint8_t*>(xml.data()) + i, n) != n) break;
  }
  Result r;
  r.ok = searcher.finish();
  r.hits = searcher.hits();
  r.total = searcher.visibleTotal();
  return r;
}

std::vector<uint32_t> offsets(const Result& r) {
  std::vector<uint32_t> out;
  for (const auto& h : r.hits) out.push_back(h.offset);
  return out;
}

}  // namespace

TEST(BookTextSearch, FindsAWordWithItsVisibleOffset) {
  const auto r = run(doc("", "<p>The quick brown fox</p>"), "brown");
  ASSERT_TRUE(r.ok);
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].offset, 10u);  // "The quick " is 10 code points
  EXPECT_EQ(r.total, 19u);
  EXPECT_NE(r.hits[0].snippet.find("quick brown fox"), std::string::npos);
}

TEST(BookTextSearch, CaseAndAccentsDoNotMatter) {
  const auto r = run(doc("", "<p>Un CAFÉ crème, et Καλημέρα</p>"), "cafe");
  EXPECT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(run(doc("", "<p>Un CAFÉ crème</p>"), "CREME").hits.size(), 1u);
  EXPECT_EQ(run(doc("", "<p>Καλημέρα κόσμε</p>"), "καλημερα").hits.size(), 1u);
  EXPECT_EQ(run(doc("", "<p>Привет мир</p>"), "ПРИВЕТ").hits.size(), 1u);
}

TEST(BookTextSearch, PartOfAWordMatches) {
  const auto r = run(doc("", "<p>reading read Reader</p>"), "read");
  EXPECT_EQ(offsets(r), (std::vector<uint32_t>{0, 8, 13}));
}

TEST(BookTextSearch, EntitiesAreExpandedAndCounted) {
  // &nbsp; is one code point in the layout parser; it also separates words for the matcher.
  const auto r = run(doc("", "<p>a&nbsp;b &amp; c &#233;t&#xE9; &mdash; end</p>"), "end");
  ASSERT_EQ(r.hits.size(), 1u);
  const std::string text =
      "a\xC2\xA0"
      "b & c \xC3\xA9t\xC3\xA9 \xE2\x80\x94 ";
  uint32_t cps = 0;
  for (const unsigned char c : text)
    if ((c & 0xC0) != 0x80) cps++;
  EXPECT_EQ(r.hits[0].offset, cps);
  EXPECT_EQ(run(doc("", "<p>a&nbsp;b</p>"), "a b").hits.size(), 1u);
}

TEST(BookTextSearch, UnknownEntityIsKeptAsText) {
  const auto r = run(doc("", "<p>x &zzz; end</p>"), "end");
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].offset, 2u + 5u + 1u);  // "x ", "&zzz;", " "
}

TEST(BookTextSearch, HeadStyleScriptTitleAndRubyParenthesesAreNotVisible) {
  const std::string xml = doc("<title>needle</title><style>needle {}</style>",
                              "<script>needle()</script><p>a<rp>needle</rp>b needle</p><style>needle</style>");
  const auto r = run(xml, "needle");
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].offset, 3u);  // "a", "b" and a space; the <rp> text is not counted
}

TEST(BookTextSearch, SoftHyphenIsCountedButIgnoredByTheMatcher) {
  const auto r = run(doc("",
                         "<p>co\xC2\xAD"
                         "operate then cooperate</p>"),
                     "cooperate");
  ASSERT_EQ(r.hits.size(), 2u);
  EXPECT_EQ(r.hits[0].offset, 0u);
  EXPECT_EQ(r.hits[1].offset, 16u);  // "co" + shy + "operate" + " then " = 2+1+7+6
}

TEST(BookTextSearch, TablesAreCounted) {
  const auto r = run(doc("", "<table><tr><td>one</td><td>two</td></tr></table><p>three</p>"), "three");
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].offset, 6u);
  EXPECT_EQ(run(doc("", "<table><tr><td>one</td><td>two</td></tr></table>"), "one two").hits.size(), 1u);
}

TEST(BookTextSearch, PhraseAcrossTags) {
  EXPECT_EQ(run(doc("", "<p>the <i>quick</i> brown</p>"), "quick brown").hits.size(), 1u);
  EXPECT_EQ(run(doc("", "<p>the <i>qui</i>ck <b>bro</b>wn</p>"), "quick brown").hits.size(), 1u);
  EXPECT_EQ(run(doc("", "<p>the quick</p><p>brown</p>"), "quick brown").hits.size(), 1u);
  // Two paragraphs with no space between them are two words.
  EXPECT_EQ(run(doc("", "<p>quick</p><p>brown</p>"), "kb").hits.size(), 0u);
  EXPECT_EQ(run(doc("", "<p>quick   \n\t brown</p>"), "quick brown").hits.size(), 1u);
}

TEST(BookTextSearch, ChunkBoundariesDoNotChangeTheHits) {
  std::string body;
  for (int i = 0; i < 40; i++) body += "<p>Caf\xC3\xA9 &nbsp;r\xC3\xA9sum\xC3\xA9 <b>fox</b> jumps &amp; over</p>";
  const std::string xml = doc("<style>fox</style>", body);
  const auto whole = run(xml, "fox jumps");
  ASSERT_FALSE(whole.hits.empty());
  for (const size_t chunk : {1u, 2u, 3u, 7u, 64u}) {
    const auto r = run(xml, "fox jumps", chunk);
    EXPECT_EQ(offsets(r), offsets(whole)) << chunk;
    EXPECT_EQ(r.total, whole.total) << chunk;
    ASSERT_EQ(r.hits.size(), whole.hits.size());
    for (size_t i = 0; i < r.hits.size(); i++) EXPECT_EQ(r.hits[i].snippet, whole.hits[i].snippet);
  }
}

TEST(BookTextSearch, PerPageAndPerChapterCaps) {
  std::string body = "<p>";
  for (int i = 0; i < 10; i++) body += "word ";
  body += "</p>";
  // Ten hits in one page-sized bucket: three kept.
  EXPECT_EQ(run(doc("", body), "word").hits.size(), 3u);
  std::string spread;
  for (int i = 0; i < 20; i++) spread += "<p>word " + std::string(2100, 'x') + "</p>";
  EXPECT_EQ(run(doc("", spread), "word", 0, 5).hits.size(), 5u);
}

TEST(BookTextSearch, AbortCutsTheStream) {
  int calls = 0;
  ChapterTextSearcher searcher(wordsearch::prepareQuery("fox"), 10, [&] { return ++calls > 1; });
  const std::string xml = doc("", "<p>fox</p>");
  EXPECT_EQ(searcher.write(reinterpret_cast<const uint8_t*>(xml.data()), 10), 10u);
  EXPECT_EQ(searcher.write(reinterpret_cast<const uint8_t*>(xml.data()) + 10, 10), 0u);
  EXPECT_TRUE(searcher.aborted());
}

TEST(BookTextSearch, BrokenXmlKeepsTheEarlierHits) {
  const auto r = run(doc("", "<p>fox</p></div><p>fox</p>"), "fox");
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.hits.size(), 1u);
}

TEST(BookTextSearch, NoMatchAndEmptyQuery) {
  EXPECT_TRUE(run(doc("", "<p>nothing here</p>"), "fox").hits.empty());
  ChapterTextSearcher empty("", 10);
  EXPECT_FALSE(empty.ok());
}

TEST(BookTextSearch, UppercaseBodyTagEndsTheVisibleText) {
  const std::string xml = std::string(kHead) + "<head></head><BODY><p>fox</p></BODY>fox</html>";
  const auto r = run(xml, "fox");
  EXPECT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.total, 3u);
}

TEST(BookTextSearch, QueryInsideALongWordStillGetsASnippet) {
  const std::string url = "https://example.com/" + std::string(200, 'a') + "needle" + std::string(200, 'b');
  const auto r = run(doc("", "<p>see " + url + " ok</p>"), "needle");
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_NE(r.hits[0].snippet.find("needle"), std::string::npos);
}

TEST(BookTextSearch, SnippetsNeverSplitAMultiByteCharacter) {
  std::string greek;
  for (int i = 0; i < 80; i++) greek += "\xCE\xBA\xCE\xB1\xCE\xBB\xCE\xB7\xCE\xBC\xCE\xAD\xCF\x81\xCE\xB1 ";
  const std::string body =
      "<p>" + greek + "ΛΕΞΗ" + greek + "</p><p>" + std::string(300, 'x') + "\xF0\x9F\x98\x80needle</p>";
  for (const char* q : {"λεξη", "needle"}) {
    const auto r = run(doc("", body), q);
    ASSERT_EQ(r.hits.size(), 1u) << q;
    const std::string& s = r.hits[0].snippet;
    // Valid UTF-8: every lead byte is followed by its continuation bytes.
    for (size_t i = 0; i < s.size();) {
      const unsigned char c = static_cast<unsigned char>(s[i]);
      const size_t len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
      ASSERT_GT(len, 0u) << "stray continuation byte in '" << s << "'";
      ASSERT_LE(i + len, s.size());
      for (size_t k = 1; k < len; k++) ASSERT_EQ(static_cast<unsigned char>(s[i + k]) & 0xC0, 0x80);
      i += len;
    }
  }
}

TEST(BookTextSearch, MultiByteCharactersSplitAtAnyByteGiveTheSameOffsets) {
  const std::string xml = doc("", "<p>\xF0\x9F\x98\x80 \xE2\x82\xAC caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC needle</p>");
  const auto whole = run(xml, "needle");
  ASSERT_EQ(whole.hits.size(), 1u);
  EXPECT_EQ(whole.hits[0].offset, 12u);
  for (size_t cut = 1; cut < xml.size(); cut++) {
    ChapterTextSearcher s(wordsearch::prepareQuery("needle"), 5);
    s.write(reinterpret_cast<const uint8_t*>(xml.data()), cut);
    s.write(reinterpret_cast<const uint8_t*>(xml.data()) + cut, xml.size() - cut);
    s.finish();
    ASSERT_EQ(s.hits().size(), 1u) << cut;
    EXPECT_EQ(s.hits()[0].offset, 12u) << cut;
    EXPECT_EQ(s.visibleTotal(), whole.total) << cut;
  }
}

TEST(BookTextSearch, ALongRunWithoutAMatchKeepsOnlyAShortTail) {
  std::string body = "<p>";
  for (int i = 0; i < 200000; i++) body += "abc ";
  body += "needle</p>";
  const auto r = run(doc("", body), "needle", 4096);
  ASSERT_EQ(r.hits.size(), 1u);
  EXPECT_EQ(r.hits[0].offset, 800000u);
}
