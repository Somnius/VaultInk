#include <gtest/gtest.h>

#include <string>

#include "HtmlBook.h"
#include "TestIo.h"

namespace {

struct Result {
  htmlbook::Status status;
  std::string page;
  std::string body;
};

Result convert(const std::string& source, const size_t step = 4096, const std::string& title = "") {
  StringReader reader{source, step};
  StringSink sink;
  Result result;
  result.status = htmlbook::streamHtml(&reader, StringReader::read, title, StringSink::write, &sink);
  result.page = sink.text;
  result.body = bodyOf(sink.text);
  return result;
}

std::string body(const std::string& source) {
  const Result result = convert(source);
  EXPECT_EQ(result.status, htmlbook::Status::Ok);
  EXPECT_TRUE(wellFormed(result.page)) << result.page;
  return result.body;
}

htmlbook::Info sniffed(const std::string& head) {
  htmlbook::Info info;
  htmlbook::sniff(head, info);
  return info;
}

}  // namespace

TEST(HtmlBook, HeadingsParagraphsInline) {
  EXPECT_EQ(body("<html><head><title>T</title><style>p{color:red}</style></head><body>"
                 "<h1>One</h1><p>a <em>b</em> <strong>c</strong> <i>d</i> <b>e</b><br>f</p></body></html>"),
            "<h1>One</h1><p>a <em>b</em> <strong>c</strong> <i>d</i> <b>e</b><br />f</p>");
}

TEST(HtmlBook, PageHeadUsesThePageTitleOrTheGivenOne) {
  const std::string source = "<html><head><title> My &amp;  Book </title></head><body><p>x</p></body></html>";
  const Result own = convert(source);
  EXPECT_NE(own.page.find("<title>My &amp; Book</title>"), std::string::npos);
  EXPECT_LT(own.page.find(htmlbook::CACHE_VERSION_TAG), 80u);
  const Result given = convert(source, 4096, "Given");
  EXPECT_NE(given.page.find("<title>Given</title>"), std::string::npos);
  EXPECT_TRUE(wellFormed(own.page));
}

TEST(HtmlBook, ListsQuotesPre) {
  EXPECT_EQ(body("<ul><li>one<li>two</ul><ol><li>x</li></ol><blockquote><p>q</blockquote><pre>a\n  b</pre>"),
            "<ul><li>one</li><li>two</li></ul><ol><li>x</li></ol><blockquote><p>q</p></blockquote><pre>a  b</pre>");
}

TEST(HtmlBook, ScriptAndStyleAreDroppedWithTheirText) {
  EXPECT_EQ(body("<p>a</p><script>for (i=0;i<n;i++){ if (a<b && c>d) x(\"<\"); }</script>"
                 "<style>a > b { c: d } &amp;</style><p>b</p>"),
            "<p>a</p><p>b</p>");
}

TEST(HtmlBook, HeadWithoutClosingTagEndsAtBody) {
  EXPECT_EQ(body("<html><head><title>x &amp; y</title><meta charset=utf-8><body><p>text</p>"), "<p>text</p>");
}

TEST(HtmlBook, EntitiesAreDecoded) {
  EXPECT_EQ(body("<p>&alpha;&beta; &copy; &#945;&#x3b2; &amp; &lt; &gt; &nbsp;| a & b &bogus; </p>"),
            "<p>αβ © &#945;&#x3b2; &amp; &lt; &gt; \xC2\xA0| a &amp; b &amp;bogus; </p>");
}

TEST(HtmlBook, UnknownTagsKeepTheirTextAndNewerBlocksStayApart) {
  EXPECT_EQ(body("<section><custom-tag>a</custom-tag></section><article>b</article><dl><dt>t<dd>d</dl>"),
            "<div>a</div><div>b</div><div><div>t<div>d</div></div></div>");
}

TEST(HtmlBook, UnclosedTagsAreClosed) {
  const Result result = convert("<div><p>a <b>bold <i>both<p>next");
  EXPECT_EQ(result.status, htmlbook::Status::Ok);
  EXPECT_TRUE(wellFormed(result.page)) << result.page;
  EXPECT_EQ(result.body, "<div><p>a <b>bold <i>both</i></b></p><p>next</p></div>");
}

TEST(HtmlBook, BareTextAndStrayAngles) {
  EXPECT_EQ(body("1 < 2 and 3 > 2 & so on"), "1 &lt; 2 and 3 &gt; 2 &amp; so on");
}

TEST(HtmlBook, SniffsMetaCharset) {
  EXPECT_EQ(sniffed("<html><head><meta charset=\"windows-1253\">").encoding, codepage::Id::Cp1253);
  EXPECT_EQ(sniffed("<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-7'>").encoding,
            codepage::Id::Iso8859_7);
  EXPECT_EQ(sniffed("<?xml version=\"1.0\" encoding=\"windows-1251\"?><html>").encoding, codepage::Id::Cp1251);
  EXPECT_EQ(sniffed("<meta charset=utf-8>").encoding, codepage::Id::Utf8);
  EXPECT_EQ(sniffed("<meta charset=shift_jis>").encoding, codepage::Id::Utf8);  // not known: the fallback
  EXPECT_EQ(sniffed("<meta name=viewport content='width=1'>").encoding, codepage::Id::Utf8);
  // a BOM wins over the page
  const htmlbook::Info bom = sniffed("\xEF\xBB\xBF<meta charset=windows-1253>");
  EXPECT_EQ(bom.encoding, codepage::Id::Utf8);
  EXPECT_EQ(bom.bomSize, 3u);
  // nothing after <body> is looked at
  EXPECT_EQ(sniffed("<body><meta charset=windows-1253>").encoding, codepage::Id::Utf8);
  // a comment is not a tag
  EXPECT_EQ(sniffed("<!-- <meta charset=windows-1253> --><meta charset=windows-1252>").encoding, codepage::Id::Cp1252);
}

TEST(HtmlBook, SniffsLanguageAndTitle) {
  const htmlbook::Info info = sniffed(
      "<!DOCTYPE html><html lang=\"el-GR\"><head><meta charset=\"windows-1253\">"
      "<title>\n \xD4\xDF\xF4\xEB\xEF\xF2 &amp; &alpha;&#946;\n</title></head><body>");
  EXPECT_EQ(info.language, "el");
  EXPECT_EQ(info.title, "Τίτλος & αβ");
  EXPECT_EQ(sniffed("<html xml:lang='en'>").language, "en");
  EXPECT_EQ(sniffed("<html>").language, "");
  EXPECT_EQ(sniffed("<title>cut off, no end").title, "");
  const htmlbook::Info longTitle = sniffed("<title>" + std::string(150, 'x') + "ααααααααααααααααααααααααααααα</title>");
  EXPECT_LE(longTitle.title.size(), htmlbook::MAX_TITLE);
  EXPECT_GE(longTitle.title.size(), htmlbook::MAX_TITLE - 1);
  // cut at a whole character
  EXPECT_NE(static_cast<unsigned char>(longTitle.title.back()) & 0xC0, 0xC0);
}

TEST(HtmlBook, GreekInWindows1253) {
  EXPECT_EQ(body("<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=windows-1253\"></head>"
                 "<body><p>\xCA\xE1\xEB\xE7\xEC\xDD\xF1\xE1 \xA2</p></body></html>"),
            "<p>Καλημέρα Ά</p>");
}

TEST(HtmlBook, GreekInUtf8WithBom) {
  const Result result = convert("\xEF\xBB\xBF<p>Καλημέρα κόσμε</p>");
  EXPECT_EQ(result.body, "<p>Καλημέρα κόσμε</p>");
}

TEST(HtmlBook, UndeclaredBytesFallBackToWindows1252) {
  EXPECT_EQ(body("<p>caf\xE9 \x93q\x94 and real UTF-8 é</p>"), "<p>café “q” and real UTF-8 é</p>");
}

TEST(HtmlBook, Utf16IsRefused) {
  EXPECT_EQ(convert(std::string("\xFF\xFE<\0p\0>\0", 8)).status, htmlbook::Status::Unsupported);
  StringReader reader{std::string_view("\xFE\xFF\0<", 4)};
  htmlbook::Info info;
  EXPECT_EQ(htmlbook::readInfo(&reader, StringReader::read, info), htmlbook::Status::Unsupported);
}

TEST(HtmlBook, EmptyFileIsAnEmptyPage) {
  const Result result = convert("");
  EXPECT_EQ(result.status, htmlbook::Status::Ok);
  EXPECT_TRUE(wellFormed(result.page));
}

TEST(HtmlBook, ControlCharactersNeverReachThePage) {
  const Result result = convert(std::string("<p>a\0b\x01\x0C", 8) + "c &#1; &#0; d</p>");
  EXPECT_TRUE(wellFormed(result.page)) << result.page;
  EXPECT_EQ(result.body, "<p>abc   d</p>");
}

TEST(HtmlBook, EveryChunkingGivesTheSamePage) {
  const std::string source =
      "<html><head><meta charset=\"utf-8\"><title>Τίτλος</title><script>if(a<b){}</script></head>"
      "<body><h2>Κεφάλαιο</h2><p align=\"center\">Ελληνικά &amp; &alpha; <b>έντονα</b> — 😀</p>"
      "<ul><li>ένα<li>δύο</ul><!-- σχόλιο --><p>τέλος</body></html>";
  const Result whole = convert(source);
  EXPECT_EQ(whole.status, htmlbook::Status::Ok);
  EXPECT_TRUE(wellFormed(whole.page)) << whole.page;
  EXPECT_EQ(whole.body,
            "<h2>Κεφάλαιο</h2><p style=\"text-align:center\">Ελληνικά &amp; α <b>έντονα</b> — 😀</p>"
            "<ul><li>ένα</li><li>δύο</li></ul><p>τέλος</p>");
  // The first read is always asked for the whole sniff window; the reader hands it over in small pieces.
  for (size_t step = 1; step <= 9; step++) EXPECT_EQ(convert(source, step).page, whole.page) << "step " << step;
}

TEST(HtmlBook, LargePageIsReadInFixedChunks) {
  std::string source = "<html><body>";
  for (int i = 0; i < 20000; i++) source += "<p>Παράγραφος " + std::to_string(i) + "</p>\n";
  source += "</body></html>";
  StringReader reader{source};
  StringSink sink;
  EXPECT_EQ(htmlbook::streamHtml(&reader, StringReader::read, "T", StringSink::write, &sink), htmlbook::Status::Ok);
  EXPECT_LE(reader.maxRequest, htmlbook::SNIFF_SIZE);
  EXPECT_NE(sink.text.find("<p>Παράγραφος 19999</p>"), std::string::npos);
  EXPECT_TRUE(wellFormed(sink.text));
}

TEST(HtmlBook, SinkAndReadFailuresAreReported) {
  StringReader reader{"<p>some text that is long enough to be flushed to the sink at the very end</p>"};
  StringSink sink;
  sink.failAfter = 10;
  EXPECT_EQ(htmlbook::streamHtml(&reader, StringReader::read, "T", StringSink::write, &sink),
            htmlbook::Status::WriteError);
  const auto failing = [](void*, uint8_t*, size_t) { return -1; };
  EXPECT_EQ(htmlbook::streamHtml(nullptr, failing, "T", StringSink::write, &sink), htmlbook::Status::IoError);
  htmlbook::Info info;
  EXPECT_EQ(htmlbook::readInfo(nullptr, failing, info), htmlbook::Status::IoError);
}

TEST(HtmlBook, DamagedPagesStayWellFormed) {
  const std::string source =
      "<html><head><title>t</title></head><body><div><p align='right'>a &amp; <b>b</b>"
      "<table><tr><td>c</td></tr></table><!-- x --><img src='x.png'/><br/>&#945;</p></div></body>";
  for (size_t cut = 0; cut <= source.size(); cut++) {
    const Result result = convert(source.substr(0, cut));
    ASSERT_EQ(result.status, htmlbook::Status::Ok);
    EXPECT_TRUE(wellFormed(result.page)) << "cut " << cut << "\n" << result.page;
  }
  for (size_t drop = 0; drop < source.size(); drop++) {
    std::string damaged = source;
    damaged.erase(drop, 1);
    const Result result = convert(damaged);
    ASSERT_EQ(result.status, htmlbook::Status::Ok);
    EXPECT_TRUE(wellFormed(result.page)) << "drop " << drop << "\n" << result.page;
  }
}
