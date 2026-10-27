#include <expat.h>
#include <gtest/gtest.h>

#include <string>

#include "MarkdownToHtml.h"

namespace {

struct StringSink : MarkdownToHtml::Sink {
  std::string text;
  void write(std::string_view part) override { text.append(part); }
};

// Converts a whole note; `chunk` > 0 feeds it in pieces of that size to exercise streaming.
std::string convert(const std::string& md, size_t chunk = 0) {
  StringSink sink;
  MarkdownToHtml converter(sink);
  converter.begin("Note");
  if (chunk == 0) {
    converter.feed(md);
  } else {
    for (size_t i = 0; i < md.size(); i += chunk) converter.feed(std::string_view(md).substr(i, chunk));
  }
  converter.finish();
  return sink.text;
}

// The body only, without the XHTML head and tail.
std::string body(const std::string& md) {
  const std::string html = convert(md);
  const size_t start = html.find("<body>\n") + 7;
  const size_t end = html.rfind("</body>");
  return html.substr(start, end - start);
}

std::string inlineHtml(const std::string& md) {
  std::string out;
  MarkdownToHtml::inlineToHtml(md, out);
  return out;
}

// The chapter parser uses expat: every output must be well-formed XML.
bool wellFormed(const std::string& xml) {
  XML_Parser parser = XML_ParserCreate("UTF-8");
  const bool ok = XML_Parse(parser, xml.data(), static_cast<int>(xml.size()), 1) == XML_STATUS_OK;
  XML_ParserFree(parser);
  return ok;
}

}  // namespace

TEST(VaultMarkdown, Headings) {
  EXPECT_EQ(body("# Title\n## Sub Part ##\n"), "<h1 id=\"Title\">Title</h1>\n<h2 id=\"Sub-Part\">Sub Part</h2>\n");
  EXPECT_EQ(body("#tag is not a heading\n"), "<p><i>#tag</i> is not a heading</p>\n");
}

TEST(VaultMarkdown, ParagraphsKeepLineBreaks) {
  EXPECT_EQ(body("one\ntwo\n\nthree\n"), "<p>one<br/>two</p>\n<p>three</p>\n");
}

TEST(VaultMarkdown, Emphasis) {
  EXPECT_EQ(inlineHtml("**b** *i* ***bi*** ~~s~~ ==h=="), "<b>b</b> <i>i</i> <b><i>bi</i></b> <s>s</s> <u>h</u>");
  EXPECT_EQ(inlineHtml("snake_case_name"), "snake_case_name");
  EXPECT_EQ(inlineHtml("2 * 3 * 4"), "2 * 3 * 4");
}

TEST(VaultMarkdown, OverlappingEmphasisStaysNested) {
  const std::string html = inlineHtml("**a *b** c*");
  EXPECT_TRUE(wellFormed("<p>" + html + "</p>")) << html;
  EXPECT_TRUE(wellFormed("<p>" + inlineHtml("**unclosed *also") + "</p>"));
}

TEST(VaultMarkdown, CodeAndEscapes) {
  EXPECT_EQ(inlineHtml("`a < b` and \\*not\\*"), "a &lt; b and *not*");
  EXPECT_EQ(body("```cpp\nint x = 1 < 2;\n```\n"), "<pre>int x = 1 &lt; 2;\n</pre>\n");
}

TEST(VaultMarkdown, WikiLinks) {
  EXPECT_EQ(inlineHtml("[[Note]]"), "<a href=\"vault:Note\">Note</a>");
  EXPECT_EQ(inlineHtml("[[Note|Shown]]"), "<a href=\"vault:Note\">Shown</a>");
  EXPECT_EQ(inlineHtml("[[Note#Part]]"), "<a href=\"vault:Note#Part\">Note &gt; Part</a>");
  EXPECT_EQ(inlineHtml("[[Note#^abc]]"), "<a href=\"vault:Note#^abc\">Note</a>");
  EXPECT_EQ(inlineHtml("\\[[not a link]]"), "[[not a link]]");
}

TEST(VaultMarkdown, Embeds) {
  EXPECT_EQ(inlineHtml("![[pic.png|300]]"), "<img src=\"vault:pic.png\" alt=\"pic.png\" width=\"300\"/>");
  EXPECT_EQ(inlineHtml("![[Other note]]"), "<a href=\"vault:Other note\">Other note</a>");
  EXPECT_EQ(inlineHtml("![alt](img/a.jpg)"), "<img src=\"vault:img/a.jpg\" alt=\"alt\"/>");
}

TEST(VaultMarkdown, MarkdownAndBareLinks) {
  EXPECT_EQ(inlineHtml("[site](https://example.com)"), "<a href=\"https://example.com\">site</a>");
  EXPECT_EQ(inlineHtml("[other](other.md)"), "<a href=\"vault:other.md\">other</a>");
  EXPECT_EQ(inlineHtml("see https://example.com/x."),
            "see <a href=\"https://example.com/x\">https://example.com/x</a>.");
}

TEST(VaultMarkdown, CommentsAndHtmlAreHidden) {
  EXPECT_EQ(inlineHtml("a %%secret%% b <span>c</span>"), "a  b c");
  EXPECT_EQ(body("before\n%%\nhidden\n%%\nafter\n"), "<p>before</p>\n<p>after</p>\n");
}

TEST(VaultMarkdown, FrontmatterIsSkipped) {
  EXPECT_EQ(body("---\ntags: [a]\naliases: x\n---\ntext\n"), "<p>text</p>\n");
}

TEST(VaultMarkdown, Lists) {
  EXPECT_EQ(body("- a\n- b\n  - c\n- d\n"),
            "<ul>\n<li>a</li>\n<li>b<ul style=\"margin-left:1.2em\">\n<li>c</li></ul>\n</li>\n<li>d</li></ul>\n");
  EXPECT_EQ(body("3. three\n4. four\n"), "<ol start=\"3\">\n<li>three</li>\n<li>four</li></ol>\n");
  EXPECT_EQ(body("1. one\n2. two\n"), "<ol>\n<li>one</li>\n<li>two</li></ol>\n");
  EXPECT_EQ(body("- [ ] todo\n- [x] done\n"), "<ul>\n<li>[ ] todo</li>\n<li>[x] done</li></ul>\n");
}

TEST(VaultMarkdown, QuotesAndCallouts) {
  EXPECT_EQ(body("> said\n> twice\n"), "<blockquote style=\"margin-left:1em\"><p>said<br/>twice</p>\n</blockquote>\n");
  EXPECT_EQ(body("> [!warning] Careful\n> body\n"),
            "<blockquote style=\"margin-left:1em\"><p><b>Careful</b></p>\n<p>body</p>\n</blockquote>\n");
  EXPECT_EQ(body("> [!note]-\n> folded\n"),
            "<blockquote style=\"margin-left:1em\"><p><b>Note</b></p>\n<p>folded</p>\n</blockquote>\n");
}

TEST(VaultMarkdown, ListsInQuotes) {
  EXPECT_EQ(body("> Intro\n> - one\n> 2. two\n"),
            "<blockquote style=\"margin-left:1em\"><p>Intro</p>\n<p>\xe2\x80\xa2 one</p>\n<p>2. two</p>\n"
            "</blockquote>\n");
}

TEST(VaultMarkdown, NoteEmbeds) {
  struct StringSink final : MarkdownToHtml::Sink {
    std::string out;
    void write(std::string_view t) override { out.append(t); }
  } sink;
  MarkdownToHtml md(sink);
  md.setNoteLoader([](std::string_view name, std::string& text) {
    if (name != "Other") return false;
    text = "---\ntags: [x]\n---\nInner **text** and ![[Other]] again.\n";
    return true;
  });
  md.feed("Before\n![[Other]]\n![[Missing]]\n");
  md.finish(false);
  EXPECT_EQ(sink.out,
            "<p>Before</p>\n<blockquote style=\"margin-left:1em\"><p><b><a href=\"vault:Other\">Other</a></b></p>\n"
            "<p>Inner <b>text</b> and <a href=\"vault:Other\">Other</a> again.</p>\n</blockquote>\n"
            "<p><a href=\"vault:Missing\">Missing</a></p>\n");
}

TEST(VaultMarkdown, Footnotes) {
  EXPECT_EQ(inlineHtml("word[^1] and [^note]"),
            "word<a href=\"#fn-1\"><sup>1</sup></a> and <a href=\"#fn-note\"><sup>note</sup></a>");
  EXPECT_EQ(body("[^1]: The *text*.\n"), "<p id=\"fn-1\"><sup>1</sup> The <i>text</i>.</p>\n");
  EXPECT_EQ(inlineHtml("x[^α β]"), "x<a href=\"#fn-α-β\"><sup>α β</sup></a>");
  EXPECT_EQ(body("- item\n[^1]: note\n"), "<ul>\n<li>item</li></ul>\n<p id=\"fn-1\"><sup>1</sup> note</p>\n");
}

TEST(VaultMarkdown, Tables) {
  EXPECT_EQ(
      body("| A | B |\n|---|:-:|\n| 1 | [[x\\|y]] |\n"),
      "<table>\n<tr><th>A</th><th>B</th></tr>\n<tr><td>1</td><td><a href=\"vault:x\">y</a></td></tr>\n</table>\n");
  EXPECT_EQ(body("| not | a table\n"), "<p>| not | a table</p>\n");
}

TEST(VaultMarkdown, RulesAndBlockIds) {
  EXPECT_EQ(body("a\n\n---\n\nb ^id-1\n"), "<p>a</p>\n<hr/>\n<p id=\"^id-1\">b</p>\n");
}

TEST(VaultMarkdown, GreekPassesThrough) {
  EXPECT_EQ(body("# Καλημέρα κόσμε\nἘν ἀρχῇ ἦν ὁ λόγος\n"),
            "<h1 id=\"Καλημέρα-κόσμε\">Καλημέρα κόσμε</h1>\n<p>Ἐν ἀρχῇ ἦν ὁ λόγος</p>\n");
}

TEST(VaultMarkdown, StreamingGivesTheSameResult) {
  const std::string md =
      "---\na: b\n---\n# H\ntext **bold** [[link|x]]\n\n- a\n  - b\n\n> [!tip]\n> t\n\n| a | b |\n|---|---|\n| 1 | 2 "
      "|\n";
  const std::string whole = convert(md);
  for (const size_t chunk : {1u, 3u, 7u, 64u}) EXPECT_EQ(convert(md, chunk), whole) << "chunk " << chunk;
}

TEST(VaultMarkdown, OutputIsAlwaysWellFormed) {
  const char* notes[] = {
      "# A\n**b *c** d*\n- x\n    - deep\n  - mid\n> q\n```\n<code>\n```\n| a | b\n|---|---|\n| 1 |\n",
      "unclosed `code and [[link without end and ![[img.png and <b>raw",
      "- [ ] a\n> [!x]\n> - in quote\n\n1) one\n- switch type\n",
      "&amp; & < > \" ' \\& ==mark **bold ~~strike",
  };
  for (const char* note : notes) {
    const std::string html = convert(note);
    EXPECT_TRUE(wellFormed(html)) << html;
  }
}

TEST(VaultMarkdown, LongLinesStayOneParagraph) {
  // A line longer than MAX_LINE, cut right before a two-byte letter, then text that looks like a
  // heading: still one paragraph, still valid UTF-8 and XML.
  const std::string longLine = std::string(MarkdownToHtml::MAX_LINE - 1, 'a') + "é# Hi";
  const std::string html = convert(longLine + "\n");
  EXPECT_TRUE(wellFormed(html));
  EXPECT_EQ(html.find("<h1"), std::string::npos);
  EXPECT_NE(html.find("é# Hi</p>"), std::string::npos);
  // Inside a code block a long line doesn't close the block early.
  const std::string code = convert("```\n" + std::string(MarkdownToHtml::MAX_LINE, 'x') + "```\nafter\n```\n");
  EXPECT_TRUE(wellFormed(code));
  EXPECT_NE(code.find("```\nafter\n</pre>"), std::string::npos);
}

TEST(VaultMarkdown, ControlCharactersBecomeSpaces) {
  const std::string html =
      convert(std::string("a\x01"
                          "b\x0B"
                          "c\n"));
  EXPECT_TRUE(wellFormed(html));
  EXPECT_NE(html.find("<p>a b c</p>"), std::string::npos);
}

TEST(VaultMarkdown, EmbedSizes) {
  EXPECT_EQ(inlineHtml("![[a.png|300x200]]"), "<img src=\"vault:a.png\" alt=\"a.png\" width=\"300\"/>");
  EXPECT_EQ(inlineHtml("![[a.png|x200]]"), "<img src=\"vault:a.png\" alt=\"a.png\"/>");
}

TEST(VaultMarkdown, SameNoteHeadingLink) { EXPECT_EQ(inlineHtml("[[#Next up]]"), "<a href=\"#Next-up\">Next up</a>"); }

TEST(VaultMarkdown, BigTablesAreWrittenInParts) {
  std::string md = "| a | b |\n|---|---|\n";
  for (size_t r = 0; r < MarkdownToHtml::MAX_TABLE_ROWS + 10; ++r) md += "| " + std::to_string(r) + " | x |\n";
  const std::string html = convert(md);
  EXPECT_TRUE(wellFormed(html));
  size_t tables = 0;
  for (size_t p = html.find("<table>"); p != std::string::npos; p = html.find("<table>", p + 1)) ++tables;
  EXPECT_EQ(tables, 2u);
  EXPECT_NE(html.find("<td>409</td>"), std::string::npos);
}
