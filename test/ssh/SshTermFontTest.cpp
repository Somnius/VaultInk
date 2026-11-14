#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "SshTermFont.h"
#include "SshTerminal.h"

using sshfont::Font;
using sshfont::Grid;

namespace {

constexpr uint8_t HEIGHTS[] = {12, 14, 16, 18};

// The cell as text: one string per pixel row, '#' for ink.
std::vector<std::string> picture(const Font& font, const uint32_t cp, const bool bold = false) {
  std::vector<std::string> rows(font.cellH, std::string(font.cellW, '.'));
  sshfont::drawCell(font, cp, bold, [&](const int dx, const int dy) {
    ASSERT_GE(dx, 0);
    ASSERT_LT(dx, font.cellW);
    ASSERT_GE(dy, 0);
    ASSERT_LT(dy, font.cellH);
    rows[dy][dx] = '#';
  });
  return rows;
}

int inkIn(const std::vector<std::string>& rows) {
  int ink = 0;
  for (const std::string& row : rows) {
    for (const char c : row) ink += c == '#';
  }
  return ink;
}

bool columnFull(const std::vector<std::string>& rows, const int col) {
  for (const std::string& row : rows) {
    if (row[col] != '#') return false;
  }
  return true;
}

bool anyRowFull(const std::vector<std::string>& rows) {
  for (const std::string& row : rows) {
    if (row.find('.') == std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST(SshTermFont, SizesAndCells) {
  EXPECT_EQ(sshfont::fontForHeight(12).cellW, 6);
  EXPECT_EQ(sshfont::fontForHeight(14).cellW, 8);
  EXPECT_EQ(sshfont::fontForHeight(16).cellW, 8);
  EXPECT_EQ(sshfont::fontForHeight(18).cellW, 10);
  for (const uint8_t height : HEIGHTS) EXPECT_EQ(sshfont::fontForHeight(height).cellH, height);
  // A value outside 12 to 18 is the default size; an odd one between is the next size up.
  for (const uint8_t bad : {0, 1, 11, 19, 20, 255}) {
    EXPECT_EQ(sshfont::snapHeight(bad), sshfont::DEFAULT_HEIGHT) << int(bad);
    EXPECT_EQ(sshfont::fontForHeight(bad).cellH, sshfont::DEFAULT_HEIGHT) << int(bad);
  }
  EXPECT_EQ(sshfont::snapHeight(13), 14);
  EXPECT_EQ(sshfont::snapHeight(15), 16);
  EXPECT_EQ(sshfont::snapHeight(17), 18);
  EXPECT_EQ(sshfont::fontForHeight(13).cellH, 14);
  for (const uint8_t height : HEIGHTS) EXPECT_EQ(sshfont::snapHeight(height), height);
  // Whatever is stored, the snapped value is one of the four and a tap always changes the size
  // that is shown (a stored 13 shows as 14 and goes to 16).
  for (int value = 0; value <= 255; ++value) {
    const uint8_t stored = static_cast<uint8_t>(value);
    const uint8_t shown = sshfont::snapHeight(stored);
    EXPECT_TRUE(shown == 12 || shown == 14 || shown == 16 || shown == 18) << value;
    const uint8_t next = sshfont::nextHeight(stored);
    EXPECT_TRUE(next == 12 || next == 14 || next == 16 || next == 18) << value;
    EXPECT_NE(next, shown) << value;
    EXPECT_NE(sshfont::fontForHeight(next).cellH, sshfont::fontForHeight(stored).cellH) << value;
  }
  EXPECT_EQ(sshfont::nextHeight(13), 16);
  EXPECT_EQ(sshfont::nextHeight(12), 14);
  EXPECT_EQ(sshfont::nextHeight(14), 16);
  EXPECT_EQ(sshfont::nextHeight(16), 18);
  EXPECT_EQ(sshfont::nextHeight(18), 12);
}

TEST(SshTermFont, CodePointTableIsSortedAndUnique) {
  ASSERT_GT(sshfont::data::GLYPH_COUNT, 900);
  for (int i = 1; i < sshfont::data::GLYPH_COUNT; ++i) {
    ASSERT_LT(sshfont::data::CODEPOINTS[i - 1], sshfont::data::CODEPOINTS[i]) << i;
  }
}

TEST(SshTermFont, HasAsciiLatinGreekCyrillicBoxAndBlocks) {
  for (uint32_t cp = 0x20; cp <= 0x7E; ++cp) EXPECT_GE(sshfont::glyphIndex(cp), 0) << cp;
  for (uint32_t cp = 0xA0; cp <= 0xFF; ++cp) EXPECT_GE(sshfont::glyphIndex(cp), 0) << cp;
  // Greek with tonos and dialytika: ά ΐ ώ Ω, and the plain alphabet.
  for (const uint32_t cp : {0x3AC, 0x390, 0x3CE, 0x3A9, 0x3B1, 0x3C9, 0x391})
    EXPECT_GE(sshfont::glyphIndex(cp), 0) << cp;
  for (uint32_t cp = 0x410; cp <= 0x44F; ++cp) EXPECT_GE(sshfont::glyphIndex(cp), 0) << cp;
  // Every box-drawing character and block element is the font's own glyph or a listed stand-in:
  // none ends as '+', '#' or '?'.
  for (uint32_t cp = 0x2500; cp <= 0x259F; ++cp) {
    if (sshfont::glyphIndex(cp) >= 0) continue;
    const uint32_t stand = sshfont::fallbackFor(cp);
    EXPECT_GE(stand, 0x2500u) << std::hex << cp;
    EXPECT_GE(sshfont::glyphIndex(stand), 0) << std::hex << cp;
  }
  EXPECT_EQ(sshfont::glyphIndex(0x1F600), -1);
  EXPECT_EQ(sshfont::glyphIndex(0x4E2D), -1);
}

TEST(SshTermFont, Fallbacks) {
  EXPECT_EQ(sshfont::fallbackFor(0x2504), 0x2500u);  // dashed horizontal -> plain
  EXPECT_EQ(sshfont::fallbackFor(0x2506), 0x2502u);  // dashed vertical -> plain
  EXPECT_EQ(sshfont::fallbackFor(0x254D), 0x2501u);
  EXPECT_EQ(sshfont::fallbackFor(0x2594), 0x2580u);
  EXPECT_EQ(sshfont::fallbackFor(0x276F), uint32_t{'>'});
  EXPECT_EQ(sshfont::fallbackFor(0x279C), 0x2192u);
  EXPECT_EQ(sshfont::fallbackFor(0x4E2D), uint32_t{'?'});   // CJK
  EXPECT_EQ(sshfont::fallbackFor(0x1F600), uint32_t{'?'});  // outside the BMP
  EXPECT_EQ(sshfont::fallbackFor(0x07), uint32_t{' '});     // a control character
  EXPECT_EQ(sshfont::fallbackFor(0x2800), uint32_t{'?'});   // Braille is not in the subset
  // A stand-in is always in the font, so no chain of fallbacks is needed.
  for (uint32_t cp = 0; cp <= 0xFFFF; ++cp) {
    if (sshfont::glyphIndex(cp) >= 0) continue;
    ASSERT_GE(sshfont::glyphIndex(sshfont::fallbackFor(cp)), 0) << std::hex << cp;
  }
  EXPECT_EQ(sshfont::resolveGlyph(0x4E2D), sshfont::glyphIndex('?'));
  EXPECT_EQ(sshfont::resolveGlyph(0x2504), sshfont::glyphIndex(0x2500));
  EXPECT_EQ(sshfont::resolveGlyph('A'), sshfont::glyphIndex('A'));
}

TEST(SshTermFont, GlyphsAreCopiedPixelForPixel) {
  // 'm' of the 8 x 16 size, as Terminus draws it.
  const std::vector<std::string> expected = {
      "........", "........", "........", "........", "........", "######..", "#..#..#.", "#..#..#.",
      "#..#..#.", "#..#..#.", "#..#..#.", "#..#..#.", "........", "........", "........", "........",
  };
  EXPECT_EQ(picture(sshfont::fontForHeight(16), 'm'), expected);
}

TEST(SshTermFont, LowerCaseLettersShareOneHeightAndBaseline) {
  // What went wrong with the scaled UI font: letters of one line had different heights. Here the
  // x-height letters start and end on the same pixel rows, in every size.
  for (const uint8_t height : HEIGHTS) {
    const Font& font = sshfont::fontForHeight(height);
    int top = -1;
    int bottom = -1;
    for (const char c : std::string("acemnorsuvwxz")) {
      const auto rows = picture(font, static_cast<uint32_t>(c));
      int first = -1;
      int last = -1;
      for (int r = 0; r < font.cellH; ++r) {
        if (rows[r].find('#') == std::string::npos) continue;
        if (first < 0) first = r;
        last = r;
      }
      if (top < 0) {
        top = first;
        bottom = last;
      }
      EXPECT_EQ(first, top) << c << " at " << int(height);
      EXPECT_EQ(last, bottom) << c << " at " << int(height);
    }
  }
}

TEST(SshTermFont, EveryGlyphStaysInsideItsCell) {
  for (const uint8_t height : HEIGHTS) {
    const Font& font = sshfont::fontForHeight(height);
    for (int i = 0; i < sshfont::data::GLYPH_COUNT; ++i) {
      const uint32_t cp = sshfont::data::CODEPOINTS[i];
      picture(font, cp, false);  // asserts on a pixel outside the cell
      picture(font, cp, true);
    }
  }
}

TEST(SshTermFont, BoxLinesReachTheCellEdges) {
  for (const uint8_t height : HEIGHTS) {
    const Font& font = sshfont::fontForHeight(height);
    // Horizontal lines fill a pixel row from edge to edge, so two cells side by side join.
    for (const uint32_t cp : {0x2500, 0x2501, 0x2550, 0x2504, 0x254C, 0x252C, 0x2534, 0x253C}) {
      EXPECT_TRUE(anyRowFull(picture(font, cp))) << std::hex << cp << " at " << std::dec << int(height);
    }
    // Vertical lines fill a pixel column from top to bottom, so two rows join.
    for (const uint32_t cp : {0x2502, 0x2503, 0x2506, 0x251C, 0x2524, 0x253C}) {
      const auto rows = picture(font, cp);
      bool full = false;
      for (int col = 0; col < font.cellW; ++col) full = full || columnFull(rows, col);
      EXPECT_TRUE(full) << std::hex << cp << " at " << std::dec << int(height);
    }
    // A corner reaches the right edge and the bottom edge: U+250C.
    const auto corner = picture(font, 0x250C);
    EXPECT_NE(corner[font.cellH - 1].find('#'), std::string::npos);
    bool right = false;
    for (const std::string& row : corner) right = right || row[font.cellW - 1] == '#';
    EXPECT_TRUE(right);
    // The rounded corner is in the font too (drawn, not a '+').
    EXPECT_GE(sshfont::glyphIndex(0x256D), 0);
  }
}

TEST(SshTermFont, BlockElements) {
  for (const uint8_t height : HEIGHTS) {
    const Font& font = sshfont::fontForHeight(height);
    const int cell = font.cellW * font.cellH;
    EXPECT_EQ(inkIn(picture(font, 0x2588)), cell);      // full block
    EXPECT_EQ(inkIn(picture(font, 0x2580)), cell / 2);  // upper half
    EXPECT_EQ(inkIn(picture(font, 0x2584)), cell / 2);  // lower half
    EXPECT_EQ(inkIn(picture(font, 0x258C)), cell / 2);  // left half
    EXPECT_EQ(inkIn(picture(font, 0x2590)), cell / 2);  // right half
    EXPECT_EQ(inkIn(picture(font, ' ')), 0);
    // The shades are patterns: some ink, not all.
    for (const uint32_t cp : {0x2591, 0x2592, 0x2593}) {
      const int ink = inkIn(picture(font, cp));
      EXPECT_GT(ink, 0);
      EXPECT_LT(ink, cell);
    }
  }
}

TEST(SshTermFont, Bold) {
  const Font& font = sshfont::fontForHeight(16);
  // Heavier than regular, and the regular pixels are all still there.
  const auto regular = picture(font, 'l');
  const auto bold = picture(font, 'l', true);
  EXPECT_GT(inkIn(bold), inkIn(regular));
  for (int r = 0; r < font.cellH; ++r) {
    for (int c = 0; c < font.cellW; ++c) {
      if (regular[r][c] == '#') {
        EXPECT_EQ(bold[r][c], '#');
      }
    }
  }
  // 'm' has ink in the first column: its bold form leaves the last column clear.
  const auto m = picture(font, 'm', true);
  for (const std::string& row : m) EXPECT_EQ(row[font.cellW - 1], '.');
  EXPECT_GT(inkIn(m), inkIn(picture(font, 'm')));
  // Box lines are never drawn bold, and the 6-pixel cells have no bold at all.
  EXPECT_EQ(picture(font, 0x2502, true), picture(font, 0x2502));
  const Font& small = sshfont::fontForHeight(12);
  EXPECT_EQ(picture(small, 'l', true), picture(small, 'l'));

  EXPECT_EQ(sshfont::boldRowBits(font, 0x4000, false), 0x6000);
  EXPECT_EQ(sshfont::boldRowBits(font, 0x0100, false), 0x0100);  // column 7 of 8: nothing past the cell
  EXPECT_EQ(sshfont::boldRowBits(font, 0x0200, false), 0x0300);
  EXPECT_EQ(sshfont::boldRowBits(font, 0x0200, true), 0x0200);  // the last column kept clear
}

TEST(SshTermFont, GridOnTheScreens) {
  // The 480-pixel side: 80, 60, 60 and 48 columns, no pixel left over or cut.
  struct Case {
    uint8_t height;
    int width;
    uint16_t cols;
    int left;
  };
  const Case cases[] = {
      {12, 480, 80, 0},
      {14, 480, 60, 0},
      {16, 480, 60, 0},
      {18, 480, 48, 0},
      // The 800-pixel side: 6-pixel cells would give 133 columns, held at the terminal's 132.
      {12, 800, 132, 4},
      {16, 800, 100, 0},
      {18, 800, 80, 0},
      // Widths that do not divide: whole cells, the rest split left and right.
      {16, 485, 60, 2},
      {18, 479, 47, 4},
      {12, 479, 79, 2},
  };
  for (const Case& c : cases) {
    const Font& font = sshfont::fontForHeight(c.height);
    const Grid grid = sshfont::gridFor(c.width, 600, font, sshterm::Terminal::MAX_COLS, sshterm::Terminal::MAX_ROWS);
    EXPECT_EQ(grid.cols, c.cols) << int(c.height) << " " << c.width;
    EXPECT_EQ(grid.left, c.left) << int(c.height) << " " << c.width;
    // The last column ends on the screen.
    if (c.width >= 20 * font.cellW) {
      EXPECT_LE(grid.left + grid.cols * font.cellW, c.width);
    }
  }
}

TEST(SshTermFont, GridRowsAndLimits) {
  const Font& font = sshfont::fontForHeight(16);
  EXPECT_EQ(sshfont::gridFor(480, 660, font, 132, 64).rows, 41);
  EXPECT_EQ(sshfont::gridFor(480, 655, font, 132, 64).rows, 40);  // a part row is not a row
  // The terminal's dirty-row mask has 64 bits: never more rows than that.
  EXPECT_EQ(sshfont::gridFor(480, 2000, sshfont::fontForHeight(12), 132, 64).rows, 64);
  // Too small an area still gives a grid the terminal accepts.
  const Grid tiny = sshfont::gridFor(40, 10, font, 132, 64);
  EXPECT_EQ(tiny.cols, 20);
  EXPECT_EQ(tiny.rows, 4);
  EXPECT_EQ(tiny.left, 0);
  // Every grid the screens can ask for is one Terminal::begin takes.
  for (const uint8_t height : HEIGHTS) {
    for (const int width : {480, 528, 792, 800}) {
      const Grid grid = sshfont::gridFor(width, 700, sshfont::fontForHeight(height), sshterm::Terminal::MAX_COLS,
                                         sshterm::Terminal::MAX_ROWS);
      sshterm::Terminal term;
      EXPECT_TRUE(term.begin(grid.cols, grid.rows, 500, sshterm::Memory{malloc, free}));
    }
  }
}

TEST(SshTermFont, LongLineWrapsByColumns) {
  // A URL longer than the line: it continues on the next row at the grid's column count, no
  // character is lost and none is placed past the last column.
  const Font& font = sshfont::fontForHeight(16);
  const Grid grid = sshfont::gridFor(480, 640, font, sshterm::Terminal::MAX_COLS, sshterm::Terminal::MAX_ROWS);
  ASSERT_EQ(grid.cols, 60);
  sshterm::Terminal term;
  ASSERT_TRUE(term.begin(grid.cols, grid.rows, 100, sshterm::Memory{malloc, free}));
  const std::string url = " GitHub : https://github.com/MichaIng/DietPi#the-dietpi-project-team-and-more";
  ASSERT_GT(url.size(), 60u);
  term.feed(url);
  EXPECT_EQ(term.rowText(0), url.substr(0, 60));
  EXPECT_EQ(term.rowText(1), url.substr(60));
  // A box line of exactly the grid's width fills one row and does not spill.
  term.feed("\r\n");
  std::string line;
  for (int i = 0; i < grid.cols; ++i) line += "\xE2\x94\x80";
  term.feed(line + "\r\n");
  EXPECT_EQ(term.rowText(2), line);
  EXPECT_EQ(term.rowText(3), "");
  const sshterm::Cell* cells = term.line(2);
  ASSERT_NE(cells, nullptr);
  EXPECT_EQ(cells[grid.cols - 1].ch, 0x2500);
}

TEST(SshTermFont, ZeroWidthCharactersTakeNoCell) {
  using sshterm::Terminal;
  for (const uint32_t cp :
       {0x0300, 0x0301, 0x036F, 0x1AB0, 0x1AFF, 0x1DC0, 0x1DFF, 0x200B, 0x200C, 0x200D, 0x200E, 0x200F, 0x2028,
        0x2029, 0x202A, 0x202E, 0x2060, 0x2064, 0x20D0, 0x20FF, 0xFE00, 0xFE0F, 0xFE20, 0xFE2F, 0xFEFF}) {
    EXPECT_TRUE(Terminal::isZeroWidth(cp)) << std::hex << cp;
  }
  for (const uint32_t cp : {0x20,   0x41,   0x02FF, 0x0370, 0x03AC, 0x1AAF, 0x1B00, 0x200A, 0x2010, 0x2027, 0x202F,
                            0x205F, 0x2065, 0x20CF, 0x2100, 0x2500, 0xFDFF, 0xFE10, 0xFE1F, 0xFE30, 0xFFFD, 0x1F600}) {
    EXPECT_FALSE(Terminal::isZeroWidth(cp)) << std::hex << cp;
  }

  Terminal term;
  ASSERT_TRUE(term.begin(20, 4, 10, sshterm::Memory{malloc, free}));
  // a + combining acute, a zero-width space, a left-to-right mark, a right-to-left override, a
  // word joiner, a variation selector, a combining half mark and a byte order mark, between the
  // letters of "abcdefgh": only the letters take cells.
  term.feed(
      "a\xCC\x81"
      "b\xE2\x80\x8B"
      "c\xE2\x80\x8E"
      "d\xE2\x80\xAE"
      "e\xE2\x81\xA0"
      "f\xEF\xB8\x8F"
      "g\xEF\xB8\xA0"
      "h\xEF\xBB\xBF");
  EXPECT_EQ(term.rowText(0), "abcdefgh");
  EXPECT_EQ(term.cursorCol(), 8);
  // A line of exactly the width followed by zero-width characters does not wrap or spill.
  term.feed("\r\n" + std::string(20, 'x') + "\xE2\x80\x8B\xCC\x81");
  EXPECT_EQ(term.rowText(1), std::string(20, 'x'));
  EXPECT_EQ(term.rowText(2), "");
  // A precomposed letter keeps its cell, and a character outside the BMP is one '?'.
  term.feed("\r\n\xCE\xAC\xF0\x9F\x98\x80.");
  EXPECT_EQ(term.rowText(2), "\xCE\xAC?.");
}
