#include <DocToHtml.h>
#include <gtest/gtest.h>

#include <fstream>
#include <random>
#include <sstream>

#include "DocFixture.h"
#include "TestIo.h"

using docfixture::buildDoc;
using docfixture::buildSummary;
using docfixture::DocOptions;
using docfixture::MemSource;
using docfixture::Piece;

namespace {

struct Converted {
  doc::Status status = doc::Status::Ok;
  std::string page;
  std::string body;
};

Converted convert(const std::string& file, const size_t step = 4096) {
  MemSource mem{file, step};
  StringSink sink;
  Converted result;
  result.status = doc::streamHtml(mem.source(), "T", StringSink::write, &sink);
  result.page = sink.text;
  result.body = bodyOf(sink.text);
  return result;
}

doc::Status infoOf(const std::string& file, doc::Info& info) {
  MemSource mem{file};
  return doc::readInfo(mem.source(), info);
}

std::string readFixture(const char* name) {
  std::ifstream in(std::string(MARKUP_FIXTURES) + "/" + name, std::ios::binary);
  std::ostringstream data;
  data << in.rdbuf();
  return data.str();
}

}  // namespace

TEST(DocToHtml, ParagraphsFromAnEightBitPiece) {
  const Converted c = convert(buildDoc({Piece::narrow("Hello\rWorld\r")}));
  EXPECT_EQ(c.status, doc::Status::Ok);
  EXPECT_EQ(c.body, "<p>Hello</p><p>World</p>");
  EXPECT_TRUE(wellFormed(c.page));
  EXPECT_NE(c.page.find(doc::CACHE_VERSION_TAG), std::string::npos);
}

TEST(DocToHtml, EightBitAndUtf16PiecesInOrder) {
  // 0x93 / 0x94 are the curly quotes and 0xE9 is e-acute in an 8-bit piece; Greek is a UTF-16 piece.
  const Converted c =
      convert(buildDoc({Piece::narrow("A \x93q\x94 caf\xE9 "), Piece::utf16(u"Βιβλίο\r"), Piece::narrow("end\r")}));
  EXPECT_EQ(c.status, doc::Status::Ok);
  EXPECT_EQ(c.body, "<p>A “q” café Βιβλίο</p><p>end</p>");
  EXPECT_TRUE(wellFormed(c.page));
}

TEST(DocToHtml, SurrogatePairsAndLoneSurrogates) {
  const Converted c =
      convert(buildDoc({Piece::utf16(u"a\xD83D\xDE00"
                                     u"b\xDC00"
                                     u"c\xD800\r")}));
  EXPECT_EQ(c.body,
            "<p>a\xF0\x9F\x98\x80"
            "bc</p>");
  EXPECT_TRUE(wellFormed(c.page));
}

TEST(DocToHtml, TableCellsAndRows) {
  const Converted c =
      convert(buildDoc({Piece::narrow("before\ra1\x07"
                                      "b1\x07\x07"
                                      "a2\x07"
                                      "b2\x07\x07"
                                      "after\r")}));
  EXPECT_EQ(c.body, "<p>before</p><p>a1 | b1</p><p>a2 | b2</p><p>after</p>");
}

TEST(DocToHtml, FieldShowsItsResultOnly) {
  const Converted c =
      convert(buildDoc({Piece::narrow("See \x13 HYPERLINK \"http://x\" \x14link text\x15 end\r"
                                      "No result \x13 XE \"index\" \x15here\r"
                                      "Nested \x13 IF \x13 PAGE \x14"
                                      "7\x15 = 7 \x14yes \x13 DATE \x14today\x15\x15.\r")}));
  EXPECT_EQ(c.body, "<p>See link text end</p><p>No result here</p><p>Nested yes today.</p>");
}

TEST(DocToHtml, FieldsDeeperThanTheCapAreHidden) {
  std::string text = "a";
  for (size_t i = 0; i < doc::MAX_FIELD_DEPTH + 3; i++) text += "\x13x\x14r";
  for (size_t i = 0; i < doc::MAX_FIELD_DEPTH + 3; i++) text += "\x15";
  text += "b\r";
  const Converted c = convert(buildDoc({Piece::narrow(text)}));
  EXPECT_EQ(c.body, "<p>a" + std::string(doc::MAX_FIELD_DEPTH, 'r') + "b</p>");
}

TEST(DocToHtml, AFieldLeftOpenDoesNotHideTheNotes) {
  DocOptions options;
  options.ccpFtn = 5;
  const Converted c = convert(buildDoc({Piece::narrow("text \x13 open\rnote\r")}, options));
  EXPECT_EQ(c.body, "<p>text </p><hr/><p>note</p>");
}

TEST(DocToHtml, SpecialCharacters) {
  const Converted c =
      convert(buildDoc({Piece::narrow("pic\x01\x08 tab\there line\x0B"
                                      "two a\x1E"
                                      "b c\x1F"
                                      "d <&>\x0Cnext\x02\x05 \r\r  \r")}));
  EXPECT_EQ(c.body, "<p>pic tab here line<br/>two a-b cd &lt;&amp;&gt;</p><p>next </p>");
  EXPECT_TRUE(wellFormed(c.page));
}

TEST(DocToHtml, FootnotesAndEndnotesFollowTheText) {
  DocOptions options;
  options.ccpFtn = 6;
  options.ccpHdd = 4;
  options.ccpEdn = 5;
  const Converted c = convert(buildDoc({Piece::narrow("main\r\x02"
                                                      "foot\rhdr\r"),
                                        Piece::utf16(u"\x0002"
                                                     u"end\r")},
                                       options));
  EXPECT_EQ(c.status, doc::Status::Ok);
  EXPECT_EQ(c.body, "<p>main</p><hr/><p>foot</p><hr/><p>end</p>");
}

TEST(DocToHtml, MiniStreamFragmentedAndDifatFiles) {
  const std::vector<Piece> pieces = {Piece::narrow("small\r"), Piece::utf16(u"α\r")};
  DocOptions mini;
  mini.wordPad = 0;  // the WordDocument stream is under 4096 bytes: it lives in the mini stream
  EXPECT_EQ(convert(buildDoc(pieces, mini)).body, "<p>small</p><p>α</p>");
  mini.fragment = true;
  EXPECT_EQ(convert(buildDoc(pieces, mini), 7).body, "<p>small</p><p>α</p>");

  DocOptions fragmented;
  fragmented.fragment = true;
  fragmented.wordPad = 20000;
  EXPECT_EQ(convert(buildDoc(pieces, fragmented), 100).body, "<p>small</p><p>α</p>");

  DocOptions big;
  big.extraStream = 8 * 1024 * 1024;  // more than 109 FAT sectors: the DIFAT sectors are followed
  const std::string file = buildDoc(pieces, big);
  ASSERT_GT(docfixture::buildCfb({}).size(), 0u);
  EXPECT_EQ(convert(file).body, "<p>small</p><p>α</p>");
}

TEST(DocToHtml, ZeroTableStream) {
  DocOptions options;
  options.flags = 0;
  EXPECT_EQ(convert(buildDoc({Piece::narrow("zero\r")}, options)).body, "<p>zero</p>");
}

TEST(DocToHtml, RefusesWhatItCannotRead) {
  doc::Info info;
  DocOptions encrypted;
  encrypted.flags = 0x0300;
  EXPECT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, encrypted), info), doc::Status::Encrypted);
  EXPECT_EQ(convert(buildDoc({Piece::narrow("x\r")}, encrypted)).status, doc::Status::Encrypted);
  encrypted.flags = 0x8300;  // XOR obfuscation
  EXPECT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, encrypted), info), doc::Status::Encrypted);

  DocOptions word95;
  word95.ident = 0xA5DC;
  EXPECT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, word95), info), doc::Status::Unsupported);

  DocOptions noWord;
  noWord.wordDocument = false;
  EXPECT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, noWord), info), doc::Status::NotDoc);

  EXPECT_EQ(infoOf("{\\rtf1 not a doc}" + std::string(600, ' '), info), doc::Status::NotDoc);
  EXPECT_EQ(infoOf("", info), doc::Status::NotDoc);
  EXPECT_EQ(infoOf("PK\x03\x04" + std::string(600, 'z'), info), doc::Status::NotDoc);  // a .docx renamed

  DocOptions manyPieces;
  manyPieces.piecesClaimed = doc::MAX_PIECES + 1;
  EXPECT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, manyPieces), info), doc::Status::TooBig);
  EXPECT_TRUE(convert(buildDoc({Piece::narrow("x\r")}, manyPieces)).page.empty());
}

TEST(DocToHtml, PiecesThatOverlapStopAtTheStreamSize) {
  // 40 pieces of 2000 bytes that all point at the same bytes: more text than the 4096-byte stream holds.
  std::vector<Piece> pieces(40, Piece::narrow(std::string(1999, 'x') + "\r"));
  DocOptions options;
  options.overlap = true;
  const Converted c = convert(buildDoc(pieces, options));
  EXPECT_EQ(c.status, doc::Status::Truncated);
  EXPECT_TRUE(wellFormed(c.page));
  EXPECT_LT(c.page.size(), 120000u);
}

TEST(DocToHtml, TitleAuthorAndLanguage) {
  DocOptions options;
  options.lid = 0x0408;
  options.summary = buildSummary(1253, "\xC2\xE9\xE2\xEB\xDF\xEF  ", " \xC1\xED\xED\xE1");
  doc::Info info;
  ASSERT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, options), info), doc::Status::Ok);
  EXPECT_EQ(info.title, "Βιβλίο");
  EXPECT_EQ(info.author, "Αννα");
  EXPECT_EQ(info.language, "el");

  options.lid = 0x0809;
  options.summary = buildSummary(1252, "", "Me", u"Wide τίτλος");
  doc::Info wide;
  ASSERT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, options), wide), doc::Status::Ok);
  EXPECT_EQ(wide.title, "Wide τίτλος");
  EXPECT_EQ(wide.author, "Me");
  EXPECT_EQ(wide.language, "en");

  options.summary = buildSummary(1252, std::string(1000, 'T'), "A");
  doc::Info longTitle;
  ASSERT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}, options), longTitle), doc::Status::Ok);
  EXPECT_EQ(longTitle.title.size(), doc::MAX_INFO_TEXT);

  doc::Info none;
  ASSERT_EQ(infoOf(buildDoc({Piece::narrow("x\r")}), none), doc::Status::Ok);
  EXPECT_TRUE(none.title.empty());
}

TEST(DocToHtml, SinkThatRefusesIsAWriteError) {
  MemSource mem{buildDoc({Piece::narrow(std::string(5000, 'a') + "\r")})};
  StringSink sink;
  sink.failAfter = 600;
  EXPECT_EQ(doc::streamHtml(mem.source(), "T", StringSink::write, &sink), doc::Status::WriteError);
}

TEST(DocToHtml, EveryShorterFileIsRefusedOrReadWithoutACrash) {
  DocOptions options;
  options.summary = buildSummary(1252, "Title", "Author");
  options.ccpFtn = 3;
  const std::string file = buildDoc({Piece::narrow("One \x13 A \x14"
                                                   "b\x15\r"),
                                     Piece::utf16(u"α\x0007\x0007\r"), Piece::narrow("n\r\r")},
                                    options);
  for (size_t len = 0; len < file.size(); len += (len < 1024 ? 1 : 37)) {
    const Converted c = convert(file.substr(0, len));
    if (!c.page.empty()) {
      EXPECT_TRUE(wellFormed(c.page)) << len;
    }
    doc::Info info;
    infoOf(file.substr(0, len), info);
  }
}

TEST(DocToHtml, DamagedFilesDoNotCrash) {
  DocOptions options;
  options.summary = buildSummary(1253, "Title", "Author");
  options.wordPad = 0;
  const std::string small = buildDoc({Piece::narrow("One\rTwo \x13 A \x14"
                                                    "b\x15\r"),
                                      Piece::utf16(u"α\r")},
                                     options);
  options.wordPad = 6000;
  options.fragment = true;
  const std::string large = buildDoc({Piece::narrow("One\rTwo\r"), Piece::utf16(u"α\r")}, options);
  const std::string real = readFixture("libreoffice.doc");
  ASSERT_FALSE(real.empty());
  std::mt19937 rng(20261113);
  size_t opened = 0;
  for (int round = 0; round < 6000; round++) {
    std::string file = round % 3 == 0 ? small : round % 3 == 1 ? large : real;
    const int edits = 1 + static_cast<int>(rng() % 6);
    for (int e = 0; e < edits; e++) {
      const size_t at = rng() % file.size();
      switch (rng() % 4) {
        case 0:
          file[at] = static_cast<char>(rng());
          break;
        case 1:
          file[at] = static_cast<char>(0xFF);
          break;
        case 2:
          file[at] = 0;
          break;
        default:  // a 32-bit field set to a big or a small number
          if (at + 4 <= file.size()) docfixture::put32(file, at, rng() % 2 ? rng() : rng() % 64);
          break;
      }
    }
    const Converted c = convert(file, 1 + rng() % 600);
    if (!c.page.empty()) {
      opened++;
      EXPECT_TRUE(wellFormed(c.page)) << round;
      EXPECT_LT(c.page.size(), 1u << 20) << round;
    }
    doc::Info info;
    infoOf(file, info);
  }
  EXPECT_GT(opened, 100u);  // the damage does not simply refuse everything
}

TEST(DocToHtml, RandomBytesAreRefused) {
  std::mt19937 rng(7);
  for (int round = 0; round < 300; round++) {
    std::string file(rng() % 3000, '\0');
    for (char& c : file) c = static_cast<char>(rng());
    if (round % 2 == 0 && file.size() >= 8) memcpy(file.data(), "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
    const Converted c = convert(file);
    EXPECT_NE(c.status, doc::Status::Ok);
  }
}

// A file LibreOffice's "MS Word 97" filter wrote from fixtures/libreoffice.fodt: the one file here that our own
// fixture writer did not make.
TEST(DocToHtml, AFileWrittenByLibreOffice) {
  const std::string file = readFixture("libreoffice.doc");
  ASSERT_FALSE(file.empty());
  doc::Info info;
  ASSERT_EQ(infoOf(file, info), doc::Status::Ok);
  EXPECT_EQ(info.title, "VaultInk test document");
  EXPECT_EQ(info.author, "Test Writer");
  const Converted c = convert(file, 333);
  EXPECT_EQ(c.status, doc::Status::Ok);
  EXPECT_TRUE(wellFormed(c.page));
  EXPECT_EQ(c.body,
            "<p>Chapter one</p>"
            "<p>First paragraph with a café and “quotes” &amp; an ampersand &lt;tag&gt;.</p>"
            "<p>Το βιβλίο είναι εδώ.</p>"
            "<p>A link: link text and after.</p>"
            "<p>Page 1 and a note here.</p>"
            "<p>a1 | b1</p><p>a2 | b2</p>"
            "<p>Line one<br/>line two tabbed.</p>"
            "<p>Last paragraph.</p>"
            "<hr/><p>The footnote text.</p>");
}
