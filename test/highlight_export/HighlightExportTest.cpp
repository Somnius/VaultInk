#include <HighlightExport.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace highlightexport;

namespace {

std::string json(std::string_view text, size_t maxChars = SIZE_MAX) {
  std::string out;
  appendJsonString(out, text, maxChars);
  return out;
}

const uint8_t* bytes(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }

}  // namespace

TEST(HighlightExportJson, QuotesBackslashesAndNewlines) {
  EXPECT_EQ(json("plain"), "\"plain\"");
  EXPECT_EQ(json(""), "\"\"");
  EXPECT_EQ(json("He said \"no\"."), "\"He said \\\"no\\\".\"");
  EXPECT_EQ(json("a\\b"), "\"a\\\\b\"");
  EXPECT_EQ(json("one\ntwo\r\nthree\tend"), "\"one\\ntwo\\r\\nthree\\tend\"");
}

TEST(HighlightExportJson, ControlCharactersBecomeEscapes) {
  EXPECT_EQ(json(std::string_view("a\x01"
                                  "b\x1f"
                                  "c\x7f",
                                  6)),
            "\"a\\u0001b\\u001fc\\u007f\"");
  EXPECT_EQ(json(std::string_view("nul\0in", 6)), "\"nul\\u0000in\"");
  EXPECT_EQ(json("\b\f"), "\"\\b\\f\"");
}

TEST(HighlightExportJson, GreekAndOtherUtf8StayAsTheyAre) {
  EXPECT_EQ(json("Γνῶθι σεαυτόν"), "\"Γνῶθι σεαυτόν\"");
  EXPECT_EQ(json("“curly” — dash … 😀"), "\"“curly” — dash … 😀\"");
}

TEST(HighlightExportJson, BytesThatAreNotUtf8BecomeTheReplacementCharacter) {
  EXPECT_EQ(json("a\xFFz"), "\"a\xEF\xBF\xBDz\"");
  EXPECT_EQ(json("cut\xCE"), "\"cut\xEF\xBF\xBD\"");                            // half a Greek letter
  EXPECT_EQ(json("\xC0\xAF"), "\"\xEF\xBF\xBD\xEF\xBF\xBD\"");                  // overlong "/"
  EXPECT_EQ(json("\xED\xA0\x80"), "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");  // a surrogate
}

TEST(HighlightExportJson, CutsOnWholeCharacters) {
  EXPECT_EQ(json("αβγδε", 3), "\"αβγ\"");
  EXPECT_EQ(json("abc", 0), "\"\"");
  EXPECT_EQ(json("ab", 10), "\"ab\"");
}

TEST(HighlightExportTime, Iso8601) {
  EXPECT_EQ(iso8601(0), "1970-01-01T00:00:00+00:00");
  EXPECT_EQ(iso8601(951782400), "2000-02-29T00:00:00+00:00");
  EXPECT_EQ(iso8601(1709251199), "2024-02-29T23:59:59+00:00");
  EXPECT_EQ(iso8601(1709251200), "2024-03-01T00:00:00+00:00");
  EXPECT_EQ(iso8601(1794389400), "2026-11-11T09:30:00+00:00");
  EXPECT_EQ(iso8601(4102444799u), "2099-12-31T23:59:59+00:00");
}

TEST(HighlightExportReadwise, GoldenBodyWithEverything) {
  ReadwiseBatch batch(Book{"Πολιτεία", "Πλάτων"});
  Highlight h;
  h.text = "«Η αρχή είναι το ήμισυ του παντός»,\nsaid \"someone\".";
  h.note = "re-read";
  h.url = "https://example.com/h/1";
  h.order = 3;
  h.timestamp = 1794389400;
  h.chapter = "Βιβλίο Α";  // not a Readwise field
  ASSERT_TRUE(batch.add(h));
  EXPECT_EQ(batch.count(), 1u);
  EXPECT_EQ(batch.body(),
            "{\"highlights\":[{\"text\":\"«Η αρχή είναι το ήμισυ του παντός»,\\nsaid \\\"someone\\\".\","
            "\"title\":\"Πολιτεία\",\"author\":\"Πλάτων\",\"source_type\":\"vaultink\",\"category\":\"books\","
            "\"note\":\"re-read\",\"location\":3,\"location_type\":\"order\","
            "\"highlighted_at\":\"2026-11-11T09:30:00+00:00\",\"highlight_url\":\"https://example.com/h/1\"}]}");
}

TEST(HighlightExportReadwise, EmptyTitleAuthorAndUntrustedClockAreLeftOut) {
  ReadwiseBatch batch(Book{"", ""});
  Highlight h;
  h.text = "Just the text";
  h.timestamp = 86400;  // a clock that was never set
  ASSERT_TRUE(batch.add(h));
  EXPECT_EQ(batch.body(),
            "{\"highlights\":[{\"text\":\"Just the text\",\"source_type\":\"vaultink\",\"category\":\"books\"}]}");

  ReadwiseBatch titled(Book{"Title \"only\"", ""});
  h.timestamp = MIN_TRUSTED_EPOCH - 1;
  h.order = 1;
  ASSERT_TRUE(titled.add(h));
  EXPECT_EQ(titled.body(),
            "{\"highlights\":[{\"text\":\"Just the text\",\"title\":\"Title \\\"only\\\"\","
            "\"source_type\":\"vaultink\",\"category\":\"books\",\"location\":1,\"location_type\":\"order\"}]}");
}

TEST(HighlightExportReadwise, TwoHighlightsAndAnEmptyOneSkipped) {
  ReadwiseBatch batch(Book{"T", "A"});
  Highlight a;
  a.text = "one";
  Highlight empty;
  Highlight b;
  b.text = "two";
  EXPECT_TRUE(batch.add(a));
  EXPECT_TRUE(batch.add(empty));
  EXPECT_TRUE(batch.add(b));
  EXPECT_EQ(batch.count(), 2u);
  EXPECT_EQ(batch.body(),
            "{\"highlights\":[{\"text\":\"one\",\"title\":\"T\",\"author\":\"A\",\"source_type\":\"vaultink\","
            "\"category\":\"books\"},{\"text\":\"two\",\"title\":\"T\",\"author\":\"A\","
            "\"source_type\":\"vaultink\",\"category\":\"books\"}]}");
  batch.reset();
  EXPECT_TRUE(batch.empty());
  EXPECT_EQ(batch.body(), "{\"highlights\":[]}");
}

TEST(HighlightExportReadwise, VeryLongFieldsAreCutAtTheLimits) {
  const std::string longText(READWISE_TEXT_MAX + 500, 'x');
  std::string greekTitle;
  for (size_t i = 0; i < READWISE_TITLE_MAX + 10; i++) greekTitle += "ω";
  ReadwiseBatch batch(Book{greekTitle, std::string_view()});
  Highlight h;
  h.text = longText;
  ASSERT_TRUE(batch.add(h));
  const std::string body = batch.body();
  const std::string expectedText = "\"text\":\"" + std::string(READWISE_TEXT_MAX, 'x') + "\",";
  EXPECT_NE(body.find(expectedText), std::string::npos);
  std::string cutTitle;
  for (size_t i = 0; i < READWISE_TITLE_MAX; i++) cutTitle += "ω";
  EXPECT_NE(body.find("\"title\":\"" + cutTitle + "\","), std::string::npos);
}

TEST(HighlightExportReadwise, BatchesOfTwentyAtMost) {
  EXPECT_EQ(readwiseBatchCount(0), 0u);
  EXPECT_EQ(readwiseBatchCount(1), 1u);
  EXPECT_EQ(readwiseBatchCount(20), 1u);
  EXPECT_EQ(readwiseBatchCount(21), 2u);
  EXPECT_EQ(readwiseBatchCount(45), 3u);

  // 45 short highlights: 20, 20, 5.
  ReadwiseBatch batch(Book{"T", ""});
  std::vector<size_t> sizes;
  Highlight h;
  h.text = "short";
  for (int i = 0; i < 45; i++) {
    if (!batch.add(h)) {
      sizes.push_back(batch.count());
      batch.reset();
      ASSERT_TRUE(batch.add(h));
    }
  }
  sizes.push_back(batch.count());
  EXPECT_EQ(sizes, (std::vector<size_t>{20, 20, 5}));
}

TEST(HighlightExportReadwise, LongHighlightsCloseABatchEarly) {
  // 4 KiB each: the body passes the soft limit after three, so the fourth starts a new request.
  const std::string text(4096, 'y');
  ReadwiseBatch batch(Book{"T", ""});
  Highlight h;
  h.text = text;
  EXPECT_TRUE(batch.add(h));
  EXPECT_TRUE(batch.add(h));
  EXPECT_TRUE(batch.add(h));
  EXPECT_FALSE(batch.add(h));
  EXPECT_EQ(batch.count(), 3u);
  EXPECT_LT(batch.body().size(), READWISE_SOFT_BODY_BYTES + 4096 + 200);

  // One highlight far past the limit (all control characters, six bytes each escaped) still goes, alone.
  const std::string worst(4096, '\x01');
  ReadwiseBatch alone(Book{"T", ""});
  h.text = worst;
  EXPECT_TRUE(alone.add(h));
  EXPECT_GT(alone.body().size(), READWISE_SOFT_BODY_BYTES);
  EXPECT_FALSE(alone.add(h));
}

TEST(HighlightExportMemos, GoldenBody) {
  Highlight h;
  h.text = "Πρώτη γραμμή \"με εισαγωγικά\"\r\n\r\nδεύτερη\tγραμμή\x01\n";
  h.chapter = "Κεφάλαιο 1";
  const Book book{"Ο Μικρός Πρίγκιπας", "Antoine de Saint-Exupéry"};
  EXPECT_EQ(memosContent(book, h),
            "> Πρώτη γραμμή \"με εισαγωγικά\"\n>\n> δεύτερη\tγραμμή\n\n"
            "Ο Μικρός Πρίγκιπας, Antoine de Saint-Exupéry (Κεφάλαιο 1)\n\n"
            "#highlights #vaultink #Ο_Μικρός_Πρίγκιπας #Antoine_de_Saint_Exupéry");
  EXPECT_EQ(memosBody(book, h),
            "{\"content\":\"> Πρώτη γραμμή \\\"με εισαγωγικά\\\"\\n>\\n> δεύτερη\\tγραμμή\\n\\n"
            "Ο Μικρός Πρίγκιπας, Antoine de Saint-Exupéry (Κεφάλαιο 1)\\n\\n"
            "#highlights #vaultink #Ο_Μικρός_Πρίγκιπας #Antoine_de_Saint_Exupéry\",\"visibility\":\"PRIVATE\"}");
}

TEST(HighlightExportMemos, EmptyTitleAndAuthor) {
  Highlight h;
  h.text = "Only a quote";
  EXPECT_EQ(memosBody(Book{"", ""}, h),
            "{\"content\":\"> Only a quote\\n\\n#highlights #vaultink\",\"visibility\":\"PRIVATE\"}");
  EXPECT_EQ(memosContent(Book{"", "Author Only"}, h),
            "> Only a quote\n\nAuthor Only\n\n#highlights #vaultink #Author_Only");
  h.chapter = "Ch";
  h.note = "mine";
  EXPECT_EQ(memosContent(Book{"A title:\nwith a break", ""}, h),
            "> Only a quote\n\nA title: with a break (Ch)\n\nmine\n\n#highlights #vaultink #A_title_with_a_break");
  // A title of punctuation only gives no tag.
  EXPECT_EQ(memosContent(Book{"...", ""}, h), "> Only a quote\n\n... (Ch)\n\nmine\n\n#highlights #vaultink");
}

TEST(HighlightExportMemos, LongTitleTagIsCutOnAWholeCharacter) {
  std::string title;
  for (int i = 0; i < 50; i++) title += "ω";  // 100 bytes
  Highlight h;
  h.text = "q";
  const std::string content = memosContent(Book{title, ""}, h);
  std::string tag;
  for (int i = 0; i < 30; i++) tag += "ω";  // 60 bytes
  EXPECT_EQ(content.substr(content.rfind('#')), "#" + tag);
}

TEST(HighlightExportMemos, VeryLongQuoteIsKeptWhole) {
  const std::string text(4096, 'z');
  Highlight h;
  h.text = text;
  const std::string body = memosBody(Book{"T", "A"}, h);
  EXPECT_NE(body.find("> " + text + "\\n\\nT, A"), std::string::npos);
}

TEST(HighlightExportMemos, ServerAddress) {
  EXPECT_EQ(memosUrl("https://memos.example.com"), "https://memos.example.com/api/v1/memos");
  EXPECT_EQ(memosUrl("https://memos.example.com///"), "https://memos.example.com/api/v1/memos");
  EXPECT_EQ(memosUrl("  memos.example.com:5230/ "), "https://memos.example.com:5230/api/v1/memos");
  EXPECT_EQ(memosUrl("HTTP://192.168.1.5:5230"), "http://192.168.1.5:5230/api/v1/memos");
  EXPECT_EQ(memosUrl("https://example.com/memos"), "https://example.com/memos/api/v1/memos");
  EXPECT_EQ(memosUrl("https://example.com/api/v1/memos"), "https://example.com/api/v1/memos");
  EXPECT_EQ(memosUrl(""), "");
  EXPECT_EQ(memosUrl("https://"), "");
  EXPECT_EQ(memosUrl("ftp://example.com"), "");
  EXPECT_EQ(memosUrl("https://exa mple.com"), "");
}

TEST(HighlightExportResponse, StatusToResult) {
  EXPECT_EQ(mapResponse(200, "").result, Result::Ok);
  EXPECT_EQ(mapResponse(201, "").result, Result::Ok);
  EXPECT_TRUE(mapResponse(204, "").ok());
  EXPECT_EQ(mapResponse(0, "").result, Result::NotConnected);
  EXPECT_EQ(mapResponse(-1, "").result, Result::NotConnected);
  EXPECT_EQ(mapResponse(401, "").result, Result::BadToken);
  EXPECT_EQ(mapResponse(403, "").result, Result::BadToken);
  EXPECT_EQ(mapResponse(404, "").result, Result::NotFound);
  EXPECT_EQ(mapResponse(400, "").result, Result::Rejected);
  EXPECT_EQ(mapResponse(413, "").result, Result::Rejected);
  EXPECT_EQ(mapResponse(302, "").result, Result::Rejected);  // redirects are not followed with a token
  EXPECT_EQ(mapResponse(500, "").result, Result::ServerError);
  EXPECT_EQ(mapResponse(503, "30").result, Result::ServerError);
  EXPECT_EQ(mapResponse(503, "30").retryAfterSeconds, 0u);
  EXPECT_EQ(mapResponse(100, "").result, Result::ServerError);
  EXPECT_FALSE(mapResponse(500, "").ok());
}

TEST(HighlightExportResponse, RateLimitAndRetryAfter) {
  EXPECT_EQ(mapResponse(429, "42").result, Result::RateLimited);
  EXPECT_EQ(mapResponse(429, "42").retryAfterSeconds, 42u);
  EXPECT_EQ(mapResponse(429, " 7 ").retryAfterSeconds, 7u);
  EXPECT_EQ(mapResponse(429, "").retryAfterSeconds, 0u);
  EXPECT_EQ(mapResponse(429, "Wed, 21 Oct 2026 07:28:00 GMT").retryAfterSeconds, 0u);
  EXPECT_EQ(mapResponse(429, "12abc").retryAfterSeconds, 0u);
  EXPECT_EQ(mapResponse(429, "-5").retryAfterSeconds, 0u);
  EXPECT_EQ(mapResponse(429, "99999999999").retryAfterSeconds, 0u);  // too long to be seconds
}

TEST(HighlightExportSent, KeyDependsOnIdAndDestination) {
  const uint64_t a = sentKey("0123456789abcdef0123456789abcdef", Destination::Readwise);
  EXPECT_EQ(a, sentKey("0123456789abcdef0123456789abcdef", Destination::Readwise));
  EXPECT_NE(a, sentKey("0123456789abcdef0123456789abcdef", Destination::Memos));
  EXPECT_NE(a, sentKey("0123456789abcdef0123456789abcdee", Destination::Readwise));
  EXPECT_NE(sentKey("", Destination::Readwise), 0u);
}

TEST(HighlightExportSent, RoundTrip) {
  SentMarkers markers;
  EXPECT_TRUE(markers.add(sentKey("a", Destination::Readwise)));
  EXPECT_TRUE(markers.add(sentKey("a", Destination::Memos)));
  EXPECT_TRUE(markers.add(sentKey("b", Destination::Readwise)));
  EXPECT_FALSE(markers.add(sentKey("b", Destination::Readwise)));  // already there
  EXPECT_EQ(markers.size(), 3u);

  const std::string file = markers.serialize();
  EXPECT_EQ(file.size(), 8u + 3 * 8 + 4);
  SentMarkers back;
  ASSERT_TRUE(back.parse(bytes(file), file.size()));
  EXPECT_EQ(back.size(), 3u);
  EXPECT_TRUE(back.contains(sentKey("a", Destination::Readwise)));
  EXPECT_TRUE(back.contains(sentKey("a", Destination::Memos)));
  EXPECT_TRUE(back.contains(sentKey("b", Destination::Readwise)));
  EXPECT_FALSE(back.contains(sentKey("b", Destination::Memos)));
  EXPECT_EQ(back.serialize(), file);

  // An empty set is a file too.
  const std::string none = SentMarkers().serialize();
  EXPECT_TRUE(back.parse(bytes(none), none.size()));
  EXPECT_EQ(back.size(), 0u);
}

TEST(HighlightExportSent, CorruptFileReadsAsEmpty) {
  SentMarkers markers;
  markers.add(1);
  markers.add(2);
  const std::string good = markers.serialize();

  SentMarkers m;
  m.add(99);
  EXPECT_FALSE(m.parse(nullptr, 0));
  EXPECT_EQ(m.size(), 0u);  // what was there is gone too
  EXPECT_FALSE(m.parse(bytes(good), 0));
  EXPECT_FALSE(m.parse(bytes(good), good.size() - 1));  // cut short

  std::string flipped = good;
  flipped[10] ^= 0x40;  // a key byte: the checksum catches it
  EXPECT_FALSE(m.parse(bytes(flipped), flipped.size()));
  EXPECT_EQ(m.size(), 0u);

  std::string magic = good;
  magic[0] = 'X';
  EXPECT_FALSE(m.parse(bytes(magic), magic.size()));

  std::string version = good;
  version[4] = 2;
  EXPECT_FALSE(m.parse(bytes(version), version.size()));

  std::string count = good;
  count[6] = 9;  // says nine keys, holds two
  EXPECT_FALSE(m.parse(bytes(count), count.size()));

  const std::string extra = good + "junk";
  EXPECT_FALSE(m.parse(bytes(extra), extra.size()));

  const std::string text = "this is not a marker file at all, just text";
  EXPECT_FALSE(m.parse(bytes(text), text.size()));

  EXPECT_TRUE(m.parse(bytes(good), good.size()));
  EXPECT_EQ(m.size(), 2u);
}

TEST(HighlightExportSent, OldestMarksAreDroppedAtTheCap) {
  SentMarkers markers;
  for (uint64_t k = 1; k <= SentMarkers::MAX_ENTRIES + 5; k++) markers.add(k);
  EXPECT_EQ(markers.size(), SentMarkers::MAX_ENTRIES);
  EXPECT_FALSE(markers.contains(5));
  EXPECT_TRUE(markers.contains(6));
  EXPECT_TRUE(markers.contains(SentMarkers::MAX_ENTRIES + 5));
  const std::string file = markers.serialize();
  SentMarkers back;
  EXPECT_TRUE(back.parse(bytes(file), file.size()));
  EXPECT_EQ(back.size(), SentMarkers::MAX_ENTRIES);
}

TEST(HighlightExportSent, FileNameFromTheBookPath) {
  const std::string name = sentFileName("/Books/Plato - Republic.epub");
  EXPECT_EQ(name.size(), 16u + 5);
  EXPECT_EQ(name.substr(16), ".sent");
  EXPECT_EQ(name.find_first_not_of("0123456789abcdef"), 16u);
  EXPECT_EQ(name, sentFileName("/Books/Plato - Republic.epub"));
  EXPECT_NE(name, sentFileName("/Books/Plato - Republic (2).epub"));
}
