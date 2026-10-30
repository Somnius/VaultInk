#include <gtest/gtest.h>

#include <string>

#include "LinkRewrite.h"

namespace {

// Targets that lead to the renamed note "Notes/Old.md" in these tests.
bool pointsToOld(std::string_view target) {
  return target == "Old" || target == "Notes/Old" || target == "Old.md" || target == "Notes/Old.md";
}

std::string run(std::string text, size_t* changed = nullptr) {
  const size_t n = linkrewrite::rewrite(text, pointsToOld, "New");
  if (changed) *changed = n;
  return text;
}

}  // namespace

TEST(LinkRewriteTest, PlainLinkAliasAnchorAndEmbed) {
  size_t n = 0;
  EXPECT_EQ(run("See [[Old]].\n", &n), "See [[New]].\n");
  EXPECT_EQ(n, 1u);
  EXPECT_EQ(run("[[Old|the old one]] and [[Old#Some heading]] and [[Old#^abc123]]\n", &n),
            "[[New|the old one]] and [[New#Some heading]] and [[New#^abc123]]\n");
  EXPECT_EQ(n, 3u);
  EXPECT_EQ(run("![[Old]]\n![[Old#Part]]\n"), "![[New]]\n![[New#Part]]\n");
}

TEST(LinkRewriteTest, KeepsFolderPrefixAndMdEnding) {
  EXPECT_EQ(run("[[Notes/Old]] [[Old.md]] [[Notes/Old.md|x]]"), "[[Notes/New]] [[New.md]] [[Notes/New.md|x]]");
}

TEST(LinkRewriteTest, LeavesOtherLinksAndTextAlone) {
  size_t n = 0;
  const std::string text = "[[Older]] [[Other/Old]] Old [[ Old ]]\n";
  EXPECT_EQ(run(text, &n), text);
  EXPECT_EQ(n, 0u);
}

TEST(LinkRewriteTest, SkipsFencedCodeAndInlineCode) {
  const std::string text =
      "```\n[[Old]]\n```\n"
      "~~~md\n[[Old]]\n~~~\n"
      "Use `[[Old]]` or ``a `[[Old]]` b`` but [[Old]] counts.\n";
  size_t n = 0;
  EXPECT_EQ(run(text, &n),
            "```\n[[Old]]\n```\n"
            "~~~md\n[[Old]]\n~~~\n"
            "Use `[[Old]]` or ``a `[[Old]]` b`` but [[New]] counts.\n");
  EXPECT_EQ(n, 1u);
}

TEST(LinkRewriteTest, TableAliasBarKeepsBackslash) {
  EXPECT_EQ(run("| a | [[Old\\|shown]] |\n"), "| a | [[New\\|shown]] |\n");
}

TEST(LinkRewriteTest, UnchangedTextIsByteForByte) {
  const std::string text = "No links here.\r\nSecond line without newline";
  size_t n = 7;
  EXPECT_EQ(run(text, &n), text);
  EXPECT_EQ(n, 0u);
}

TEST(LinkRewriteTest, KeepsCrLfAndGreekNames) {
  std::string text = "Γραμμή [[Old]]\r\n[[Old|Παλιό]]\r\n";
  EXPECT_EQ(linkrewrite::rewrite(text, pointsToOld, "Νέο"), 2u);
  EXPECT_EQ(text, "Γραμμή [[Νέο]]\r\n[[Νέο|Παλιό]]\r\n");
}

TEST(LinkRewriteTest, UnclosedBracketsAndBackticksDoNotLoop) {
  EXPECT_EQ(run("[[Old and no end\n`open code [[Old]]\n"), "[[Old and no end\n`open code [[Old]]\n");
}

TEST(LinkRewriteTest, MarkdownLinks) {
  size_t n = 0;
  EXPECT_EQ(run("[the old](Old.md) and [x](Notes/Old.md#Part) and ![[Old]]\n", &n),
            "[the old](New.md) and [x](Notes/New.md#Part) and ![[New]]\n");
  EXPECT_EQ(n, 3u);
}

TEST(LinkRewriteTest, MarkdownLinkNamesWithSpacesAreEncoded) {
  size_t n = 0;
  std::string text = "[a](Notes/Old.md)\n";
  n = linkrewrite::rewrite(text, pointsToOld, "New Name");
  EXPECT_EQ(text, "[a](Notes/New%20Name.md)\n");
  EXPECT_EQ(n, 1u);
  text = "[a](Notes/New%20Name.md)\n";  // already-encoded spaces are decoded for `points`
  n = linkrewrite::rewrite(text, [](std::string_view target) { return target == "Notes/New Name.md"; }, "Third");
  EXPECT_EQ(text, "[a](Notes/Third.md)\n");
  EXPECT_EQ(n, 1u);
}

TEST(LinkRewriteTest, MarkdownLinksToOtherThingsStay) {
  const std::string text =
      "[web](https://example.com/Old.md) [pic](Old.png) ![img](Old.md.png) [t](Old.md \"title\") [a](<Old.md>) "
      "`[c](Old.md)`\n";
  size_t n = 0;
  EXPECT_EQ(run(text, &n), text);
  EXPECT_EQ(n, 0u);
}
