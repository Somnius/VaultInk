#include <gtest/gtest.h>

#include <chrono>
#include <cstring>

#include "Sudoku.h"

using namespace sudoku;

namespace {

Grid gridFrom(const char* s) {
  Grid g{};
  for (int i = 0; i < CELLS; ++i) {
    const char c = s[i];
    g[i] = (c >= '1' && c <= '9') ? static_cast<uint8_t>(c - '0') : 0;
  }
  return g;
}

Board boardFrom(const Grid& g) {
  Board b;
  for (int i = 0; i < CELLS; ++i) b.cells[i].value = g[i];
  return b;
}

bool validSolution(const Grid& g) {
  Board b = boardFrom(g);
  return isSolved(b);
}

int firstEditable(const Game& g) {
  for (int i = 0; i < CELLS; ++i) {
    if (!g.board.cells[i].given) return i;
  }
  return -1;
}

int firstGiven(const Game& g) {
  for (int i = 0; i < CELLS; ++i) {
    if (g.board.cells[i].given) return i;
  }
  return -1;
}

// Arto Inkala's 2012 "world's hardest" puzzle.
const char* HARD = "8..........36......7..9.2...5...7.......457.....1...3...1....68..85...1..9....4..";
// Two 5s in the first row.
const char* INVALID = "55.........36......7..9.2...5...7.......457.....1...3...1....68..85...1..9....4..";
// Easy puzzle (Project Euler #96, grid 01).
const char* EASY = "003020600900305001001806400008102900700000008006708200002609500800203009005010300";
const char* EASY_SOLVED = "483921657967345821251876493548132976729564138136798245372689514814253769695417382";

}  // namespace

TEST(SudokuSolver, SolvesEasyKnownPuzzle) {
  Grid g = gridFrom(EASY);
  ASSERT_TRUE(solve(g));
  EXPECT_EQ(g, gridFrom(EASY_SOLVED));
  EXPECT_EQ(countSolutions(gridFrom(EASY), 2), 1);
}

TEST(SudokuSolver, SolvesHardPuzzleUniquely) {
  const auto t0 = std::chrono::steady_clock::now();
  Grid g = gridFrom(HARD);
  ASSERT_TRUE(solve(g));
  EXPECT_TRUE(validSolution(g));
  for (int i = 0; i < CELLS; ++i) {
    if (gridFrom(HARD)[i]) {
      EXPECT_EQ(g[i], gridFrom(HARD)[i]);
    }
  }
  EXPECT_EQ(countSolutions(gridFrom(HARD), 2), 1);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
  EXPECT_LT(ms.count(), 500);
}

TEST(SudokuSolver, InvalidPuzzleHasNoSolution) {
  Grid g = gridFrom(INVALID);
  EXPECT_EQ(countSolutions(g, 2), 0);
  EXPECT_FALSE(solve(g));
}

TEST(SudokuSolver, EmptyGridHasManySolutions) {
  Grid g{};
  EXPECT_EQ(countSolutions(g, 5), 5);
}

TEST(SudokuGenerate, PuzzlesAreValidAndUnique) {
  for (uint32_t seed = 1; seed <= 6; ++seed) {
    for (Difficulty d : {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard}) {
      Game g;
      generate(g, seed, d);
      const Grid puzzle = g.board.values();
      EXPECT_EQ(countSolutions(puzzle, 2), 1) << "seed " << seed;
      EXPECT_TRUE(validSolution(g.solution));
      for (int i = 0; i < CELLS; ++i) {
        EXPECT_EQ(g.board.cells[i].given, puzzle[i] != 0);
        if (puzzle[i]) {
          EXPECT_EQ(puzzle[i], g.solution[i]);
        }
      }
      Grid solved = puzzle;
      ASSERT_TRUE(solve(solved));
      EXPECT_EQ(solved, g.solution);
    }
  }
}

TEST(SudokuGenerate, SeedIsDeterministic) {
  Game a, b, c;
  generate(a, 12345, Difficulty::Medium);
  generate(b, 12345, Difficulty::Medium);
  generate(c, 12346, Difficulty::Medium);
  EXPECT_EQ(a.board.values(), b.board.values());
  EXPECT_EQ(a.solution, b.solution);
  EXPECT_NE(a.board.values(), c.board.values());
}

TEST(SudokuGenerate, DifficultyGivensRanges) {
  for (uint32_t seed = 100; seed < 120; ++seed) {
    Game e, m, h;
    generate(e, seed, Difficulty::Easy);
    generate(m, seed, Difficulty::Medium);
    generate(h, seed, Difficulty::Hard);
    EXPECT_EQ(countGivens(e.board), 40) << seed;
    EXPECT_EQ(countGivens(m.board), 32) << seed;
    EXPECT_GE(countGivens(h.board), 22) << seed;
    EXPECT_LE(countGivens(h.board), 32) << seed;
  }
}

TEST(SudokuGenerate, FastEnough) {
  const auto t0 = std::chrono::steady_clock::now();
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    Game g;
    generate(g, seed, Difficulty::Hard);
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
  EXPECT_LT(ms.count(), 2000) << "10 hard puzzles took " << ms.count() << " ms";
}

TEST(SudokuBoard, ConflictsAndCompletion) {
  Board b = boardFrom(gridFrom(EASY_SOLVED));
  EXPECT_TRUE(isComplete(b));
  EXPECT_TRUE(isSolved(b));
  auto bad = conflicts(b);
  for (bool x : bad) EXPECT_FALSE(x);

  b.cells[0].value = b.cells[1].value;  // row duplicate
  bad = conflicts(b);
  EXPECT_TRUE(bad[0]);
  EXPECT_TRUE(bad[1]);
  EXPECT_FALSE(bad[2]);
  EXPECT_TRUE(isComplete(b));
  EXPECT_FALSE(isSolved(b));

  b.cells[0].value = 0;
  EXPECT_FALSE(isComplete(b));
  bad = conflicts(b);
  EXPECT_FALSE(bad[0]);
  EXPECT_FALSE(bad[1]);
}

TEST(SudokuBoard, Candidates) {
  Board b = boardFrom(gridFrom(EASY));
  // Cell 0 solves to 4; candidates must include it and exclude row/col/box digits.
  const uint16_t c = candidates(b, 0);
  EXPECT_TRUE(c & (1u << 3));
  EXPECT_FALSE(c & (1u << 2));     // 3 is in row 0
  EXPECT_FALSE(c & (1u << 8));     // 9 is in column 0
  EXPECT_EQ(candidates(b, 2), 0);  // filled cell
}

TEST(SudokuHint, GivesSolutionValueOrNone) {
  Game g;
  generate(g, 7, Difficulty::Easy);
  Hint h = hint(g);
  ASSERT_TRUE(h.found);
  EXPECT_EQ(g.board.cells[h.cell].value, 0);
  EXPECT_EQ(h.value, g.solution[h.cell]);

  // A wrong (but non-conflicting) entry blocks hints: pick an empty cell with two or more
  // candidates and enter one that is not the solution.
  int cell = -1;
  uint8_t wrong = 0;
  for (int i = 0; i < CELLS && wrong == 0; ++i) {
    if (g.board.cells[i].given) continue;
    const uint16_t cand = candidates(g.board, i);
    for (uint8_t d = 1; d <= 9; ++d) {
      if ((cand & (1u << (d - 1))) && d != g.solution[i]) {
        cell = i;
        wrong = d;
      }
    }
  }
  ASSERT_NE(wrong, 0);
  ASSERT_TRUE(setValue(g, cell, wrong));
  EXPECT_FALSE(hint(g).found);
  ASSERT_TRUE(undo(g));
  EXPECT_TRUE(hint(g).found);

  // Fill the whole board from the solution: no hint left.
  for (int i = 0; i < CELLS; ++i) {
    if (!g.board.cells[i].given) setValue(g, i, g.solution[i]);
  }
  EXPECT_TRUE(isSolved(g.board));
  EXPECT_FALSE(hint(g).found);

  Game blank;
  EXPECT_FALSE(hint(blank).found);  // unknown solution
}

TEST(SudokuEdit, GivensAreImmutable) {
  Game g;
  generate(g, 3, Difficulty::Easy);
  const int cell = firstGiven(g);
  const uint8_t before = g.board.cells[cell].value;
  EXPECT_FALSE(setValue(g, cell, before == 9 ? 1 : before + 1));
  EXPECT_FALSE(clearCell(g, cell));
  EXPECT_FALSE(toggleNote(g, cell, 5));
  EXPECT_EQ(g.board.cells[cell].value, before);
  EXPECT_EQ(g.board.cells[cell].notes, 0);
  EXPECT_EQ(g.undo.size(), 0);
  EXPECT_FALSE(setValue(g, firstEditable(g), 0));
  EXPECT_FALSE(setValue(g, 81, 1));
}

TEST(SudokuEdit, UndoRevertsValuesAndNotes) {
  Game g;
  generate(g, 3, Difficulty::Easy);
  const int cell = firstEditable(g);
  EXPECT_FALSE(undo(g));
  ASSERT_TRUE(toggleNote(g, cell, 2));
  ASSERT_TRUE(toggleNote(g, cell, 7));
  EXPECT_EQ(g.board.cells[cell].notes, (1u << 1) | (1u << 6));
  ASSERT_TRUE(setValue(g, cell, 5));
  EXPECT_EQ(g.board.cells[cell].value, 5);
  EXPECT_EQ(g.board.cells[cell].notes, 0);
  ASSERT_TRUE(clearCell(g, cell));
  EXPECT_EQ(g.board.cells[cell].value, 0);

  ASSERT_TRUE(undo(g));
  EXPECT_EQ(g.board.cells[cell].value, 5);
  ASSERT_TRUE(undo(g));
  EXPECT_EQ(g.board.cells[cell].value, 0);
  EXPECT_EQ(g.board.cells[cell].notes, (1u << 1) | (1u << 6));
  ASSERT_TRUE(undo(g));
  EXPECT_EQ(g.board.cells[cell].notes, (1u << 1));
  ASSERT_TRUE(undo(g));
  EXPECT_EQ(g.board.cells[cell].notes, 0);
  EXPECT_FALSE(undo(g));
}

TEST(SudokuEdit, UndoRingKeepsLast64) {
  Game g;
  generate(g, 3, Difficulty::Easy);
  const int cell = firstEditable(g);
  for (int i = 0; i < 100; ++i) toggleNote(g, cell, 1);
  EXPECT_EQ(g.undo.size(), 64);
  int n = 0;
  while (undo(g)) ++n;
  EXPECT_EQ(n, 64);
}

TEST(SudokuSave, RoundTrip) {
  static_assert(SAVE_SIZE <= 200, "save must fit in 200 bytes");
  Game g;
  generate(g, 99, Difficulty::Hard);
  g.elapsedSeconds = 1234;
  const int cell = firstEditable(g);
  setValue(g, cell, 4);
  int other = -1;
  for (int i = cell + 1; i < CELLS; ++i) {
    if (!g.board.cells[i].given) {
      other = i;
      break;
    }
  }
  ASSERT_GE(other, 0);
  toggleNote(g, other, 3);
  toggleNote(g, other, 9);

  SaveBuffer buf;
  serialize(g, buf);
  Game r;
  ASSERT_TRUE(deserialize(buf.data(), buf.size(), r));
  EXPECT_EQ(r.seed, 99u);
  EXPECT_EQ(r.difficulty, Difficulty::Hard);
  EXPECT_EQ(r.elapsedSeconds, 1234u);
  EXPECT_EQ(r.solution, g.solution);
  EXPECT_EQ(r.undo.size(), 0);
  for (int i = 0; i < CELLS; ++i) {
    EXPECT_EQ(r.board.cells[i].value, g.board.cells[i].value);
    EXPECT_EQ(r.board.cells[i].given, g.board.cells[i].given);
    EXPECT_EQ(r.board.cells[i].notes, g.board.cells[i].notes);
  }
}

TEST(SudokuSave, RejectsCorruptInput) {
  Game g;
  generate(g, 5, Difficulty::Medium);
  SaveBuffer buf;
  serialize(g, buf);
  Game r;

  EXPECT_FALSE(deserialize(buf.data(), buf.size() - 1, r));
  EXPECT_FALSE(deserialize(nullptr, buf.size(), r));

  SaveBuffer bad = buf;
  bad[0] = 0x7F;  // version
  EXPECT_FALSE(deserialize(bad.data(), bad.size(), r));

  bad = buf;
  bad[20] ^= 0x01;  // a cell byte, checksum now wrong
  EXPECT_FALSE(deserialize(bad.data(), bad.size(), r));

  bad = buf;
  bad[SAVE_SIZE - 1] ^= 0xFF;  // checksum itself
  EXPECT_FALSE(deserialize(bad.data(), bad.size(), r));

  SaveBuffer zeros{};
  EXPECT_FALSE(deserialize(zeros.data(), zeros.size(), r));

  // Valid checksum but givens without a unique solution: strip all givens and re-sign.
  Game empty;
  for (Cell& c : empty.board.cells) c = Cell{};
  serialize(empty, bad);
  EXPECT_FALSE(deserialize(bad.data(), bad.size(), r));
}

TEST(Sudoku, NotesAreRefusedOnAFilledCell) {
  sudoku::Game game;
  sudoku::generate(game, 7, sudoku::Difficulty::Easy);
  int empty = -1;
  for (int i = 0; i < sudoku::CELLS && empty < 0; ++i) {
    if (!game.board.cells[i].given) empty = i;
  }
  ASSERT_GE(empty, 0);
  ASSERT_TRUE(sudoku::setValue(game, empty, 5));
  EXPECT_FALSE(sudoku::toggleNote(game, empty, 3));
}
