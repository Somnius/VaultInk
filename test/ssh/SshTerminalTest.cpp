#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "SshTerminal.h"

using sshterm::RedrawPolicy;
using sshterm::Terminal;

namespace {

size_t liveBlocks = 0;
void* testAlloc(const size_t bytes) {
  ++liveBlocks;
  return malloc(bytes);
}
void testRelease(void* block) {
  --liveBlocks;
  free(block);
}
const sshterm::Memory kMemory{testAlloc, testRelease};

struct Screen {
  Terminal term;
  explicit Screen(const uint16_t cols = 20, const uint16_t rows = 5, const uint16_t scrollback = 10) {
    EXPECT_TRUE(term.begin(cols, rows, scrollback, kMemory));
  }
  std::vector<std::string> rows() const {
    std::vector<std::string> out;
    for (uint16_t r = 0; r < term.rows(); ++r) out.push_back(term.rowText(r));
    return out;
  }
};

struct Case {
  const char* name;
  std::string input;
  std::vector<std::string> rows;  // rows not listed must be empty
  int cursorRow;
  int cursorCol;
};

}  // namespace

// A table of byte sequences and the screen they must leave (20 columns, 5 rows).
TEST(SshTerminal, SequenceTable) {
  const std::vector<Case> cases = {
      {"plain text", "hello", {"hello"}, 0, 5},
      {"CR LF", "one\r\ntwo", {"one", "two"}, 1, 3},
      {"LF alone keeps the column", "ab\ncd", {"ab", "  cd"}, 1, 4},
      {"CR returns to column 0", "abcdef\rXY", {"XYcdef"}, 0, 2},
      {"backspace moves left", "abc\b\bX", {"aXc"}, 0, 2},
      {"backspace stops at column 0", "a\b\b\bZ", {"Z"}, 0, 1},
      {"tab to the next multiple of 8", "a\tb\tc", {"a       b       c"}, 0, 17},
      {"tab stops at the last column", "\t\t\t\tX", {"                   X"}, 0, 19},
      {"bell and NUL show nothing", std::string("a\a") + std::string(1, '\0') + "b", {"ab"}, 0, 2},
      {"wrap at the right edge", "12345678901234567890AB", {"12345678901234567890", "AB"}, 1, 2},
      {"no wrap until the next character", "12345678901234567890\r\nX", {"12345678901234567890", "X"}, 1, 1},
      {"cursor up and down", "a\r\nb\r\nc\x1b[2AX\x1b[1BY", {"aX", "b Y", "c"}, 1, 3},
      {"cursor right and left", "abc\x1b[5CX\x1b[3DY", {"abc   Y X"}, 0, 7},
      {"cursor position", "\x1b[3;4HX\x1b[1;1HY", {"Y", "", "   X"}, 0, 1},
      {"cursor position without numbers is home", "abc\x1b[HX", {"Xbc"}, 0, 1},
      {"column absolute", "abcdef\x1b[3GX", {"abXdef"}, 0, 3},
      {"row absolute", "a\x1b[4dX", {"a", "", "", " X"}, 3, 2},
      {"cursor moves are clamped", "\x1b[99;99HX\x1b[99A\x1b[99DY", {"Y", "", "", "", "                   X"}, 0, 1},
      {"erase to end of line", "abcdef\x1b[3D\x1b[K", {"abc"}, 0, 3},
      {"erase to start of line", "abcdef\x1b[3D\x1b[1K", {"    ef"}, 0, 3},
      {"erase whole line", "abcdef\x1b[2K", {""}, 0, 6},
      {"erase below", "aaa\r\nbbb\r\nccc\x1b[2;2H\x1b[J", {"aaa", "b"}, 1, 1},
      {"erase above", "aaa\r\nbbb\r\nccc\x1b[2;2H\x1b[1J", {"", "  b", "ccc"}, 1, 1},
      {"erase display keeps the cursor", "aaa\r\nbbb\x1b[2JX", {"", "   X"}, 1, 4},
      {"clear as the shell sends it", "junk\r\nmore\x1b[H\x1b[2Jok", {"ok"}, 0, 2},
      {"delete characters", "abcdef\x1b[1G\x1b[2P", {"cdef"}, 0, 0},
      {"insert blanks", "abcdef\x1b[1G\x1b[2@", {"  abcdef"}, 0, 0},
      {"erase characters", "abcdef\x1b[1G\x1b[3X", {"   def"}, 0, 0},
      {"insert line", "a\r\nb\r\nc\x1b[2;1H\x1b[L", {"a", "", "b", "c"}, 1, 0},
      {"delete line", "a\r\nb\r\nc\x1b[2;1H\x1b[M", {"a", "c"}, 1, 0},
      {"colours and bold are swallowed", "\x1b[1;31mred\x1b[0m \x1b[38;5;200mx\x1b[m", {"red x"}, 0, 5},
      {"window title is swallowed (BEL)", "\x1b]0;user@host: ~\aprompt$", {"prompt$"}, 0, 7},
      {"window title is swallowed (ST)", "\x1b]2;title\x1b\\ok", {"ok"}, 0, 2},
      {"bracketed paste mode is swallowed", "\x1b[?2004hls\x1b[?2004l", {"ls"}, 0, 2},
      {"charset selection is swallowed", "\x1b(Babc\x1b)0", {"abc"}, 0, 3},
      {"keypad modes are swallowed", "\x1b=a\x1b>b", {"ab"}, 0, 2},
      {"unknown CSI is swallowed", "a\x1b[5 qb\x1b[>4;2mc", {"abc"}, 0, 3},
      {"save and restore cursor",
       "ab\x1b"
       "7\r\ncd\x1b"
       "8X",
       {"abX", "cd"},
       0,
       3},
      {"save and restore cursor (CSI)", "ab\x1b[s\r\ncd\x1b[uX", {"abX", "cd"}, 0, 3},
      {"reverse index at the top scrolls down", "a\r\nb\x1b[H\x1bMX", {"X", "a", "b"}, 0, 1},
      {"next line",
       "ab\x1b"
       "Ecd",
       {"ab", "cd"},
       1,
       2},
      {"UTF-8 Greek", "\xCE\xBA\xCE\xB1\xCE\xBB\xCE\xB7", {"\xCE\xBA\xCE\xB1\xCE\xBB\xCE\xB7"}, 0, 4},
      {"UTF-8 three bytes",
       "\xE2\x82\xAC"
       "5",
       {"\xE2\x82\xAC"
        "5"},
       0,
       2},
      {"outside the BMP becomes ?", "\xF0\x9F\x98\x80!", {"?!"}, 0, 2},
      {"a broken UTF-8 sequence does not eat the next character",
       "\xCE"
       "A",
       {"A"},
       0,
       1},
      {"a stray continuation byte becomes ?", "\x80z", {"?z"}, 0, 2},
      {"a control character inside a sequence acts",
       "ab\x1b[\r"
       "2CX",
       {"abX"},
       0,
       3},
      {"scroll region", "1\r\n2\r\n3\r\n4\r\n5\x1b[2;4r\x1b[4;1H\nX", {"1", "3", "4", "X", "5"}, 3, 1},
      {"full reset",
       "abc\r\ndef\x1b"
       "cX",
       {"X"},
       0,
       1},
  };
  for (const Case& c : cases) {
    Screen screen;
    screen.term.feed(c.input);
    const std::vector<std::string> rows = screen.rows();
    for (size_t r = 0; r < rows.size(); ++r) {
      const std::string expected = r < c.rows.size() ? c.rows[r] : "";
      EXPECT_EQ(rows[r], expected) << c.name << ", row " << r;
    }
    EXPECT_EQ(screen.term.cursorRow(), c.cursorRow) << c.name;
    EXPECT_EQ(screen.term.cursorCol(), c.cursorCol) << c.name;
  }
}

TEST(SshTerminal, SequencesSplitAcrossFeedsGiveTheSameScreen) {
  const std::string input = "ab\x1b[3;4HX\x1b]0;title\a\xCE\xBA\x1b[1;31mred\x1b[0m\r\nend";
  Screen whole;
  whole.term.feed(input);
  Screen pieces;
  for (const char c : input) pieces.term.feed(reinterpret_cast<const uint8_t*>(&c), 1);
  EXPECT_EQ(whole.rows(), pieces.rows());
  EXPECT_EQ(whole.term.cursorRow(), pieces.term.cursorRow());
  EXPECT_EQ(whole.term.cursorCol(), pieces.term.cursorCol());
}

TEST(SshTerminal, ScrollingFillsTheScrollback) {
  Screen screen(20, 3, 4);
  for (int i = 1; i <= 6; ++i) screen.term.feed("line" + std::to_string(i) + (i < 6 ? "\r\n" : ""));
  EXPECT_EQ(screen.rows(), (std::vector<std::string>{"line4", "line5", "line6"}));
  EXPECT_EQ(screen.term.scrollbackLines(), 3);
  EXPECT_EQ(screen.term.rowText(0, 1), "line3");
  EXPECT_EQ(screen.term.rowText(0, 3), "line1");
  EXPECT_EQ(screen.term.rowText(2, 3), "line3");
  EXPECT_EQ(screen.term.line(0, 4), nullptr);  // further back than there is
}

TEST(SshTerminal, ScrollbackIsBoundedAndTheOldestLineGoes) {
  Screen screen(20, 3, 4);
  for (int i = 1; i <= 30; ++i) screen.term.feed("line" + std::to_string(i) + "\r\n");
  EXPECT_EQ(screen.term.scrollbackLines(), 4);
  EXPECT_EQ(screen.term.rowText(0, 4), "line25");
  EXPECT_EQ(screen.term.rowText(1, 0), "line30");
  EXPECT_EQ(screen.term.rowText(2, 0), "");
}

TEST(SshTerminal, AScrollRegionDoesNotFeedTheScrollback) {
  Screen screen(20, 4, 4);
  screen.term.feed(
      "\x1b[1;2r"
      "a\r\nb\r\nc\r\nd");
  EXPECT_EQ(screen.term.scrollbackLines(), 0);
  EXPECT_EQ(screen.rows(), (std::vector<std::string>{"c", "d", "", ""}));
}

TEST(SshTerminal, AttributesAreKeptPerCell) {
  Screen screen;
  screen.term.feed("a\x1b[1mb\x1b[7mc\x1b[22md\x1b[0me\x1b[4mf");
  const sshterm::Cell* row = screen.term.line(0);
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(row[0].attr, 0);
  EXPECT_EQ(row[1].attr, sshterm::ATTR_BOLD);
  EXPECT_EQ(row[2].attr, sshterm::ATTR_BOLD | sshterm::ATTR_INVERSE);
  EXPECT_EQ(row[3].attr, sshterm::ATTR_INVERSE);
  EXPECT_EQ(row[4].attr, 0);
  EXPECT_EQ(row[5].attr, sshterm::ATTR_UNDERLINE);
}

TEST(SshTerminal, AnswersCursorAndDeviceQueries) {
  Screen screen;
  screen.term.feed("abc\r\nde\x1b[6n");
  EXPECT_EQ(screen.term.takeReply(), "\x1b[2;3R");
  EXPECT_EQ(screen.term.takeReply(), "");
  screen.term.feed("\x1b[5n\x1b[c");
  EXPECT_EQ(screen.term.takeReply(), "\x1b[0n\x1b[?6c");
}

TEST(SshTerminal, ChangesAreReportedOnce) {
  Screen screen(20, 4, 4);
  EXPECT_FALSE(screen.term.hasChanges());
  screen.term.feed("a\r\nb");
  Terminal::Changes changes = screen.term.takeChanges();
  EXPECT_EQ(changes.dirtyRows, 0b0011u);
  EXPECT_EQ(changes.scrolledLines, 0u);
  EXPECT_FALSE(changes.cleared);
  EXPECT_FALSE(screen.term.hasChanges());

  screen.term.feed("\r\nc\r\nd\r\ne\r\nf");
  changes = screen.term.takeChanges();
  EXPECT_EQ(changes.scrolledLines, 2u);
  EXPECT_EQ(changes.dirtyRows, 0b1111u);

  screen.term.feed("\x1b[2J");
  EXPECT_TRUE(screen.term.takeChanges().cleared);
  screen.term.feed("\x1b[1m\x1b[0m");  // attributes alone change nothing on the screen
  EXPECT_FALSE(screen.term.hasChanges());
}

TEST(SshTerminal, ModesAreTracked) {
  Screen screen;
  EXPECT_TRUE(screen.term.cursorVisible());
  EXPECT_FALSE(screen.term.applicationCursorKeys());
  screen.term.feed("\x1b[?25l\x1b[?1h");
  EXPECT_FALSE(screen.term.cursorVisible());
  EXPECT_TRUE(screen.term.applicationCursorKeys());
  screen.term.feed("\x1b[?25h\x1b[?1l");
  EXPECT_TRUE(screen.term.cursorVisible());
  EXPECT_FALSE(screen.term.applicationCursorKeys());
  // Autowrap off: the last column is overwritten.
  screen.term.feed("\x1b[?7l12345678901234567890XYZ");
  EXPECT_EQ(screen.term.rowText(0), "1234567890123456789Z");
  EXPECT_EQ(screen.term.rowText(1), "");
}

TEST(SshTerminal, AlternateScreenRequestIsCountedAndErasesTheScreen) {
  Screen screen;
  screen.term.feed("shell line\r\n");
  EXPECT_EQ(screen.term.alternateScreenRequests(), 0u);
  screen.term.feed("\x1b[?1049h\x1b[2;1Hvim");
  EXPECT_EQ(screen.term.alternateScreenRequests(), 1u);
  EXPECT_EQ(screen.rows(), (std::vector<std::string>{"", "vim", "", "", ""}));
  screen.term.feed("\x1b[?1049l");
  EXPECT_EQ(screen.term.alternateScreenRequests(), 1u);
  EXPECT_EQ(screen.term.rowText(1), "");
}

TEST(SshTerminal, RefusesBadSizesAndFreesItsBlock) {
  const size_t before = liveBlocks;
  {
    Terminal term;
    EXPECT_FALSE(term.ready());
    EXPECT_FALSE(term.begin(1, 5, 0, kMemory));
    EXPECT_FALSE(term.begin(80, 1, 0, kMemory));
    EXPECT_FALSE(term.begin(133, 24, 0, kMemory));
    EXPECT_FALSE(term.begin(80, 65, 0, kMemory));
    EXPECT_FALSE(term.begin(80, 24, 2001, kMemory));
    EXPECT_FALSE(term.begin(80, 24, 100, sshterm::Memory{}));
    EXPECT_FALSE(term.begin(80, 24, 100, sshterm::Memory{[](size_t) -> void* { return nullptr; }, testRelease}));
    term.feed("ignored without a screen");
    EXPECT_EQ(term.rowText(0), "");
    EXPECT_TRUE(term.begin(80, 24, 500, kMemory));
    EXPECT_EQ(liveBlocks, before + 1);
    EXPECT_TRUE(term.begin(40, 10, 0, kMemory));  // again: the first block is given back
    EXPECT_EQ(liveBlocks, before + 1);
  }
  EXPECT_EQ(liveBlocks, before);
  EXPECT_EQ(Terminal::bytesFor(80, 24, 500), 80u * 524u * 4u);
}

TEST(SshTerminal, RandomBytesNeverLeaveTheGrid) {
  Screen screen(80, 24, 50);
  uint32_t seed = 12345;
  std::string noise;
  for (int i = 0; i < 200000; ++i) {
    seed = seed * 1664525u + 1013904223u;
    // Weighted towards escape sequences and digits so the parser's states are all visited.
    static const char kLikely[] = "\x1b[];?0123456789HJKmrABCDLMP@Xhlsu\r\n\b\t";
    const uint8_t pick = static_cast<uint8_t>(seed >> 24);
    noise += pick < 160 ? kLikely[pick % (sizeof(kLikely) - 1)] : static_cast<char>(seed >> 16);
  }
  screen.term.feed(noise);
  EXPECT_LT(screen.term.cursorRow(), 24);
  EXPECT_LT(screen.term.cursorCol(), 80);
  EXPECT_LE(screen.term.scrollbackLines(), 50);
}

TEST(SshRedrawPolicy, BatchesOutputAndLimitsTheRate) {
  RedrawPolicy policy(500, 100);
  EXPECT_EQ(policy.poll(0), RedrawPolicy::Action::None);
  Terminal::Changes typed;
  typed.dirtyRows = 1;
  policy.note(typed);
  EXPECT_EQ(policy.poll(10), RedrawPolicy::Action::Fast);  // the first change is drawn at once
  policy.note(typed);
  policy.note(typed);
  EXPECT_EQ(policy.poll(200), RedrawPolicy::Action::None);  // too soon: batched
  EXPECT_EQ(policy.poll(509), RedrawPolicy::Action::None);
  EXPECT_EQ(policy.poll(510), RedrawPolicy::Action::Fast);
  EXPECT_EQ(policy.poll(2000), RedrawPolicy::Action::None);  // nothing new
}

TEST(SshRedrawPolicy, FullRefreshAfterAClearOrManyLines) {
  RedrawPolicy policy(500, 100);
  Terminal::Changes cleared;
  cleared.cleared = true;
  policy.note(cleared);
  EXPECT_EQ(policy.poll(0), RedrawPolicy::Action::Full);

  Terminal::Changes scrolled;
  scrolled.scrolledLines = 60;
  policy.note(scrolled);
  EXPECT_EQ(policy.poll(1000), RedrawPolicy::Action::Fast);
  policy.note(scrolled);  // 120 lines since the last full refresh
  EXPECT_EQ(policy.poll(2000), RedrawPolicy::Action::Full);
  policy.note(scrolled);  // the count started again
  EXPECT_EQ(policy.poll(3000), RedrawPolicy::Action::Fast);
}

TEST(SshRedrawPolicy, WorksAcrossTheMillisecondWrap) {
  RedrawPolicy policy(500, 100);
  policy.noteOther();
  EXPECT_EQ(policy.poll(0xFFFFFF00u), RedrawPolicy::Action::Fast);
  policy.noteOther();
  EXPECT_EQ(policy.poll(0xFFFFFFF0u), RedrawPolicy::Action::None);
  EXPECT_EQ(policy.poll(0x00000100u), RedrawPolicy::Action::Fast);  // 528 ms later, past the wrap
}
