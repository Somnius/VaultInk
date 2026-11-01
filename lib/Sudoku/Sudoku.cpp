#include "Sudoku.h"

#include <cstring>

namespace sudoku {

namespace {

constexpr uint16_t ALL_DIGITS = 0x1FF;

inline int rowOf(int cell) { return cell / 9; }
inline int colOf(int cell) { return cell % 9; }
inline int boxOf(int cell) { return (cell / 27) * 3 + (cell % 9) / 3; }

inline int popcount9(uint16_t m) {
  int n = 0;
  while (m) {
    m &= static_cast<uint16_t>(m - 1);
    ++n;
  }
  return n;
}

// Lowest set bit -> digit 1..9.
inline uint8_t firstDigit(uint16_t m) {
  uint8_t d = 1;
  while (!(m & 1)) {
    m >>= 1;
    ++d;
  }
  return d;
}

struct Masks {
  uint16_t rows[9] = {};
  uint16_t cols[9] = {};
  uint16_t boxes[9] = {};

  // Returns false when a filled cell repeats a digit in its row, column or box.
  bool load(const Grid& grid) {
    for (int i = 0; i < CELLS; ++i) {
      const uint8_t v = grid[i];
      if (v == 0) continue;
      if (v > 9) return false;
      const uint16_t bit = static_cast<uint16_t>(1u << (v - 1));
      if ((rows[rowOf(i)] | cols[colOf(i)] | boxes[boxOf(i)]) & bit) return false;
      set(i, bit);
    }
    return true;
  }
  void set(int i, uint16_t bit) {
    rows[rowOf(i)] |= bit;
    cols[colOf(i)] |= bit;
    boxes[boxOf(i)] |= bit;
  }
  void unset(int i, uint16_t bit) {
    rows[rowOf(i)] &= static_cast<uint16_t>(~bit);
    cols[colOf(i)] &= static_cast<uint16_t>(~bit);
    boxes[boxOf(i)] &= static_cast<uint16_t>(~bit);
  }
  uint16_t free(int i) const {
    return static_cast<uint16_t>(ALL_DIGITS & ~(rows[rowOf(i)] | cols[colOf(i)] | boxes[boxOf(i)]));
  }
};

// Picks the empty cell with the fewest candidates. Returns -1 when the grid is full, and sets
// `freeMask` to 0 when some empty cell has no candidate (dead end).
int pickCell(const Grid& grid, const Masks& m, uint16_t& freeMask) {
  int best = -1;
  int bestCount = 10;
  freeMask = 0;
  for (int i = 0; i < CELLS; ++i) {
    if (grid[i] != 0) continue;
    const uint16_t f = m.free(i);
    const int n = popcount9(f);
    if (n < bestCount) {
      best = i;
      bestCount = n;
      freeMask = f;
      if (n <= 1) break;
    }
  }
  return best;
}

// Counts solutions up to `limit`. `found` accumulates. Stack per level: a few ints only
// (grid and masks are shared by reference).
void countRec(Grid& grid, Masks& m, int limit, int& found) {
  uint16_t f;
  const int cell = pickCell(grid, m, f);
  if (cell < 0) {
    ++found;
    return;
  }
  while (f && found < limit) {
    const uint16_t bit = static_cast<uint16_t>(f & static_cast<uint16_t>(-f));
    f = static_cast<uint16_t>(f & ~bit);
    grid[cell] = firstDigit(bit);
    m.set(cell, bit);
    countRec(grid, m, limit, found);
    m.unset(cell, bit);
    grid[cell] = 0;
  }
}

// Fills the first solution in place. Returns true when done.
bool solveRec(Grid& grid, Masks& m) {
  uint16_t f;
  const int cell = pickCell(grid, m, f);
  if (cell < 0) return true;
  while (f) {
    const uint16_t bit = static_cast<uint16_t>(f & static_cast<uint16_t>(-f));
    f = static_cast<uint16_t>(f & ~bit);
    grid[cell] = firstDigit(bit);
    m.set(cell, bit);
    if (solveRec(grid, m)) return true;
    m.unset(cell, bit);
    grid[cell] = 0;
  }
  return false;
}

struct XorShift32 {
  uint32_t state;
  explicit XorShift32(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
  uint32_t next() {
    uint32_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
  }
  // Uniform-ish in [0, n).
  uint32_t below(uint32_t n) { return next() % n; }
};

// Fills a full valid grid with random digit order at each step (always succeeds: an empty
// grid has solutions and the search backtracks).
bool fillRec(Grid& grid, Masks& m, XorShift32& rng) {
  uint16_t f;
  const int cell = pickCell(grid, m, f);
  if (cell < 0) return true;
  uint8_t order[9];
  int n = 0;
  for (uint8_t d = 1; d <= 9; ++d) {
    if (f & (1u << (d - 1))) order[n++] = d;
  }
  for (int i = n - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(static_cast<uint32_t>(i + 1)));
    const uint8_t t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (int i = 0; i < n; ++i) {
    const uint16_t bit = static_cast<uint16_t>(1u << (order[i] - 1));
    grid[cell] = order[i];
    m.set(cell, bit);
    if (fillRec(grid, m, rng)) return true;
    m.unset(cell, bit);
    grid[cell] = 0;
  }
  return false;
}

bool validCell(int cell) { return cell >= 0 && cell < CELLS; }

void pushUndo(Game& game, int cell) {
  const Cell& c = game.board.cells[cell];
  Move m;
  m.cell = static_cast<uint8_t>(cell);
  m.prevValue = c.value;
  m.prevNotes = c.notes;
  game.undo.push(m);
}

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace

Grid Board::values() const {
  Grid g;
  for (int i = 0; i < CELLS; ++i) g[i] = cells[i].value;
  return g;
}

void UndoStack::clear() {
  head = 0;
  count = 0;
}

void UndoStack::push(const Move& m) {
  moves[head] = m;
  head = static_cast<uint8_t>((head + 1) % CAPACITY);
  if (count < CAPACITY) ++count;
}

bool UndoStack::pop(Move& out) {
  if (count == 0) return false;
  head = static_cast<uint8_t>((head + CAPACITY - 1) % CAPACITY);
  --count;
  out = moves[head];
  return true;
}

int countSolutions(const Grid& grid, int limit) {
  if (limit <= 0) return 0;
  Masks m;
  if (!m.load(grid)) return 0;
  Grid work = grid;
  int found = 0;
  countRec(work, m, limit, found);
  return found;
}

bool solve(Grid& grid) {
  Masks m;
  if (!m.load(grid)) return false;
  Grid work = grid;
  if (!solveRec(work, m)) return false;
  grid = work;
  return true;
}

void generate(Game& game, uint32_t seed, Difficulty difficulty) {
  XorShift32 rng(seed);
  Grid full{};
  {
    Masks m;
    fillRec(full, m, rng);
  }

  // Visit the cells in a random order and drop each one whose removal keeps the solution
  // unique, until the target is reached. One pass over 81 cells bounds the work.
  uint8_t order[CELLS];
  for (int i = 0; i < CELLS; ++i) order[i] = static_cast<uint8_t>(i);
  for (int i = CELLS - 1; i > 0; --i) {
    const int j = static_cast<int>(rng.below(static_cast<uint32_t>(i + 1)));
    const uint8_t t = order[i];
    order[i] = order[j];
    order[j] = t;
  }

  Grid puzzle = full;
  int givens = CELLS;
  const int target = targetGivens(difficulty);
  for (int k = 0; k < CELLS && givens > target; ++k) {
    const int cell = order[k];
    const uint8_t saved = puzzle[cell];
    puzzle[cell] = 0;
    if (countSolutions(puzzle, 2) == 1) {
      --givens;
    } else {
      puzzle[cell] = saved;
    }
  }

  game.board = Board{};
  for (int i = 0; i < CELLS; ++i) {
    game.board.cells[i].value = puzzle[i];
    game.board.cells[i].given = puzzle[i] != 0;
  }
  game.solution = full;
  game.seed = seed;
  game.difficulty = difficulty;
  game.elapsedSeconds = 0;
  game.undo.clear();
}

int countGivens(const Board& board) {
  int n = 0;
  // cppcheck-suppress useStlAlgorithm
  for (const Cell& c : board.cells) n += c.given ? 1 : 0;
  return n;
}

std::array<bool, CELLS> conflicts(const Board& board) {
  std::array<bool, CELLS> out{};
  for (int i = 0; i < CELLS; ++i) {
    const uint8_t v = board.cells[i].value;
    if (v == 0) continue;
    for (int j = 0; j < CELLS; ++j) {
      if (j == i || board.cells[j].value != v) continue;
      if (rowOf(i) == rowOf(j) || colOf(i) == colOf(j) || boxOf(i) == boxOf(j)) {
        out[i] = true;
        break;
      }
    }
  }
  return out;
}

bool isComplete(const Board& board) {
  // cppcheck-suppress useStlAlgorithm
  for (const Cell& c : board.cells) {
    if (c.value == 0) return false;
  }
  return true;
}

bool isSolved(const Board& board) {
  if (!isComplete(board)) return false;
  const auto bad = conflicts(board);
  // cppcheck-suppress useStlAlgorithm
  for (bool b : bad) {
    if (b) return false;
  }
  return true;
}

uint16_t candidates(const Board& board, int cell) {
  if (!validCell(cell) || board.cells[cell].value != 0) return 0;
  uint16_t used = 0;
  for (int j = 0; j < CELLS; ++j) {
    const uint8_t v = board.cells[j].value;
    if (v == 0 || v > 9) continue;
    if (rowOf(j) == rowOf(cell) || colOf(j) == colOf(cell) || boxOf(j) == boxOf(cell)) {
      used |= static_cast<uint16_t>(1u << (v - 1));
    }
  }
  return static_cast<uint16_t>(ALL_DIGITS & ~used);
}

Hint hint(const Game& game) {
  Hint h;
  bool known = false;
  for (uint8_t v : game.solution) known = known || v != 0;
  if (!known) return h;
  const auto bad = conflicts(game.board);
  for (int i = 0; i < CELLS; ++i) {
    if (bad[i]) return h;
    const uint8_t v = game.board.cells[i].value;
    if (v != 0 && v != game.solution[i]) return h;
  }
  int best = -1;
  int bestCount = 10;
  for (int i = 0; i < CELLS; ++i) {
    if (game.board.cells[i].value != 0) continue;
    const int n = popcount9(candidates(game.board, i));
    if (n < bestCount) {
      best = i;
      bestCount = n;
    }
  }
  if (best < 0) return h;
  h.found = true;
  h.cell = static_cast<uint8_t>(best);
  h.value = game.solution[best];
  return h;
}

bool setValue(Game& game, int cell, uint8_t value) {
  if (!validCell(cell) || value < 1 || value > 9) return false;
  Cell& c = game.board.cells[cell];
  if (c.given) return false;
  if (c.value == value && c.notes == 0) return false;
  pushUndo(game, cell);
  c.value = value;
  c.notes = 0;
  return true;
}

bool clearCell(Game& game, int cell) {
  if (!validCell(cell)) return false;
  Cell& c = game.board.cells[cell];
  if (c.given) return false;
  if (c.value == 0 && c.notes == 0) return false;
  pushUndo(game, cell);
  c.value = 0;
  c.notes = 0;
  return true;
}

bool toggleNote(Game& game, int cell, uint8_t digit) {
  if (!validCell(cell) || digit < 1 || digit > 9) return false;
  Cell& c = game.board.cells[cell];
  if (c.given || c.value != 0) return false;  // notes live only on empty cells
  pushUndo(game, cell);
  c.notes ^= static_cast<uint16_t>(1u << (digit - 1));
  return true;
}

bool undo(Game& game) {
  Move m;
  if (!game.undo.pop(m)) return false;
  Cell& c = game.board.cells[m.cell];
  c.value = m.prevValue;
  c.notes = m.prevNotes;
  return true;
}

namespace {
void put32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
uint32_t get32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

void serialize(const Game& game, SaveBuffer& out) {
  uint8_t* p = out.data();
  p[0] = SAVE_VERSION;
  put32(p + 1, game.seed);
  p[5] = static_cast<uint8_t>(game.difficulty);
  put32(p + 6, game.elapsedSeconds);
  uint8_t* q = p + 10;
  for (int i = 0; i < CELLS; ++i) {
    const Cell& c = game.board.cells[i];
    // bits 0-3 value, bit 4 given, bits 5-13 notes.
    const uint16_t w = static_cast<uint16_t>((c.value & 0x0F) | (c.given ? 0x10 : 0) | ((c.notes & 0x1FF) << 5));
    q[2 * i] = static_cast<uint8_t>(w);
    q[2 * i + 1] = static_cast<uint8_t>(w >> 8);
  }
  const uint16_t crc = crc16(p, SAVE_SIZE - 2);
  p[SAVE_SIZE - 2] = static_cast<uint8_t>(crc);
  p[SAVE_SIZE - 1] = static_cast<uint8_t>(crc >> 8);
}

bool deserialize(const uint8_t* data, size_t size, Game& game) {
  if (data == nullptr || size != SAVE_SIZE) return false;
  if (data[0] != SAVE_VERSION) return false;
  const uint16_t stored = static_cast<uint16_t>(data[SAVE_SIZE - 2] | (data[SAVE_SIZE - 1] << 8));
  if (crc16(data, SAVE_SIZE - 2) != stored) return false;
  if (data[5] > static_cast<uint8_t>(Difficulty::Hard)) return false;

  Game g;
  g.seed = get32(data + 1);
  g.difficulty = static_cast<Difficulty>(data[5]);
  g.elapsedSeconds = get32(data + 6);
  const uint8_t* q = data + 10;
  Grid givens{};
  for (int i = 0; i < CELLS; ++i) {
    const uint16_t w = static_cast<uint16_t>(q[2 * i] | (q[2 * i + 1] << 8));
    Cell& c = g.board.cells[i];
    c.value = static_cast<uint8_t>(w & 0x0F);
    c.given = (w & 0x10) != 0;
    c.notes = static_cast<uint16_t>(w >> 5);
    if (c.value > 9 || c.notes > ALL_DIGITS) return false;
    if (c.given && (c.value == 0 || c.notes != 0)) return false;
    if (c.given) givens[i] = c.value;
  }
  Grid sol = givens;
  if (countSolutions(givens, 2) != 1 || !solve(sol)) return false;
  g.solution = sol;
  game = g;
  return true;
}

}  // namespace sudoku
