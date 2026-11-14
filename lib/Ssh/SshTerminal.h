#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The screen model of the SSH terminal: a grid of cells fed with the bytes a server sends. A VT100
// subset, enough for a shell prompt and line-oriented programs (ls, cat, git, apt, tail). There is
// no alternate screen: full-screen programs (vim, htop, less, tmux) are not supported; the request
// for one is counted so the screen can say so. No platform code: tested on the computer.
namespace sshterm {

// One character position. 4 bytes, so 80 columns x (rows + scrollback) stays small.
struct Cell {
  uint16_t ch = ' ';  // a code point of the Basic Multilingual Plane; others are shown as '?'
  uint8_t attr = 0;   // ATTR_* bits
  uint8_t pad = 0;
};

constexpr uint8_t ATTR_BOLD = 0x01;
constexpr uint8_t ATTR_UNDERLINE = 0x02;
constexpr uint8_t ATTR_INVERSE = 0x04;

// Gives and takes back the one block the terminal lives in (PSRAM on the device, malloc in tests).
struct Memory {
  void* (*alloc)(size_t bytes) = nullptr;
  void (*release)(void* block) = nullptr;
};

class Terminal {
 public:
  static constexpr uint16_t MAX_COLS = 132;
  static constexpr uint16_t MAX_ROWS = 64;
  static constexpr uint16_t MAX_SCROLLBACK = 2000;

  Terminal() = default;
  ~Terminal();
  Terminal(const Terminal&) = delete;
  Terminal& operator=(const Terminal&) = delete;

  // The bytes begin() will ask `memory` for.
  static size_t bytesFor(uint16_t cols, uint16_t rows, uint16_t scrollback);
  // False when a size is out of range or the block could not be had. Calling it again starts over.
  bool begin(uint16_t cols, uint16_t rows, uint16_t scrollback, const Memory& memory);
  bool ready() const { return cells_ != nullptr; }

  uint16_t cols() const { return cols_; }
  uint16_t rows() const { return rows_; }

  // Bytes from the server.
  void feed(const uint8_t* data, size_t len);
  void feed(const std::string& text) { feed(reinterpret_cast<const uint8_t*>(text.data()), text.size()); }

  // What the terminal has to send back to the server (answers to "where is the cursor" and "what
  // are you"); empty most of the time. Cleared by the call.
  std::string takeReply();

  // --- what to draw ---
  // Row `row` of the picture when the view is scrolled back by `viewOffset` lines (0: the live
  // screen). nullptr when out of range.
  const Cell* line(uint16_t row, uint16_t viewOffset = 0) const;
  // Lines above the screen that can be scrolled back to.
  uint16_t scrollbackLines() const { return history_; }
  uint16_t cursorRow() const { return curRow_; }
  uint16_t cursorCol() const { return curCol_ < cols_ ? curCol_ : static_cast<uint16_t>(cols_ - 1); }
  bool cursorVisible() const { return cursorVisible_; }
  // The arrow keys are to be sent as ESC O A (the server asked with ESC [ ? 1 h).
  bool applicationCursorKeys() const { return appCursor_; }
  // Row `row` as UTF-8 without the spaces at its end (tests, and the log).
  std::string rowText(uint16_t row, uint16_t viewOffset = 0) const;

  // --- what changed since the last take (the e-ink redraw policy reads these) ---
  struct Changes {
    uint64_t dirtyRows = 0;      // bit r: row r of the live screen changed
    uint32_t scrolledLines = 0;  // lines that left the top of the screen
    bool cleared = false;        // the whole screen was erased
    bool any() const { return dirtyRows != 0 || scrolledLines != 0 || cleared; }
  };
  Changes takeChanges();
  bool hasChanges() const { return changes_.any(); }

  // True for characters that take no cell: combining marks, zero-width spaces and joiners, bidi
  // controls, invisible operators, variation selectors and the byte order mark.
  static bool isZeroWidth(uint32_t cp);

  // How many times the server asked for the alternate screen (a full-screen program started).
  uint32_t alternateScreenRequests() const { return altRequests_; }

 private:
  enum class State : uint8_t { Ground, Escape, EscapeSkip, Csi, StringSeq, StringEsc };

  Cell* rowPtr(uint16_t row);
  const Cell* rowPtr(uint16_t row) const;
  void markDirty(uint16_t row) { changes_.dirtyRows |= (uint64_t{1} << row); }
  void markAllDirty();
  static void clearCells(Cell* from, uint16_t count);
  void eraseRow(uint16_t row, uint16_t fromCol, uint16_t toCol);
  void reset();

  void putChar(uint32_t cp);
  void control(uint8_t c);
  void lineFeed();
  void reverseIndex();
  void scrollUp(uint16_t top, uint16_t bottom, uint16_t count);
  void scrollDown(uint16_t top, uint16_t bottom, uint16_t count);
  void escape(uint8_t c);
  void csiByte(uint8_t c);
  void csiDispatch(uint8_t final);
  void setMode(bool on);
  void selectGraphics();
  void moveTo(int row, int col);
  uint16_t param(size_t index, uint16_t fallback) const;

  Memory memory_{};
  Cell* cells_ = nullptr;
  uint16_t cols_ = 0;
  uint16_t rows_ = 0;
  uint16_t capacity_ = 0;  // lines in the ring: rows + scrollback
  uint16_t first_ = 0;     // ring index of the top line of the live screen
  uint16_t history_ = 0;   // lines of scrollback that hold something

  uint16_t curRow_ = 0;
  uint16_t curCol_ = 0;  // == cols_ while a wrap is pending
  uint16_t savedRow_ = 0;
  uint16_t savedCol_ = 0;
  uint16_t regionTop_ = 0;
  uint16_t regionBottom_ = 0;  // inclusive
  uint8_t attr_ = 0;
  bool autoWrap_ = true;
  bool cursorVisible_ = true;
  bool appCursor_ = false;

  State state_ = State::Ground;
  static constexpr size_t MAX_PARAMS = 8;
  uint16_t params_[MAX_PARAMS]{};
  uint8_t paramCount_ = 0;
  bool paramStarted_ = false;
  bool paramOverflow_ = false;  // more than MAX_PARAMS parameters: the rest are ignored
  char csiPrefix_ = 0;          // '?', '>' or 0
  bool csiIntermediate_ = false;

  uint32_t utf8Code_ = 0;
  uint8_t utf8Left_ = 0;

  Changes changes_{};
  uint32_t altRequests_ = 0;
  std::string reply_;
};

// When the e-ink panel is redrawn for a terminal that keeps printing: output is batched, a fast
// refresh at most every `minIntervalMs`, and a full refresh after the screen was erased or every
// `fullAfterLines` lines of scrolling (ghosting builds up otherwise).
class RedrawPolicy {
 public:
  enum class Action : uint8_t { None, Fast, Full };

  explicit RedrawPolicy(uint32_t minIntervalMs = 500, uint32_t fullAfterLines = 120)
      : minIntervalMs_(minIntervalMs), fullAfterLines_(fullAfterLines) {}

  // Tell it about changes as they are taken from the terminal.
  void note(const Terminal::Changes& changes);
  // Something outside the terminal changed the picture (the view was scrolled, a key row changed).
  void noteOther() { pending_ = true; }
  // What to do now. Fast or Full means "draw now": the policy then counts from `nowMs`.
  Action poll(uint32_t nowMs);

 private:
  uint32_t minIntervalMs_;
  uint32_t fullAfterLines_;
  uint32_t lastDrawMs_ = 0;
  uint32_t linesSinceFull_ = 0;
  bool drawnOnce_ = false;
  bool pending_ = false;
  bool wantFull_ = false;
};

}  // namespace sshterm
