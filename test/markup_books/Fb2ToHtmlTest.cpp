#include <gtest/gtest.h>

#include <string>

#include "Fb2ToHtml.h"
#include "TestIo.h"

namespace {

struct Result {
  fb2::Status status;
  std::string page;
  std::string body;
};

Result convert(const std::string& source, const size_t step = 4096, const std::string& title = "T") {
  StringReader reader{source, step};
  StringSink sink;
  Result result;
  result.status = fb2::streamHtml(&reader, StringReader::read, title, StringSink::write, &sink);
  result.page = sink.text;
  result.body = bodyOf(sink.text);
  return result;
}

std::string book(const std::string& bodies, const std::string& description = "") {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
         "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" "
         "xmlns:l=\"http://www.w3.org/1999/xlink\">\n<description>" +
         description + "</description>\n" + bodies + "\n</FictionBook>";
}

std::string body(const std::string& bodies) {
  const Result result = convert(book(bodies));
  EXPECT_EQ(result.status, fb2::Status::Ok);
  EXPECT_TRUE(wellFormed(result.page)) << result.page;
  return result.body;
}

fb2::Status info(const std::string& source, fb2::Info& out, const size_t step = 4096) {
  StringReader reader{source, step};
  return fb2::readInfo(&reader, StringReader::read, out);
}

const char* const TITLE_INFO =
    "<title-info><genre>prose</genre>"
    "<author><first-name>Νίκος</first-name><middle-name>Κ.</middle-name><last-name>Καζαντζάκης</last-name></author>"
    "<author><first-name>Second</first-name><last-name>Author</last-name></author>"
    "<book-title>Βίος &amp; Πολιτεία</book-title><annotation><p>About</p></annotation><lang>el</lang>"
    "<coverpage><image l:href=\"#cover.jpg\"/></coverpage></title-info>"
    "<document-info><author><nickname>maker</nickname></author></document-info>";

}  // namespace

TEST(Fb2, SectionsTitlesParagraphs) {
  EXPECT_EQ(body("<body><title><p>Book</p><p>Second line</p></title>"
                 "<section><title><p>Chapter 1</p></title><p>First.</p><p>Second.</p>"
                 "<section><title><p>Part</p></title><p>Deep.</p>"
                 "<section><title><p>Sub</p></title><section><title><p>SubSub</p></title></section></section>"
                 "</section></section></body>"),
            "<h1>Book<br />Second line</h1><h2>Chapter 1</h2><p>First.</p><p>Second.</p>"
            "<h3>Part</h3><p>Deep.</p><h4>Sub</h4><h4>SubSub</h4>");
}

TEST(Fb2, PageHeadAndVersionTag) {
  const Result result = convert(book("<body><p>x</p></body>"), 4096, "A & B");
  EXPECT_EQ(result.status, fb2::Status::Ok);
  EXPECT_LT(result.page.find(fb2::CACHE_VERSION_TAG), 80u);
  EXPECT_NE(result.page.find("<title>A &amp; B</title>"), std::string::npos);
}

TEST(Fb2, InlineStyles) {
  EXPECT_EQ(body("<body><section><p>a <emphasis>em <strong>both</strong></emphasis> <strong>st</strong> "
                 "<strikethrough>gone</strikethrough> x<sub>1</sub> y<sup>2</sup> <code>c</code> "
                 "<a l:href=\"http://x\">link</a><a l:href=\"#n1\" type=\"note\">[1]</a> "
                 "<style name=\"s\">styled</style></p></section></body>"),
            "<p>a <i>em <b>both</b></i> <b>st</b> <s>gone</s> x<sub>1</sub> y<sup>2</sup> <code>c</code> "
            "link<sup>[1]</sup> styled</p>");
}

TEST(Fb2, EscapesText) {
  EXPECT_EQ(body("<body><section><p>a &lt; b &amp; c &gt; d <![CDATA[<raw>]]></p></section></body>"),
            "<p>a &lt; b &amp; c &gt; d &lt;raw&gt;</p>");
}

TEST(Fb2, PoemsCitesEpigraphs) {
  EXPECT_EQ(body("<body><section><epigraph><p>Motto</p><text-author>Someone</text-author></epigraph>"
                 "<poem><title><p>Song</p></title><stanza><v>line one</v><v>line <emphasis>two</emphasis></v></stanza>"
                 "<stanza><v>next</v></stanza><text-author>Poet</text-author><date>1900</date></poem>"
                 "<cite><p>Quoted</p><subtitle>* * *</subtitle><text-author>Who</text-author></cite>"
                 "<subtitle>Sub</subtitle><empty-line/><p>After</p></section></body>"),
            "<blockquote><p>Motto</p><p style=\"text-align:right\"><i>Someone</i></p></blockquote>"
            "<blockquote><h4>Song</h4><p>line one<br />line <i>two</i></p><p>next</p>"
            "<p style=\"text-align:right\"><i>Poet</i></p><p style=\"text-align:right\"><i>1900</i></p></blockquote>"
            "<blockquote><p>Quoted</p><h4>* * *</h4><p style=\"text-align:right\"><i>Who</i></p></blockquote>"
            "<h4>Sub</h4><br /><p>After</p>");
}

TEST(Fb2, StanzaWithATitleBetweenLines) {
  EXPECT_EQ(
      body("<body><section><poem><stanza><v>a</v><title><p>T</p></title><v>b</v></stanza></poem></section></body>"),
      "<blockquote><p>a</p><h4>T</h4><p>b</p></blockquote>");
}

TEST(Fb2, Tables) {
  EXPECT_EQ(body("<body><section><table><tr><th>H</th><td>a <strong>b</strong></td></tr></table></section></body>"),
            "<table><tr><td>H</td><td>a <b>b</b></td></tr></table>");
}

TEST(Fb2, ImagesAndBinariesAreSkipped) {
  const std::string source = book(
      "<body><section><p>Before<image l:href=\"#p.png\"/>after</p><image l:href=\"#p.png\"/></section></body>"
      "<binary id=\"p.png\" content-type=\"image/png\">iVBORw0KGgoAAAANSUhEUgAA</binary>");
  const Result result = convert(source);
  EXPECT_EQ(result.status, fb2::Status::Ok);
  EXPECT_EQ(result.body, "<p>Beforeafter</p>");
  EXPECT_EQ(result.page.find("iVBOR"), std::string::npos);
}

TEST(Fb2, DescriptionIsNotText) {
  const Result result = convert(book("<body><p>Text</p></body>", TITLE_INFO));
  EXPECT_EQ(result.body, "<p>Text</p>");
}

TEST(Fb2, NotesBodyFollowsTheText) {
  EXPECT_EQ(body("<body><section><p>Text</p></section></body>"
                 "<body name=\"notes\"><title><p>Notes</p></title><section id=\"n1\"><title><p>1</p></title>"
                 "<p>The note.</p></section></body>"),
            "<p>Text</p><h1>Notes</h1><h2>1</h2><p>The note.</p>");
}

TEST(Fb2, GreekUtf8) {
  EXPECT_EQ(body("<body><section><title><p>Κεφάλαιο</p></title><p>Καλημέρα <emphasis>κόσμε</emphasis> — «ναι»</p>"
                 "</section></body>"),
            "<h2>Κεφάλαιο</h2><p>Καλημέρα <i>κόσμε</i> — «ναι»</p>");
}

TEST(Fb2, GreekWindows1253AndCyrillic1251) {
  const std::string greek =
      "<?xml version=\"1.0\" encoding=\"windows-1253\"?><FictionBook><description><title-info>"
      "<book-title>\xD4\xDF\xF4\xEB\xEF\xF2</book-title></title-info></description>"
      "<body><p>\xCA\xE1\xEB\xE7\xEC\xDD\xF1\xE1</p></body></FictionBook>";
  const Result result = convert(greek);
  EXPECT_EQ(result.status, fb2::Status::Ok);
  EXPECT_EQ(result.body, "<p>Καλημέρα</p>");
  fb2::Info found;
  EXPECT_EQ(info(greek, found), fb2::Status::Ok);
  EXPECT_EQ(found.title, "Τίτλος");

  const std::string russian =
      "<?xml version=\"1.0\" encoding=\"windows-1251\"?><FictionBook>"
      "<body><p>\xCF\xF0\xE8\xE2\xE5\xF2</p></body></FictionBook>";
  EXPECT_EQ(convert(russian).body, "<p>Привет</p>");
}

TEST(Fb2, Utf16AndLatin1) {
  // UTF-16LE with a BOM: every ASCII character and a zero, and the Greek alpha as B1 03.
  std::string utf16 = "\xFF\xFE";
  const auto add = [&utf16](const std::string& text) {
    for (const char c : text) {
      utf16 += c;
      utf16 += '\0';
    }
  };
  add("<FictionBook><body><p>a");
  utf16 += std::string("\xB1\x03", 2);
  add("</p></body></FictionBook>");
  for (const size_t step : {size_t{1}, size_t{3}, size_t{4096}}) {
    const Result result = convert(utf16, step);
    EXPECT_EQ(result.status, fb2::Status::Ok);
    EXPECT_EQ(result.body, "<p>aα</p>");
  }
  EXPECT_EQ(convert("<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><FictionBook><body><p>caf\xE9</p></body>"
                    "</FictionBook>")
                .body,
            "<p>café</p>");
  EXPECT_EQ(convert("<?xml version=\"1.0\" encoding=\"ISO-8859-7\"?><FictionBook><body><p>\xE1\xE2 \xB6</p></body>"
                    "</FictionBook>")
                .body,
            "<p>αβ Ά</p>");
}

TEST(Fb2, UnknownEncodingIsRefused) {
  const std::string source =
      "<?xml version=\"1.0\" encoding=\"koi8-r\"?><FictionBook><body><p>x</p></body></FictionBook>";
  EXPECT_EQ(convert(source).status, fb2::Status::Malformed);
  fb2::Info found;
  EXPECT_EQ(info(source, found), fb2::Status::Malformed);
}

TEST(Fb2, Info) {
  const std::string source = book("<body><p>" + std::string(300000, 'x') + "</p></body>", TITLE_INFO);
  for (const size_t step : {size_t{1}, size_t{13}, size_t{4096}}) {
    fb2::Info found;
    StringReader reader{source, step};
    EXPECT_EQ(fb2::readInfo(&reader, StringReader::read, found), fb2::Status::Ok);
    EXPECT_EQ(found.title, "Βίος & Πολιτεία");
    EXPECT_EQ(found.author, "Νίκος Κ. Καζαντζάκης");
    EXPECT_EQ(found.language, "el");
    EXPECT_LT(reader.at, 4096u);  // stops at the end of title-info, long before the text
  }
}

TEST(Fb2, InfoNicknameAndMissingParts) {
  fb2::Info found;
  EXPECT_EQ(info(book("<body/>", "<title-info><author><nickname>nick</nickname></author></title-info>"), found),
            fb2::Status::Ok);
  EXPECT_EQ(found.author, "nick");
  EXPECT_EQ(found.title, "");
  // no description at all: stops at the body
  fb2::Info none;
  EXPECT_EQ(info("<FictionBook><body><p>x</p></body></FictionBook>", none), fb2::Status::Ok);
  EXPECT_EQ(none.title, "");
}

TEST(Fb2, InfoIsCapped) {
  fb2::Info found;
  EXPECT_EQ(info(book("<body/>", "<title-info><book-title>" + std::string(199, 'x') + "ααα" + std::string(5000, 'y') +
                                     "</book-title></title-info>"),
                 found, 7),
            fb2::Status::Ok);
  EXPECT_EQ(found.title, std::string(199, 'x'));  // cut before the character that would pass the cap
}

TEST(Fb2, InfoScanStopsAtTheLimit) {
  // A description that goes on and on before title-info: the scan gives up, the caller uses the file name.
  const std::string source = book("<body/>", "<custom-info>" + std::string(200 * 1024, 'c') +
                                                 "</custom-info><title-info><book-title>Late"
                                                 "</book-title></title-info>");
  StringReader reader{source, 1024};
  fb2::Info found;
  EXPECT_EQ(fb2::readInfo(&reader, StringReader::read, found), fb2::Status::Ok);
  EXPECT_EQ(found.title, "");
  EXPECT_LE(reader.at, fb2::INFO_SCAN_LIMIT + 1024);
}

TEST(Fb2, NotFb2) {
  EXPECT_EQ(convert("<html><body><p>x</p></body></html>").status, fb2::Status::NotFb2);
  fb2::Info found;
  EXPECT_EQ(info("<html><body/></html>", found), fb2::Status::NotFb2);
  EXPECT_EQ(info("", found), fb2::Status::Malformed);
  EXPECT_EQ(info("PK\x03\x04 zipped", found), fb2::Status::Malformed);
  EXPECT_EQ(convert("not xml at all").status, fb2::Status::Malformed);
}

TEST(Fb2, CutShortFileKeepsWhatWasRead) {
  const std::string source =
      book("<body><section><title><p>One</p></title><p>First <emphasis>half").substr(0, std::string::npos);
  const std::string cut = source.substr(0, source.find("half") + 4);
  const Result result = convert(cut);
  EXPECT_EQ(result.status, fb2::Status::Truncated);
  EXPECT_TRUE(wellFormed(result.page)) << result.page;
  EXPECT_EQ(result.body, "<h2>One</h2><p>First <i>half</i></p>");
}

TEST(Fb2, EveryCutIsWellFormedOrRefused) {
  const std::string source = book(
      "<body><title><p>B</p></title><section><title><p>C</p></title><p>a <strong>b</strong></p>"
      "<poem><stanza><v>one</v><v>two</v></stanza><text-author>P</text-author></poem>"
      "<cite><p>q</p></cite><table><tr><td>x</td></tr></table><empty-line/></section></body>");
  for (size_t cut = 0; cut < source.size(); cut++) {
    const Result result = convert(source.substr(0, cut), 5);
    if (result.status == fb2::Status::Ok || result.status == fb2::Status::Truncated) {
      EXPECT_TRUE(wellFormed(result.page)) << "cut " << cut << "\n" << result.page;
    } else {
      EXPECT_TRUE(result.status == fb2::Status::Malformed || result.status == fb2::Status::NotFb2) << "cut " << cut;
    }
  }
}

TEST(Fb2, EveryChunkingGivesTheSamePage) {
  const std::string source = book(
      "<body><title><p>Τίτλος</p></title><section><p>Ελληνικά <strong>έντονα</strong> &amp; 😀</p>"
      "<poem><stanza><v>στίχος</v><v>άλλος</v></stanza></poem></section></body>"
      "<binary id=\"a\">QUJD\nREVG</binary>",
      TITLE_INFO);
  const Result whole = convert(source);
  EXPECT_EQ(whole.status, fb2::Status::Ok);
  EXPECT_EQ(whole.body,
            "<h1>Τίτλος</h1><p>Ελληνικά <b>έντονα</b> &amp; 😀</p>"
            "<blockquote><p>στίχος<br />άλλος</p></blockquote>");
  for (size_t step = 1; step <= 9; step++) EXPECT_EQ(convert(source, step).page, whole.page) << "step " << step;
}

TEST(Fb2, TooDeepIsRefused) {
  std::string nested;
  for (size_t i = 0; i < fb2::MAX_DEPTH + 10; i++) nested += "<section>";
  nested += "<p>x</p>";
  for (size_t i = 0; i < fb2::MAX_DEPTH + 10; i++) nested += "</section>";
  EXPECT_EQ(convert(book("<body>" + nested + "</body>")).status, fb2::Status::TooDeep);
  // ...and a deep but allowed book is read
  std::string allowed;
  for (size_t i = 0; i < 20; i++) allowed += "<section>";
  allowed += "<p>x</p>";
  for (size_t i = 0; i < 20; i++) allowed += "</section>";
  EXPECT_EQ(body("<body>" + allowed + "</body>"), "<p>x</p>");
}

TEST(Fb2, BigBinaryAndBigTextStayInsideTheMemoryCap) {
  // 2 MB of base64 and a 1 MB paragraph: character data is handed over as it is read, expat keeps none of it.
  std::string base64;
  for (int i = 0; i < 2 * 1024 * 1024 / 77; i++) base64 += std::string(76, 'A') + "\n";
  const std::string source = book("<body><section><p>" + std::string(1024 * 1024, 'x') +
                                  "</p><p>end</p></section></body>"
                                  "<binary id=\"big.jpg\" content-type=\"image/jpeg\">" +
                                  base64 + "</binary>");
  StringReader reader{source};
  StringSink sink;
  EXPECT_EQ(fb2::streamHtml(&reader, StringReader::read, "T", StringSink::write, &sink), fb2::Status::Ok);
  EXPECT_LE(reader.maxRequest, 1024u);
  EXPECT_NE(sink.text.find("<p>end</p>"), std::string::npos);
  EXPECT_LT(sink.text.size(), 1024u * 1024 + 4096);
}

TEST(Fb2, OneHugeTokenHitsTheMemoryCap) {
  // A comment is one token: expat has to hold all of it, so the cap stops it.
  const std::string source = book("<body><p>a</p><!--" + std::string(1024 * 1024, 'c') + "--><p>b</p></body>");
  const Result result = convert(source);
  EXPECT_EQ(result.status, fb2::Status::Oom);
  // The same with an attribute value.
  EXPECT_EQ(convert(book("<body><p id=\"" + std::string(1024 * 1024, 'i') + "\">a</p></body>")).status,
            fb2::Status::Oom);
  // The cap is not sticky: the next book opens.
  EXPECT_EQ(body("<body><p>ok</p></body>"), "<p>ok</p>");
}

TEST(Fb2, SinkAndReadFailuresAreReported) {
  const std::string source = book("<body><p>" + std::string(5000, 'x') + "</p></body>");
  StringReader reader{source};
  StringSink sink;
  sink.failAfter = 300;
  EXPECT_EQ(fb2::streamHtml(&reader, StringReader::read, "T", StringSink::write, &sink), fb2::Status::WriteError);
  const auto failing = [](void*, uint8_t*, size_t) { return -1; };
  EXPECT_EQ(fb2::streamHtml(nullptr, failing, "T", StringSink::write, &sink), fb2::Status::IoError);
  fb2::Info found;
  EXPECT_EQ(fb2::readInfo(nullptr, failing, found), fb2::Status::IoError);
}
