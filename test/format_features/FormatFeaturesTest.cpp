#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "activities/reader/FormatFeatures.h"
#include "util/MenuLayout.h"

using formatfeatures::ALL_KINDS;
using formatfeatures::BookKind;
using formatfeatures::bookKind;

namespace {
constexpr uint32_t ALL_ROWS = (1u << formatfeatures::TEXT_ROW_COUNT) - 1u;
constexpr uint32_t STYLE_ROW = 1u << formatfeatures::TEXT_ROW_EMBEDDED_STYLE;

// The Text panel rows the reader shows, as EpubReaderActivity::buildTextRows builds them.
std::vector<int> shownRows(const BookKind kind, const std::string& order, const uint32_t hidden) {
  std::vector<int> defaults;
  for (int i = 0; i < formatfeatures::TEXT_ROW_COUNT; ++i) {
    if (formatfeatures::showsTextRow(kind, i)) defaults.push_back(i);
  }
  return menulayout::arrange(defaults, order, hidden, 0);
}

bool has(const std::vector<int>& rows, const int row) { return std::find(rows.begin(), rows.end(), row) != rows.end(); }
}  // namespace

TEST(FormatFeatures, TheKindComesFromThePath) {
  EXPECT_EQ(bookKind("/books/a.epub"), BookKind::Epub);
  EXPECT_EQ(bookKind("/books/a.txt"), BookKind::Txt);
  EXPECT_EQ(bookKind("/books/A.TXT"), BookKind::Txt);
  EXPECT_EQ(bookKind("/vault/note.md"), BookKind::Note);
  EXPECT_EQ(bookKind("/vault/NOTE.MD"), BookKind::Note);
  EXPECT_EQ(bookKind("/.vaultink/pdf/1234.txt"), BookKind::PdfText);
  EXPECT_EQ(bookKind("/books/.vaultink/pdf/a.txt"), BookKind::Txt);  // only the folder at the card's root
  EXPECT_EQ(bookKind("/.vaultink/pdf/a.md"), BookKind::Note);
  for (const char* path : {"/b/a.mobi", "/b/a.prc", "/b/a.azw", "/b/a.AZW3"}) {
    EXPECT_EQ(bookKind(path), BookKind::Mobi) << path;
  }
  EXPECT_EQ(bookKind("/b/a.fb2"), BookKind::Fb2);
  for (const char* path : {"/b/a.html", "/b/a.htm", "/b/a.xhtml"}) EXPECT_EQ(bookKind(path), BookKind::Html) << path;
  EXPECT_EQ(bookKind("/b/a.rtf"), BookKind::Rtf);
  EXPECT_EQ(bookKind("/b/a.doc"), BookKind::Doc);
  EXPECT_EQ(bookKind("/b/a.chm"), BookKind::Chm);
}

TEST(FormatFeatures, AnUnknownPathShowsEverything) {
  for (const char* path : {"", "x", "/b/a.docx", "/b/a.txt.bak", "/b/txt"}) {
    EXPECT_EQ(bookKind(path), BookKind::Epub) << path;
    EXPECT_EQ(formatfeatures::textRowMask(bookKind(path)), ALL_ROWS) << path;
  }
}

TEST(FormatFeatures, TheMaskIsNeverEmptyAndNamesOnlyRealRows) {
  for (const BookKind kind : ALL_KINDS) {
    const uint32_t mask = formatfeatures::textRowMask(kind);
    EXPECT_NE(mask, 0u);
    EXPECT_EQ(mask & ~ALL_ROWS, 0u);
  }
}

// Every row has an answer for every kind, and the Style row is the only one a format leaves out. A new row changes
// TEXT_ROW_COUNT (the static_assert in TextPanelRows.h asks for it), and this test with it.
TEST(FormatFeatures, EveryRowIsCoveredAndOnlyStyleIsLeftOut) {
  ASSERT_EQ(formatfeatures::TEXT_ROW_COUNT, 15);
  for (const BookKind kind : ALL_KINDS) {
    for (int row = 0; row < formatfeatures::TEXT_ROW_COUNT; ++row) {
      const bool expected =
          row != formatfeatures::TEXT_ROW_EMBEDDED_STYLE || formatfeatures::embeddedStyleApplies(kind);
      EXPECT_EQ(formatfeatures::showsTextRow(kind, row), expected) << "row " << row;
    }
    EXPECT_FALSE(formatfeatures::showsTextRow(kind, -1));
    EXPECT_FALSE(formatfeatures::showsTextRow(kind, formatfeatures::TEXT_ROW_COUNT));
  }
}

TEST(FormatFeatures, StyleRowPerKind) {
  // Plain paragraphs only: nothing for the Style row to switch.
  for (const BookKind kind : {BookKind::Txt, BookKind::Note, BookKind::Doc}) {
    EXPECT_FALSE(formatfeatures::embeddedStyleApplies(kind));
    EXPECT_EQ(formatfeatures::textRowMask(kind), ALL_ROWS & ~STYLE_ROW);
  }
  // Style sheets (EPUB), heading / bold / italic marks (PDF text), a paragraph alignment (the rest).
  for (const BookKind kind : {BookKind::Epub, BookKind::PdfText, BookKind::Mobi, BookKind::Fb2, BookKind::Html,
                              BookKind::Rtf, BookKind::Chm}) {
    EXPECT_TRUE(formatfeatures::embeddedStyleApplies(kind));
    EXPECT_EQ(formatfeatures::textRowMask(kind), ALL_ROWS);
  }
}

TEST(FormatFeatures, BookStyleAlignmentOnlyWhereThePageHasAnAlignment) {
  for (const BookKind kind : {BookKind::Txt, BookKind::Note, BookKind::Doc, BookKind::PdfText}) {
    EXPECT_FALSE(formatfeatures::bookAlignmentApplies(kind));
    EXPECT_EQ(formatfeatures::alignmentChoiceCount(kind), 4);
    EXPECT_EQ(formatfeatures::alignmentChoiceShown(kind, 4), 0);  // a stored Book's style shows as Justify
  }
  for (const BookKind kind :
       {BookKind::Epub, BookKind::Mobi, BookKind::Fb2, BookKind::Html, BookKind::Rtf, BookKind::Chm}) {
    EXPECT_TRUE(formatfeatures::bookAlignmentApplies(kind));
    EXPECT_EQ(formatfeatures::alignmentChoiceCount(kind), 5);
    EXPECT_EQ(formatfeatures::alignmentChoiceShown(kind, 4), 4);
  }
  for (const BookKind kind : ALL_KINDS) {
    for (int stored = 0; stored < 4; ++stored) EXPECT_EQ(formatfeatures::alignmentChoiceShown(kind, stored), stored);
    EXPECT_EQ(formatfeatures::alignmentChoiceShown(kind, -1), 0);
    EXPECT_EQ(formatfeatures::alignmentChoiceShown(kind, 9), 0);
  }
}

TEST(FormatFeatures, ContentsNeedsMoreThanOneEntry) {
  EXPECT_FALSE(formatfeatures::showContents(-1));
  EXPECT_FALSE(formatfeatures::showContents(0));
  EXPECT_FALSE(formatfeatures::showContents(1));
  EXPECT_TRUE(formatfeatures::showContents(2));
  EXPECT_TRUE(formatfeatures::showContents(500));
}

TEST(FormatFeatures, AStoredOrderStaysValidWhenARowIsLeftOut) {
  const std::string order = menulayout::encode({12, 3});  // Style first, then alignment
  const std::vector<int> epub = shownRows(BookKind::Epub, order, 0);
  ASSERT_EQ(epub.size(), 15u);
  EXPECT_EQ(epub[0], 12);
  EXPECT_EQ(epub[1], 3);
  const std::vector<int> txt = shownRows(BookKind::Txt, order, 0);
  ASSERT_EQ(txt.size(), 14u);
  EXPECT_EQ(txt[0], 3);  // the order goes on with the next row it names
  EXPECT_FALSE(has(txt, 12));
  // Rows keep their numbers: nothing is renumbered around the missing one.
  EXPECT_TRUE(has(txt, 13));
  EXPECT_TRUE(has(txt, 14));
}

TEST(FormatFeatures, HidingEveryOtherRowNeverShowsALeftOutRow) {
  // Everything hidden but Style: an EPUB shows that one row, a TXT falls back to its own rows, never to Style.
  const uint32_t hidden = ALL_ROWS & ~STYLE_ROW;
  EXPECT_EQ(shownRows(BookKind::Epub, "", hidden), (std::vector<int>{12}));
  const std::vector<int> txt = shownRows(BookKind::Txt, "", hidden);
  EXPECT_EQ(txt.size(), 14u);
  EXPECT_FALSE(has(txt, 12));
}
