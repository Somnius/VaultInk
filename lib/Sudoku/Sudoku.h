#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Platform-free Sudoku engine: board, solver, generator, hints, undo and a compact save format.
// No heap, no Arduino/ESP includes. The solver recurses at most one level per empty cell (<= 81
// levels), each frame holding a handful of ints plus a copy of three 9-entry bitmask tables
// (about 100 bytes), so a full solve stays under ~10 KB of stack.
namespace sudoku {

constexpr int CELLS = 81;

enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2 };

// Target number of givens per difficulty (generate stops removing cells once reached).
constexpr uint8_t targetGivens(Difficulty d) { return d == Difficulty::Easy ? 40 : d == Difficulty::Medium ? 32 : 26; }

// A plain 9x9 grid of values 0 (empty) .. 9, row-major.
using Grid = std::array<uint8_t, CELLS>;

struct Cell {
  uint8_t value = 0;   // 0 = empty
  bool given = false;  // part of the puzzle, never editable
  uint16_t notes = 0;  // bit (d-1) set = pencil note for digit d
};

struct Board {
  std::array<Cell, CELLS> cells{};

  Grid values() const;
};

// One undoable change to a cell.
struct Move {
  uint8_t cell = 0;
  uint8_t prevValue = 0;
  uint16_t prevNotes = 0;
};

// Fixed ring of the last 64 moves.
class UndoStack {
 public:
  static constexpr uint8_t CAPACITY = 64;

  void clear();
  void push(const Move& m);
  bool pop(Move& out);
  uint8_t size() const { return count; }

 private:
  std::array<Move, CAPACITY> moves{};
  uint8_t head = 0;  // index where the next push goes
  uint8_t count = 0;
};

struct Hint {
  bool found = false;
  uint8_t cell = 0;
  uint8_t value = 0;
};

struct Game {
  Board board;
  Grid solution{};  // the unique solution of the givens; all zeros when unknown
  uint32_t seed = 0;
  Difficulty difficulty = Difficulty::Easy;
  uint32_t elapsedSeconds = 0;
  UndoStack undo;
};

// ---- solver ----

// Number of solutions of `grid`, stopping at `limit` (so limit=2 answers "is it unique?").
// Returns 0 when the filled cells already break a rule.
int countSolutions(const Grid& grid, int limit);

// Fills `grid` in place with its first solution. Returns false when there is none.
bool solve(Grid& grid);

// ---- generator ----

// Deterministic: the same seed and difficulty always give the same puzzle. Fills game.board
// (givens only), game.solution, seed, difficulty; resets elapsed time and undo.
void generate(Game& game, uint32_t seed, Difficulty difficulty);

// Count of given cells.
int countGivens(const Board& board);

// ---- board helpers ----

// True for every cell whose value shares a row, column or box with an equal value.
std::array<bool, CELLS> conflicts(const Board& board);
bool isComplete(const Board& board);  // every cell has a value
bool isSolved(const Board& board);    // complete and no conflicts

// Digits the cell could still take given the other values on the board (bit d-1 = digit d).
// Zero for a filled cell.
uint16_t candidates(const Board& board, int cell);

// Next cell to fill from the unique solution: the empty cell with the fewest candidates. Not
// found when the board conflicts, when a filled cell differs from the solution, when the board
// is complete, or when the solution is unknown.
Hint hint(const Game& game);

// ---- editing (all refuse givens and return false when nothing changed) ----

bool setValue(Game& game, int cell, uint8_t value);  // value 1..9; clears the cell's notes
bool clearCell(Game& game, int cell);                // value and notes
bool toggleNote(Game& game, int cell, uint8_t digit);
bool undo(Game& game);  // reverts the last move

// ---- save format ----

// Version(1) + seed(4) + difficulty(1) + elapsed(4) + 81 cells x 2 bytes + CRC-16(2).
constexpr size_t SAVE_SIZE = 1 + 4 + 1 + 4 + CELLS * 2 + 2;
constexpr uint8_t SAVE_VERSION = 1;

using SaveBuffer = std::array<uint8_t, SAVE_SIZE>;

void serialize(const Game& game, SaveBuffer& out);

// Rejects a bad version, a bad checksum, out-of-range fields and givens without a unique
// solution. On success the game's solution is recomputed and the undo stack is empty.
bool deserialize(const uint8_t* data, size_t size, Game& game);

}  // namespace sudoku
