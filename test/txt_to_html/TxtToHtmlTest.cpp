#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "Print.h"
#include "TxtToHtml.h"

namespace {

class StringPrint : public Print {
 public:
  std::string str;
  size_t write(uint8_t b) override {
    str.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    str.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
};

const std::string kHeader =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- TXT_CACHE_VERSION: 1 -->\n"
    "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n";
const std::string kFooter = "\n</body>\n</html>\n";

std::string convert(std::string_view content) {
  StringPrint out;
  EXPECT_TRUE(TxtToHtml::stream("test.txt", content, out));
  return out.str;
}

TEST(TxtToHtmlTest, LeadingSpacesIndentation) {
  EXPECT_EQ(convert("  two spaces"), kHeader + "&#160;&#160;two spaces" + kFooter);
  EXPECT_EQ(convert("    four spaces"), kHeader + "&#160;&#160;&#160;&#160;four spaces" + kFooter);
  EXPECT_EQ(convert("Line 1\n   three spaces"), kHeader + "Line 1<br />&#160;&#160;&#160;three spaces" + kFooter);
}

TEST(TxtToHtmlTest, MidLineConsecutiveSpaces) {
  EXPECT_EQ(convert("One space"), kHeader + "One space" + kFooter);
  EXPECT_EQ(convert("Two  spaces"), kHeader + "Two&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Three   spaces"), kHeader + "Three&#160;&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Four    spaces"), kHeader + "Four&#160;&#160;&#160; spaces" + kFooter);
}

TEST(TxtToHtmlTest, PreservesCacheVersionTagsForBothFormats) {
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("test.MD", "text", out));
  EXPECT_EQ(out.str,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- MD_CACHE_VERSION: 8 -->\n"
            "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n<p>text</p>\n</body>\n</html>\n");
  EXPECT_NE(out.str.find(TxtToHtml::cacheVersionTag("test.MD")), std::string::npos);
  EXPECT_NE(convert("text").find(TxtToHtml::cacheVersionTag("test.txt")), std::string::npos);
}

TEST(TxtToHtmlTest, TaskBoxesChangeTheCacheTagAndTheTasks) {
  TxtToHtml::setTaskBoxes(true);
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("n.md", "- [ ] a\n- [x] b\n", out));
  TxtToHtml::setTaskBoxes(false);
  EXPECT_NE(out.str.find("<!-- MD_CACHE_VERSION: 8 boxes -->"), std::string::npos);
  EXPECT_NE(out.str.find("\xE2\x98\x90</a> a"), std::string::npos);  // the tickable link holds the box
  EXPECT_NE(out.str.find("\xE2\x98\x91</a> b"), std::string::npos);
  EXPECT_EQ(out.str.find("[ ]"), std::string::npos);
  EXPECT_STRNE(TxtToHtml::cacheVersionTag("n.md"), "<!-- MD_CACHE_VERSION: 8 boxes -->");
}

// A PDF's converted text carries style marks (kos25k #29, 0.73.0): 1 + digit and 4 around a heading, 2 and 3 switch
// bold and italic. Styled turns them into tags, plain drops them.
std::string convertPdf(const std::string& content, const bool styled) {
  TxtToHtml::setPdfStyled(styled);
  StringPrint out;
  EXPECT_TRUE(TxtToHtml::stream("/.vaultink/pdf/123/Book.txt", content, out));
  TxtToHtml::setPdfStyled(true);
  return out.str;
}

TEST(TxtToHtmlTest, PdfMarksBecomeTagsWhenStyled) {
  const std::string text =
      "\x01"
      "2Title\x04\n\nSome \x02"
      "bold\x02"
      " and \x03"
      "italic\x03"
      " words.";
  const std::string html = convertPdf(text, true);
  EXPECT_NE(html.find("<h2>Title</h2>"), std::string::npos);
  EXPECT_NE(html.find("Some <b>bold</b> and <i>italic</i> words."), std::string::npos);
  EXPECT_NE(html.find("TXT_CACHE_VERSION: 2 "), std::string::npos);
}

TEST(TxtToHtmlTest, PdfMarksVanishWhenPlain) {
  const std::string text =
      "\x01"
      "2Title\x04\n\nSome \x02"
      "bold\x02"
      " words.";
  const std::string html = convertPdf(text, false);
  EXPECT_EQ(html.find("<h2>"), std::string::npos);
  EXPECT_EQ(html.find("<b>"), std::string::npos);
  EXPECT_NE(html.find("Title"), std::string::npos);
  EXPECT_NE(html.find("Some bold words."), std::string::npos);
  EXPECT_NE(html.find("TXT_CACHE_VERSION: 2 "), std::string::npos);
}

TEST(TxtToHtmlTest, OverlappingBoldAndItalicStayNested) {
  const std::string html = convertPdf(
      "\x02"
      "a\x03"
      "b\x02"
      "c\x03 end",
      true);
  EXPECT_NE(html.find("<b>a</b><b><i>b</i></b><i>c</i> end"), std::string::npos);
}

TEST(TxtToHtmlTest, MarksOutsideAPdfCacheAreNotMarks) {
  EXPECT_EQ(convert("a\x02"
                    "b")
                .find("<b>"),
            std::string::npos);
}

TEST(TxtToHtmlTest, AHeadingNeverSitsInsideBoldOrAnotherHeading) {
  const std::string html = convertPdf(
      "\x02"
      "bold \x01"
      "1One\x01"
      "2Two\x04 tail \x03"
      "x\x04",
      true);
  EXPECT_EQ(html.find("<b><h"), std::string::npos);
  EXPECT_EQ(html.find("<h1>One<h2>"), std::string::npos);
  EXPECT_NE(html.find("<h1>One</h1><h2>Two</h2>"), std::string::npos);
}

}  // namespace
