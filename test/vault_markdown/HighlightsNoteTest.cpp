#include <gtest/gtest.h>

#include "HighlightsNote.h"

using namespace highlightsnote;

TEST(HighlightsNote, BuildsTitleAuthorMarkerAndQuotes) {
  const std::string note = build("The Haunted Lighthouse", "A. Keeper",
                                 {{"Chapter one", "A storm rolled in."},
                                  {"Chapter one", "Ninety-nine steps."},
                                  {"Chapter two", "First paragraph.\n\n\n\nSecond paragraph."}});
  EXPECT_EQ(note,
            "# The Haunted Lighthouse\n*A. Keeper*\n%% VaultInk highlights %%\n"
            "\n## Chapter one\n"
            "\n> A storm rolled in.\n"
            "\n> Ninety-nine steps.\n"
            "\n## Chapter two\n"
            "\n> First paragraph.\n>\n> Second paragraph.\n");
}

TEST(HighlightsNote, NoAuthorNoChapterAndEmptyHighlights) {
  EXPECT_EQ(build("Book", "", {{"", "one"}, {"", "   "}, {"", "two"}}),
            "# Book\n%% VaultInk highlights %%\n\n> one\n\n> two\n");
  EXPECT_EQ(build("Empty", "x", {}), "# Empty\n*x*\n%% VaultInk highlights %%\n");
}

TEST(HighlightsNote, RecognisesItsOwnExports) {
  EXPECT_TRUE(isExport(build("T", "A", {{"", "x"}})));
  EXPECT_TRUE(isExport(build("T", "", {})));
  EXPECT_FALSE(isExport("# T\nmy own notes\n"));
  EXPECT_FALSE(isExport("%% VaultInk highlights %%\n"));
  EXPECT_FALSE(isExport(""));
}

TEST(HighlightsNote, FileNames) {
  EXPECT_EQ(fileNameFor("Dune: Part/One?"), "Dune_ Part_One_");
  EXPECT_EQ(fileNameFor("..hidden"), "hidden");
  EXPECT_EQ(fileNameFor(""), "Highlights");
  EXPECT_EQ(fileNameFor("..."), "Highlights");
  EXPECT_EQ(fileNameFor("Title. "), "Title");
  const std::string greek(150, 'x');
  EXPECT_EQ(fileNameFor(greek).size(), 100u);
  std::string utf8;
  for (int i = 0; i < 60; ++i) utf8 += "ή";  // two bytes each: 120 bytes, cut at 100 on a boundary
  EXPECT_EQ(fileNameFor(utf8).size(), 100u);
}
