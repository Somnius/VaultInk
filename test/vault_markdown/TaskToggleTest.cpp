#include <gtest/gtest.h>

#include <string>

#include "MarkdownToHtml.h"
#include "TaskToggle.h"

using namespace tasktoggle;

namespace {
struct StringSink final : MarkdownToHtml::Sink {
  std::string text;
  void write(std::string_view part) override { text.append(part); }
};

std::string withTaskLinks(const std::string& md) {
  StringSink sink;
  MarkdownToHtml converter(sink);
  converter.setTaskLinks(true);
  converter.begin("t");
  converter.feed(md);
  converter.finish();
  return sink.text;
}

// The href of the n-th task link in the HTML.
std::string nthHref(const std::string& html, int n) {
  size_t pos = 0;
  for (int i = 0; i <= n; ++i) {
    pos = html.find("href=\"task:", pos);
    if (pos == std::string::npos) return "";
    pos += 6;
  }
  return html.substr(pos, html.find('"', pos) - pos);
}
}  // namespace

TEST(TaskToggle, HrefRoundTrip) {
  uint32_t line = 0, hash = 0;
  ASSERT_TRUE(parseHref(href(12, 0x1a2b3c4d), line, hash));
  EXPECT_EQ(line, 12u);
  EXPECT_EQ(hash, 0x1a2b3c4du);
  EXPECT_FALSE(parseHref("vault:Note", line, hash));
  EXPECT_FALSE(parseHref("task:12", line, hash));
  EXPECT_FALSE(parseHref("task:x:1", line, hash));
}

TEST(TaskToggle, MarkOffset) {
  EXPECT_EQ(markOffset("- [ ] a"), 3u);
  EXPECT_EQ(markOffset("  * [x] b"), 5u);
  EXPECT_EQ(markOffset("12. [ ] c"), 5u);
  EXPECT_EQ(markOffset("- [ ]"), 3u);
  EXPECT_EQ(markOffset("- [ ]x"), std::string_view::npos);
  EXPECT_EQ(markOffset("> - [ ] quoted"), std::string_view::npos);
  EXPECT_EQ(markOffset("plain [ ] text"), std::string_view::npos);
}

TEST(TaskToggle, ConverterLinksAndToggleMatch) {
  std::string md = "---\ntitle: x\n---\n# Tasks\n- [ ] one\n  - [x] two\n> - [ ] quoted\n1. [X] three\r\n";
  const std::string html = withTaskLinks(md);
  EXPECT_EQ(html.find("[ ] quoted") != std::string::npos, true);  // a quoted box stays plain
  uint32_t line = 0, hash = 0;
  ASSERT_TRUE(parseHref(nthHref(html, 0), line, hash));
  EXPECT_EQ(line, 4u);
  EXPECT_EQ(toggle(md, line, hash), Result::Ok);
  EXPECT_NE(md.find("- [x] one"), std::string::npos);
  ASSERT_TRUE(parseHref(nthHref(html, 1), line, hash));
  EXPECT_EQ(toggle(md, line, hash), Result::Ok);
  EXPECT_NE(md.find("  - [ ] two"), std::string::npos);
  ASSERT_TRUE(parseHref(nthHref(html, 2), line, hash));
  EXPECT_EQ(line, 7u);
  EXPECT_EQ(toggle(md, line, hash), Result::Ok);  // CRLF line: the hash ignores the \r
  EXPECT_NE(md.find("1. [ ] three\r\n"), std::string::npos);
  EXPECT_TRUE(nthHref(html, 3).empty());
}

TEST(TaskToggle, RefusesAChangedNote) {
  std::string md = "- [ ] one\n- [ ] two\n";
  const std::string html = withTaskLinks(md);
  uint32_t line = 0, hash = 0;
  ASSERT_TRUE(parseHref(nthHref(html, 1), line, hash));
  std::string edited = "new first line\n" + md;  // "two" moved down a line
  EXPECT_EQ(toggle(edited, line, hash), Result::Changed);
  EXPECT_EQ(edited, "new first line\n" + md);
  std::string shorter = "- [ ] one";  // no line 1 at all
  EXPECT_EQ(toggle(shorter, line, hash), Result::NotFound);
}

TEST(TaskToggle, ByteOrderMark) {
  std::string md = "\xEF\xBB\xBF- [ ] first\n";
  const std::string html = withTaskLinks(md.substr(3));  // the reader strips the mark before converting
  uint32_t line = 0, hash = 0;
  ASSERT_TRUE(parseHref(nthHref(html, 0), line, hash));
  EXPECT_EQ(toggle(md, line, hash), Result::Ok);
  EXPECT_EQ(md, "\xEF\xBB\xBF- [x] first\n");
}

TEST(TaskToggle, OffByDefault) {
  StringSink sink;
  MarkdownToHtml converter(sink);
  converter.begin("t");
  converter.feed("- [ ] a\n");
  converter.finish();
  EXPECT_EQ(sink.text.find("task:"), std::string::npos);
}
