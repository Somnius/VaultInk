#include <gtest/gtest.h>

#include <string>

#include "RtfToHtml.h"
#include "TestIo.h"

namespace {

struct Result {
  rtf::Status status;
  std::string page;
  std::string body;
};

Result convert(const std::string& source, const size_t step = 4096, const std::string& title = "T") {
  StringReader reader{source, step};
  StringSink sink;
  Result result;
  result.status = rtf::streamHtml(&reader, StringReader::read, title, StringSink::write, &sink);
  result.page = sink.text;
  result.body = bodyOf(sink.text);
  return result;
}

std::string body(const std::string& source) {
  const Result result = convert(source);
  EXPECT_EQ(result.status, rtf::Status::Ok);
  EXPECT_TRUE(wellFormed(result.page)) << result.page;
  return result.body;
}

}  // namespace

TEST(Rtf, ParagraphsBoldItalic) {
  EXPECT_EQ(body(R"({\rtf1\ansi\deff0 Hello {\b bold} and {\i italic}.\par Second \b on\b0  off\par})"),
            "<p>Hello <b>bold</b> and <i>italic</i>.</p><p>Second <b>on</b> off</p>");
}

TEST(Rtf, PageHeadAndVersionTag) {
  const Result result = convert(R"({\rtf1 x})", 4096, "A & B <c>");
  EXPECT_EQ(result.status, rtf::Status::Ok);
  EXPECT_NE(result.page.find(rtf::CACHE_VERSION_TAG), std::string::npos);
  EXPECT_LT(result.page.find(rtf::CACHE_VERSION_TAG), 80u);  // Txt::validateCache reads the first 79 bytes
  EXPECT_NE(result.page.find("<title>A &amp; B &lt;c&gt;</title>"), std::string::npos);
  EXPECT_TRUE(wellFormed(result.page));
}

TEST(Rtf, NestedBoldItalicClosesInOrder) {
  EXPECT_EQ(body(R"({\rtf1 {\b a{\i b}c}\i d\par})"), "<p><b>a</b><b><i>b</i></b><b>c</b><i>d</i></p>");
}

TEST(Rtf, EscapesMarkup) {
  EXPECT_EQ(body(R"({\rtf1 a<b & c>d \{x\} \\ \par})"), "<p>a&lt;b &amp; c&gt;d {x} \\ </p>");
}

TEST(Rtf, GreekThroughAnsiCodePage) {
  // "Καλημέρα" as \'hh in Windows-1253
  EXPECT_EQ(body(R"({\rtf1\ansi\ansicpg1253 \'ca\'e1\'eb\'e7\'ec\'dd\'f1\'e1\par})"), "<p>Καλημέρα</p>");
}

TEST(Rtf, GreekThroughFontCharset) {
  // Word's usual way: the document is 1252, the Greek font says \fcharset161.
  const std::string source = R"({\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\froman\fcharset0 Times;})"
                             R"({\f1\fswiss\fcharset161{\*\fname Arial;}Arial Greek;}})"
                             R"(caf\'e9 \f1\'ca\'e1\'eb\'e7\'ec\'dd\'f1\'e1\f0  caf\'e9\par})";
  EXPECT_EQ(body(source), "<p>café Καλημέρα café</p>");
}

TEST(Rtf, DefaultFontCharset) {
  EXPECT_EQ(body(R"({\rtf1\ansi\deff2{\fonttbl\f2\fnil\fcharset161 Helvetica;}\'e1\'e2\plain\'e3\par})"), "<p>αβγ</p>");
}

TEST(Rtf, UnicodeWithFallbackSkip) {
  // \uN then one fallback character (the default \uc1): a literal or a \'hh
  EXPECT_EQ(body(R"({\rtf1 \u922?\u945?\u955\'eb\u)"
                 "8212-x\\par}"),
            "<p>Καλ—x</p>");
  // \uc0: no fallback; \uc2: two, and only inside its group
  EXPECT_EQ(body(R"({\rtf1 \uc0\u945 \u946 b{\uc2\u947??c}\u948?d\par})"), "<p>αβbγcδ?d</p>");
  // negative values are 16-bit: -3913 is U+F0B7
  EXPECT_EQ(body(R"({\rtf1 \u-3913?\par})"), "<p>" + std::string("\xEF\x82\xB7") + "</p>");
  // half a surrogate pair is dropped
  EXPECT_EQ(body(R"({\rtf1 a\u-8704?b\u-10179?c\par})"), "<p>abc</p>");
  // a surrogate pair
  EXPECT_EQ(body(R"({\rtf1 \u-10179?\u-8704?\par})"), "<p>😀</p>");
  // the skip is per group and ends at a brace
  EXPECT_EQ(body(R"({\rtf1 {\u945}x\par})"), "<p>αx</p>");
}

TEST(Rtf, HeaderGroupsAreSkipped) {
  const std::string source =
      R"({\rtf1\ansi{\fonttbl{\f0 Times New Roman;}}{\colortbl;\red0\green0\blue0;}{\stylesheet{\s0 Normal;}})"
      R"({\*\generator Riched20 10.0;}{\info{\title My Book}{\author Some One}{\subject hidden}})"
      R"({\header page head}{\footer page foot}Text{\footnote a note}{\pict\pngblip 89504e47}{\*\unknown x} end\par})";
  EXPECT_EQ(body(source), "<p>Text end</p>");
}

TEST(Rtf, BinaryDataIsSteppedOver) {
  std::string source = R"({\rtf1 a{\pict\bin5 )";
  source += std::string("}{\\}\x00", 5);
  source += R"(}b\par})";
  EXPECT_EQ(body(source), "<p>ab</p>");
}

TEST(Rtf, TablesBecomeText) {
  EXPECT_EQ(body(R"({\rtf1 \trowd\cellx1000\cellx2000 A\cell B\cell\row\trowd C\cell\cell\row After\par})"),
            "<p>A | B</p><p>C</p><p>After</p>");
}

TEST(Rtf, SpecialCharactersAndBreaks) {
  EXPECT_EQ(body(R"({\rtf1 \ldblquote q\rdblquote\emdash\endash\bullet\~x\_y\-z\tab t\line n\par})"),
            "<p>“q”—–•\xC2\xA0x-yz t<br />n</p>");
}

TEST(Rtf, HeadingsAndAlignment) {
  EXPECT_EQ(body(R"({\rtf1 \pard\outlinelevel0 Chapter\par\pard\qc centred\par\pard\qr right\par\pard plain\par})"),
            "<h1>Chapter</h1><p style=\"text-align:center\">centred</p><p style=\"text-align:right\">right</p>"
            "<p>plain</p>");
}

TEST(Rtf, HiddenTextAndEmptyParagraphs) {
  EXPECT_EQ(body(R"({\rtf1 a{\v secret}b\par\par\par c\par})"), "<p>ab</p><p>c</p>");
}

TEST(Rtf, LineEndsAreNotText) { EXPECT_EQ(body("{\\rtf1 one\r\ntwo\\\nthree\\par}"), "<p>onetwo</p><p>three</p>"); }

TEST(Rtf, UnclosedFileStillCloses) {
  const Result result = convert(R"({\rtf1 {\b bold {\i both)");
  EXPECT_EQ(result.status, rtf::Status::Ok);
  EXPECT_TRUE(wellFormed(result.page));
  EXPECT_EQ(result.body, "<p><b>bold </b><b><i>both</i></b></p>");
}

TEST(Rtf, TextAfterTheLastBraceIsIgnored) { EXPECT_EQ(body(R"({\rtf1 a\par}junk{\b x})"), "<p>a</p>"); }

TEST(Rtf, NotRtf) {
  EXPECT_EQ(convert("Just text").status, rtf::Status::NotRtf);
  EXPECT_EQ(convert("").status, rtf::Status::NotRtf);
  EXPECT_TRUE(rtf::isRtf("\xEF\xBB\xBF{\\rtf1"));
  EXPECT_FALSE(rtf::isRtf("{\\rt"));
}

TEST(Rtf, DeeperThanTheCapStillReadsAndCloses) {
  std::string source = R"({\rtf1 )";
  for (size_t i = 0; i < rtf::Converter::MAX_DEPTH + 40; i++) source += R"({\b )";
  source += "deep";
  for (size_t i = 0; i < rtf::Converter::MAX_DEPTH + 40; i++) source += "}";
  source += R"( after\par})";
  EXPECT_EQ(body(source), "<p><b>deep</b> after</p>");
}

TEST(Rtf, LongControlWordIsIgnored) {
  EXPECT_EQ(body("{\\rtf1 a\\" + std::string(500, 'w') + "12 b\\par}"), "<p>ab</p>");
}

TEST(Rtf, MoreFontsThanTheCapUseTheDocumentPage) {
  std::string source = R"({\rtf1\ansi\ansicpg1252{\fonttbl)";
  for (size_t i = 0; i < rtf::Converter::MAX_FONTS + 5; i++) {
    source += "{\\f" + std::to_string(i) + "\\fcharset161 F;}";
  }
  source += "}\\f1\\'e1\\f" + std::to_string(rtf::Converter::MAX_FONTS + 2) + "\\'e1\\par}";
  EXPECT_EQ(body(source), "<p>αá</p>");
}

TEST(Rtf, SinkFailureIsReported) {
  StringReader reader{R"({\rtf1 some text\par})"};
  StringSink sink;
  sink.failAfter = 10;
  EXPECT_EQ(rtf::streamHtml(&reader, StringReader::read, "T", StringSink::write, &sink), rtf::Status::WriteError);
}

TEST(Rtf, ReadErrorIsReported) {
  const auto failing = [](void*, uint8_t*, size_t) { return -1; };
  StringSink sink;
  EXPECT_EQ(rtf::streamHtml(nullptr, failing, "T", StringSink::write, &sink), rtf::Status::IoError);
  rtf::Info info;
  EXPECT_EQ(rtf::readInfo(nullptr, failing, info), rtf::Status::IoError);
}

TEST(Rtf, EveryChunkingGivesTheSamePage) {
  const std::string source =
      R"({\rtf1\ansi\ansicpg1253\deff0{\fonttbl{\f0\fcharset161 Arial;}}{\info{\title \'d4\'df\'f4\'eb\'ef\'f2}})"
      R"(\pard\qc{\b \'c1\u946?}\par \u-10179?\u-8704? x{\pict\bin3 }{\}y\line z\cell w\row})";
  const Result whole = convert(source);
  EXPECT_EQ(whole.status, rtf::Status::Ok);
  EXPECT_EQ(whole.body,
            "<p style=\"text-align:center\"><b>Αβ</b></p><p style=\"text-align:center\">😀 xy<br />z | w</p>");
  for (size_t step = 1; step <= 9; step++) EXPECT_EQ(convert(source, step).page, whole.page) << "step " << step;
}

TEST(Rtf, InfoTitleAuthorLanguage) {
  const std::string source =
      R"({\rtf1\ansi\ansicpg1253\deff0\deflang1032{\fonttbl{\f0 Arial;}}{\stylesheet{\s0 Normal;}})"
      R"({\info{\title \'d4\'df\'f4\'eb\'ef\'f2 & \u914?}{\subject no}{\author \'a2\'ed\'ed\'e1}{\creatim\yr2020}}Body\par})";
  for (const size_t step : {size_t{1}, size_t{7}, size_t{4096}}) {
    StringReader reader{source, step};
    rtf::Info info;
    EXPECT_EQ(rtf::readInfo(&reader, StringReader::read, info), rtf::Status::Ok);
    EXPECT_EQ(info.title, "Τίτλος & Β");
    EXPECT_EQ(info.author, "Άννα");
    EXPECT_EQ(info.language, "el");
  }
}

TEST(Rtf, InfoIsCappedAndStopsAtTheText) {
  const std::string source =
      R"({\rtf1{\info{\title )" + std::string(1000, 'x') + R"(}}body )" + std::string(600 * 1024, 'y') + R"(\par})";
  StringReader reader{source, 1024};
  rtf::Info info;
  EXPECT_EQ(rtf::readInfo(&reader, StringReader::read, info), rtf::Status::Ok);
  EXPECT_EQ(info.title.size(), rtf::MAX_INFO_TEXT);
  EXPECT_LE(reader.at, 4096u);  // stopped once the info group closed

  // No info group at all: the scan stops at the first text.
  const std::string bare = R"({\rtf1 text )" + std::string(600 * 1024, 'y') + "}";
  StringReader bareReader{bare, 1024};
  EXPECT_EQ(rtf::readInfo(&bareReader, StringReader::read, info), rtf::Status::Ok);
  EXPECT_TRUE(info.title.empty());
  EXPECT_LE(bareReader.at, 2048u);

  // A header that never ends: the scan gives up at the limit.
  const std::string endless = R"({\rtf1{\stylesheet )" + std::string(600 * 1024, 's') + "}}";
  StringReader endlessReader{endless, 1024};
  EXPECT_EQ(rtf::readInfo(&endlessReader, StringReader::read, info), rtf::Status::Ok);
  EXPECT_LE(endlessReader.at, rtf::INFO_SCAN_LIMIT + 1024);
}

TEST(Rtf, NotRtfInfo) {
  StringReader reader{"<html>"};
  rtf::Info info;
  EXPECT_EQ(rtf::readInfo(&reader, StringReader::read, info), rtf::Status::NotRtf);
}

TEST(Rtf, GarbageNeverBreaksThePage) {
  // Every prefix of a busy document, and the document with each byte knocked out, still gives a well-formed page.
  const std::string source =
      R"({\rtf1\ansi\ansicpg1253{\fonttbl{\f0\fcharset161 A;}}{\b a\'e1\u945?{\i b}}\par\qc c\cell d\row{\*\x y}\bin2 zz\line e})";
  for (size_t cut = 0; cut <= source.size(); cut++) {
    const Result result = convert(source.substr(0, cut));
    if (result.status != rtf::Status::Ok) continue;  // cut inside "{\rtf": refused
    EXPECT_TRUE(wellFormed(result.page)) << "cut " << cut << "\n" << result.page;
  }
  for (size_t drop = 5; drop < source.size(); drop++) {
    std::string damaged = source;
    damaged.erase(drop, 1);
    const Result result = convert(damaged);
    ASSERT_EQ(result.status, rtf::Status::Ok);
    EXPECT_TRUE(wellFormed(result.page)) << "drop " << drop << "\n" << result.page;
  }
}
