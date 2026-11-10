#include <Wikipedia.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

using namespace wikipedia;

namespace {

// The shape of a REST page summary (hand-written after the API's fields; nested objects and the
// HTML extract are there to be skipped).
const char* const TEA_JSON =
    R"JSON({"type":"standard","title":"Tea","displaytitle":"<span class=\"mw-page-title-main\">Tea</span>",
"namespace":{"id":0,"text":""},"wikibase_item":"Q6097","titles":{"canonical":"Tea","normalized":"Tea","display":"Tea"},
"pageid":29969,"thumbnail":{"source":"https://upload.wikimedia.org/x/320px-Tea.jpg","width":320,"height":213},
"lang":"en","dir":"ltr","revision":"1250000000","tid":"aa-bb","timestamp":"2026-10-01T10:00:00Z",
"description":"Hot drink made from water and tea leaves","description_source":"local",
"content_urls":{"desktop":{"page":"https://en.wikipedia.org/wiki/Tea","extract":"not this one"},
"mobile":{"page":"https://en.m.wikipedia.org/wiki/Tea"}},
"extract":"Tea is an aromatic beverage prepared by pouring hot or boiling water over cured or fresh leaves of \"Camellia sinensis\".\nIt is the most widely consumed drink after water.",
"extract_html":"<p><b>Tea</b> is an aromatic beverage.</p>"})JSON";

const char* const ATHENS_EL_JSON =
    R"JSON({"type":"standard","title":"Αθήνα","displaytitle":"<span class=\"mw-page-title-main\">Αθήνα</span>",
"namespace":{"id":0,"text":""},"titles":{"canonical":"Αθήνα","normalized":"Αθήνα","display":"Αθήνα"},"pageid":1325,
"lang":"el","dir":"ltr","description":"πρωτεύουσα της Ελλάδας",
"coordinates":{"lat":37.98,"lon":23.72},
"extract":"Η Αθήνα είναι η πρωτεύουσα και μεγαλύτερη πόλη της Ελλάδας.",
"extract_html":"<p>Η <b>Αθήνα</b> είναι η πρωτεύουσα.</p>"})JSON";

// The same page with the letters as \u escapes, as a server may send them.
const char* const ATHENS_ESCAPED_JSON = R"JSON({"type":"standard","title":"Αθήνα","extract":"Η Αθήνα 🏛 é"})JSON";

const char* const NOT_FOUND_JSON =
    R"JSON({"type":"https://mediawiki.org/wiki/HyperSwitch/errors/not_found","title":"Not found.","method":"get",
"detail":"Page or revision not found.","uri":"/el.wikipedia.org/v1/page/summary/Xyzzyqq"})JSON";

const char* const DISAMBIGUATION_JSON =
    R"JSON({"type":"disambiguation","title":"Mercury","displaytitle":"Mercury","pageid":19694,
"description":"Topics referred to by the same term","extract":"Mercury most commonly refers to:\nMercury (planet)",
"extract_html":"<p>Mercury most commonly refers to:</p>"})JSON";

Summary parse(const std::string& body, int status, size_t chunk = 0) {
  SummaryParser parser;
  if (chunk == 0) {
    parser.feed(body.data(), body.size());
  } else {
    for (size_t i = 0; i < body.size(); i += chunk) {
      if (!parser.feed(body.data() + i, std::min(chunk, body.size() - i))) break;
    }
  }
  return parser.finish(status);
}

}  // namespace

TEST(WikipediaLanguage, BookFirstThenUiThenEnglish) {
  EXPECT_EQ(languageFor("el", "EN"), "el");
  EXPECT_EQ(languageFor("el-GR", "EN"), "el");
  EXPECT_EQ(languageFor("EN_us", "EL"), "en");
  EXPECT_EQ(languageFor(" fr ", "EL"), "fr");
  EXPECT_EQ(languageFor("grc", "EL"), "grc");
  EXPECT_EQ(languageFor("", "EL"), "el");
  EXPECT_EQ(languageFor("und3fined", "DE"), "de");
  EXPECT_EQ(languageFor("x", "PT-BR"), "pt");
  EXPECT_EQ(languageFor("english", ""), "en");  // not a code: passed over
  EXPECT_EQ(languageFor("", ""), "en");
  EXPECT_EQ(languageFor("e/../x", "??"), "en");  // nothing odd reaches the host name
  EXPECT_EQ(languageFor("nb-NO", "EN"), "no");
  EXPECT_EQ(languageFor("eng", "EL"), "en");  // ISO 639-2 codes map to the two-letter Wikipedia
  EXPECT_EQ(languageFor("ell", "EN"), "el");
  EXPECT_EQ(languageFor("gre", "EN"), "el");
  EXPECT_EQ(languageFor("und", "DE"), "de");   // undetermined is not a language: the UI language
  EXPECT_EQ(languageFor("ceb", "EN"), "ceb");  // a three-letter Wikipedia of its own passes through
}

TEST(WikipediaTitle, PlainWordsAndSpaces) {
  EXPECT_EQ(encodeTitle("Tea"), "Tea");
  EXPECT_EQ(encodeTitle("  Marie   Curie \n"), "Marie_Curie");
  EXPECT_EQ(encodeTitle("New\nYork City"), "New_York_City");
}

TEST(WikipediaTitle, PunctuationAroundTheSelectionGoes) {
  EXPECT_EQ(encodeTitle("\"Athens,\""), "Athens");
  EXPECT_EQ(encodeTitle("(tea)."), "tea");
  EXPECT_EQ(encodeTitle("\xC2\xAB\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1\xC2\xBB"), "%CE%91%CE%B8%CE%AE%CE%BD%CE%B1");
  EXPECT_EQ(encodeTitle("\xE2\x80\x9CTea\xE2\x80\x9D\xE2\x80\xA6"), "Tea");
  EXPECT_EQ(encodeTitle("Mercury (planet)"), "Mercury_%28planet%29");
  EXPECT_EQ(encodeTitle("rock-and-roll"), "rock-and-roll");
}

TEST(WikipediaTitle, EverythingElseIsPercentEncoded) {
  EXPECT_EQ(encodeTitle("AC/DC"), "AC%2FDC");
  EXPECT_EQ(encodeTitle("Q&A?x=1#y"), "Q%26A%3Fx%3D1%23y");
  EXPECT_EQ(encodeTitle("50% off"), "50%25_off");
  EXPECT_EQ(encodeTitle("caf\xC3\xA9"), "caf%C3%A9");
  EXPECT_EQ(encodeTitle("a/../../w/index.php"), "a%2F..%2F..%2Fw%2Findex.php");
  EXPECT_EQ(encodeTitle("co\xC2\xADoperate"), "cooperate");  // soft hyphen from the book's hyphenation
}

TEST(WikipediaTitle, NothingOrTooMuchGivesNoTitle) {
  EXPECT_EQ(encodeTitle(""), "");
  EXPECT_EQ(encodeTitle("  ... "), "");
  EXPECT_EQ(encodeTitle(std::string(MAX_TITLE_BYTES + 1, 'a')), "");
  EXPECT_EQ(encodeTitle(std::string(MAX_TITLE_BYTES, 'a')).size(), MAX_TITLE_BYTES);
}

TEST(WikipediaUrl, LanguageAndTitle) {
  EXPECT_EQ(summaryUrl("en", "Tea"), "https://en.wikipedia.org/api/rest_v1/page/summary/Tea");
  EXPECT_EQ(summaryUrl("el-GR", "\xCE\x91\xCE\xB8\xCE\xAE\xCE\xBD\xCE\xB1"),
            "https://el.wikipedia.org/api/rest_v1/page/summary/%CE%91%CE%B8%CE%AE%CE%BD%CE%B1");
  EXPECT_EQ(summaryUrl("evil.example/x", "Tea"), "https://en.wikipedia.org/api/rest_v1/page/summary/Tea");
  EXPECT_EQ(summaryUrl("en", " . "), "");
}

TEST(WikipediaSummary, AnArticleGivesTitleAndExtractOnly) {
  const Summary s = parse(TEA_JSON, 200);
  EXPECT_EQ(s.kind, Kind::Article);
  EXPECT_EQ(s.title, "Tea");
  EXPECT_EQ(s.extract,
            "Tea is an aromatic beverage prepared by pouring hot or boiling water over cured or fresh leaves of "
            "\"Camellia sinensis\".\nIt is the most widely consumed drink after water.");
  EXPECT_FALSE(s.truncated);
}

TEST(WikipediaSummary, TheSameAnswerInAnyChunkSize) {
  const Summary whole = parse(TEA_JSON, 200);
  for (const size_t chunk : {1, 2, 3, 7, 64, 500}) {
    const Summary s = parse(TEA_JSON, 200, chunk);
    EXPECT_EQ(s.kind, Kind::Article) << chunk;
    EXPECT_EQ(s.title, whole.title) << chunk;
    EXPECT_EQ(s.extract, whole.extract) << chunk;
  }
}

TEST(WikipediaSummary, AGreekPage) {
  for (const size_t chunk : {0, 1, 5}) {
    const Summary s = parse(ATHENS_EL_JSON, 200, chunk);
    EXPECT_EQ(s.kind, Kind::Article);
    EXPECT_EQ(s.title, "Αθήνα");
    EXPECT_EQ(s.extract, "Η Αθήνα είναι η πρωτεύουσα και μεγαλύτερη πόλη της Ελλάδας.");
  }
}

TEST(WikipediaSummary, UnicodeEscapesBecomeUtf8) {
  const Summary s = parse(ATHENS_ESCAPED_JSON, 200, 3);
  EXPECT_EQ(s.kind, Kind::Article);
  EXPECT_EQ(s.title, "Αθήνα");
  EXPECT_EQ(s.extract, "Η Αθήνα \xF0\x9F\x8F\x9B \xC3\xA9");  // a surrogate pair and a 2-byte letter
}

TEST(WikipediaSummary, A404IsNotFoundWhateverTheBodySays) {
  Summary s = parse(NOT_FOUND_JSON, 404);
  EXPECT_EQ(s.kind, Kind::NotFound);
  EXPECT_TRUE(s.extract.empty());
  s = parse("<html>gateway</html>", 404);
  EXPECT_EQ(s.kind, Kind::NotFound);
  // The same body with a 200 is still not an article: it has no extract.
  EXPECT_EQ(parse(NOT_FOUND_JSON, 200).kind, Kind::Invalid);
}

TEST(WikipediaSummary, ADisambiguationPageIsNamedAsOne) {
  const Summary s = parse(DISAMBIGUATION_JSON, 200, 4);
  EXPECT_EQ(s.kind, Kind::Disambiguation);
  EXPECT_EQ(s.title, "Mercury");
  EXPECT_TRUE(s.extract.empty());
}

TEST(WikipediaSummary, OtherStatusesAndBrokenBodiesAreInvalid) {
  EXPECT_EQ(parse(TEA_JSON, 500).kind, Kind::Invalid);
  EXPECT_EQ(parse(TEA_JSON, 302).kind, Kind::Invalid);
  EXPECT_EQ(parse("", 200).kind, Kind::Invalid);
  EXPECT_EQ(parse("<html><body>captive portal</body></html>", 200).kind, Kind::Invalid);
  EXPECT_EQ(parse(R"([{"extract":"in an array"}])", 200).kind, Kind::Invalid);
  EXPECT_EQ(parse(R"({"type":"standard","title":"X","extract":""})", 200).kind, Kind::Invalid);
  EXPECT_EQ(parse(R"({"type":"standard","title":"X","extract":42})", 200).kind, Kind::Invalid);
  EXPECT_EQ(parse(R"({"type":"standard","title":"X"})", 200).kind, Kind::Invalid);
}

TEST(WikipediaSummary, OnlyTopLevelFieldsCount) {
  const Summary s =
      parse(R"({"a":{"extract":"nested","title":"nested"},"b":["extract","title",{"extract":"deeper"}],)"
            R"("title":"Top","extract":"The real one, with a } and a ] and a , inside.","extract":"second"})",
            200);
  EXPECT_EQ(s.kind, Kind::Article);
  EXPECT_EQ(s.title, "Top");
  EXPECT_EQ(s.extract, "The real one, with a } and a ] and a , inside.");
}

TEST(WikipediaSummary, NothingAfterTheObjectIsRead) {
  const Summary s = parse(R"({"title":"A","extract":"one"}{"title":"B","extract":"two"})", 200);
  EXPECT_EQ(s.title, "A");
  EXPECT_EQ(s.extract, "one");
}

TEST(WikipediaSummary, ALongExtractIsCutAtTheLimitOnAWholeLetter) {
  std::string body = R"({"type":"standard","title":"Μακρύ","extract":")";
  while (body.size() < MAX_BODY_BYTES + 3000) body += "αβγ ";  // 2-byte letters, so a cut can split one
  body += R"(","extract_html":"<p>x</p>"})";

  SummaryParser parser;
  size_t fed = 0;
  bool more = true;
  while (more && fed < body.size()) {
    const size_t n = std::min<size_t>(1021, body.size() - fed);
    more = parser.feed(body.data() + fed, n);
    fed += n;
  }
  EXPECT_FALSE(more);  // the parser asked to stop
  const Summary s = parser.finish(200);
  EXPECT_EQ(s.kind, Kind::Article);
  EXPECT_TRUE(s.truncated);
  EXPECT_LE(s.extract.size(), MAX_BODY_BYTES + 4);
  EXPECT_GT(s.extract.size(), MAX_BODY_BYTES - 200);
  const std::string tail = " \xE2\x80\xA6";
  ASSERT_GE(s.extract.size(), tail.size() + 2);
  EXPECT_EQ(s.extract.substr(s.extract.size() - tail.size()), tail);
  // The byte before the ellipsis ends a whole character: no lone lead byte.
  const std::string before = s.extract.substr(0, s.extract.size() - tail.size());
  const unsigned char last = static_cast<unsigned char>(before.back());
  EXPECT_TRUE(last < 0x80 || (last & 0xC0) == 0x80);
  if ((last & 0xC0) == 0x80) {
    EXPECT_EQ(static_cast<unsigned char>(before[before.size() - 2]) & 0xE0, 0xC0);
  }
}

TEST(WikipediaSummary, TheLimitBeforeAnyExtractIsInvalid) {
  std::string body = R"({"type":"standard","title":"Big","description":")";
  body.append(MAX_BODY_BYTES, 'x');
  body += R"(","extract":"too late"})";
  SummaryParser parser;
  EXPECT_FALSE(parser.feed(body.data(), body.size()));
  EXPECT_EQ(parser.finish(200).kind, Kind::Invalid);
  // Feeding more after the limit changes nothing.
  EXPECT_FALSE(parser.feed("x", 1));
}

TEST(WikipediaSummary, ABodyUnderTheLimitAsksForMore) {
  SummaryParser parser;
  EXPECT_TRUE(parser.feed(TEA_JSON, strlen(TEA_JSON)));
}
