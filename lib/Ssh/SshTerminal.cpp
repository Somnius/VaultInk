#include "SshTerminal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sshterm {

namespace {
constexpr uint16_t TAB_WIDTH = 8;

void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}
}  // namespace

Terminal::~Terminal() {
  if (cells_ && memory_.release) memory_.release(cells_);
}

size_t Terminal::bytesFor(const uint16_t cols, const uint16_t rows, const uint16_t scrollback) {
  return static_cast<size_t>(cols) * (static_cast<size_t>(rows) + scrollback) * sizeof(Cell);
}

bool Terminal::begin(const uint16_t cols, const uint16_t rows, const uint16_t scrollback, const Memory& memory) {
  if (cells_ && memory_.release) memory_.release(cells_);
  cells_ = nullptr;
  if (cols < 2 || cols > MAX_COLS || rows < 2 || rows > MAX_ROWS || scrollback > MAX_SCROLLBACK) return false;
  if (!memory.alloc || !memory.release) return false;
  cells_ = static_cast<Cell*>(memory.alloc(bytesFor(cols, rows, scrollback)));
  if (!cells_) return false;
  memory_ = memory;
  cols_ = cols;
  rows_ = rows;
  capacity_ = static_cast<uint16_t>(rows + scrollback);
  for (size_t i = 0; i < static_cast<size_t>(cols_) * capacity_; ++i) cells_[i] = Cell{};
  first_ = 0;
  history_ = 0;
  reset();
  changes_ = Changes{};
  altRequests_ = 0;
  reply_.clear();
  return true;
}

void Terminal::reset() {
  curRow_ = curCol_ = 0;
  savedRow_ = savedCol_ = 0;
  regionTop_ = 0;
  regionBottom_ = static_cast<uint16_t>(rows_ - 1);
  attr_ = 0;
  autoWrap_ = true;
  cursorVisible_ = true;
  appCursor_ = false;
  state_ = State::Ground;
  utf8Left_ = 0;
}

Cell* Terminal::rowPtr(const uint16_t row) { return cells_ + static_cast<size_t>((first_ + row) % capacity_) * cols_; }

const Cell* Terminal::rowPtr(const uint16_t row) const {
  return cells_ + static_cast<size_t>((first_ + row) % capacity_) * cols_;
}

const Cell* Terminal::line(const uint16_t row, const uint16_t viewOffset) const {
  if (!cells_ || row >= rows_ || viewOffset > history_) return nullptr;
  const uint32_t index = (static_cast<uint32_t>(first_) + capacity_ + row - viewOffset) % capacity_;
  return cells_ + static_cast<size_t>(index) * cols_;
}

std::string Terminal::rowText(const uint16_t row, const uint16_t viewOffset) const {
  std::string out;
  const Cell* cells = line(row, viewOffset);
  if (!cells) return out;
  uint16_t end = cols_;
  while (end > 0 && cells[end - 1].ch == ' ') --end;
  for (uint16_t i = 0; i < end; ++i) appendUtf8(out, cells[i].ch);
  return out;
}

std::string Terminal::takeReply() {
  std::string out;
  out.swap(reply_);
  return out;
}

Terminal::Changes Terminal::takeChanges() {
  const Changes out = changes_;
  changes_ = Changes{};
  return out;
}

void Terminal::markAllDirty() { changes_.dirtyRows |= rows_ >= 64 ? ~uint64_t{0} : ((uint64_t{1} << rows_) - 1); }

void Terminal::clearCells(Cell* from, const uint16_t count) {
  for (uint16_t i = 0; i < count; ++i) from[i] = Cell{};
}

void Terminal::eraseRow(const uint16_t row, const uint16_t fromCol, const uint16_t toCol) {
  if (row >= rows_ || fromCol >= toCol) return;
  clearCells(rowPtr(row) + fromCol, static_cast<uint16_t>(std::min<uint16_t>(toCol, cols_) - fromCol));
  markDirty(row);
}

void Terminal::scrollUp(const uint16_t top, const uint16_t bottom, uint16_t count) {
  if (top > bottom || bottom >= rows_) return;
  count = std::min<uint16_t>(count, static_cast<uint16_t>(bottom - top + 1));
  if (count == 0) return;
  if (top == 0 && bottom == rows_ - 1) {
    // The whole screen moves: the top lines become scrollback by turning the ring.
    for (uint16_t i = 0; i < count; ++i) {
      first_ = static_cast<uint16_t>((first_ + 1) % capacity_);
      if (history_ < capacity_ - rows_) ++history_;
      clearCells(rowPtr(static_cast<uint16_t>(rows_ - 1)), cols_);
    }
    changes_.scrolledLines += count;
  } else {
    for (uint16_t row = top; row + count <= bottom; ++row) {
      memcpy(rowPtr(row), rowPtr(static_cast<uint16_t>(row + count)), sizeof(Cell) * cols_);
    }
    for (uint16_t row = static_cast<uint16_t>(bottom - count + 1); row <= bottom; ++row) clearCells(rowPtr(row), cols_);
  }
  markAllDirty();
}

void Terminal::scrollDown(const uint16_t top, const uint16_t bottom, uint16_t count) {
  if (top > bottom || bottom >= rows_) return;
  count = std::min<uint16_t>(count, static_cast<uint16_t>(bottom - top + 1));
  if (count == 0) return;
  for (int row = bottom; row - count >= top; --row) {
    memcpy(rowPtr(static_cast<uint16_t>(row)), rowPtr(static_cast<uint16_t>(row - count)), sizeof(Cell) * cols_);
  }
  for (uint16_t row = top; row < top + count; ++row) clearCells(rowPtr(row), cols_);
  markAllDirty();
}

void Terminal::lineFeed() {
  if (curRow_ == regionBottom_) {
    scrollUp(regionTop_, regionBottom_, 1);
  } else if (curRow_ + 1 < rows_) {
    ++curRow_;
  }
}

void Terminal::reverseIndex() {
  if (curRow_ == regionTop_) {
    scrollDown(regionTop_, regionBottom_, 1);
  } else if (curRow_ > 0) {
    --curRow_;
  }
}

void Terminal::moveTo(int row, int col) {
  row = std::clamp(row, 0, rows_ - 1);
  col = std::clamp(col, 0, cols_ - 1);
  curRow_ = static_cast<uint16_t>(row);
  curCol_ = static_cast<uint16_t>(col);
}

bool Terminal::isZeroWidth(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) ||  // combining diacritical marks
         (cp >= 0x1AB0 && cp <= 0x1AFF) ||  // ... extended
         (cp >= 0x1DC0 && cp <= 0x1DFF) ||  // ... supplement
         (cp >= 0x200B && cp <= 0x200F) ||  // zero-width space, joiners, direction marks
         (cp >= 0x2028 && cp <= 0x202E) ||  // line and paragraph separators, bidi embedding controls
         (cp >= 0x2060 && cp <= 0x2064) ||  // word joiner, invisible operators
         (cp >= 0x20D0 && cp <= 0x20FF) ||  // combining marks for symbols
         (cp >= 0xFE00 && cp <= 0xFE0F) ||  // variation selectors
         (cp >= 0xFE20 && cp <= 0xFE2F) ||  // combining half marks
         cp == 0xFEFF;                      // byte order mark
}

void Terminal::putChar(uint32_t cp) {
  // Characters with no width would need their own cell handling: dropped, so they take no cell.
  if (isZeroWidth(cp)) return;
  if (cp > 0xFFFF) cp = '?';
  if (curCol_ >= cols_) {
    if (autoWrap_) {
      curCol_ = 0;
      lineFeed();
    } else {
      curCol_ = static_cast<uint16_t>(cols_ - 1);
    }
  }
  Cell& cell = rowPtr(curRow_)[curCol_];
  cell.ch = static_cast<uint16_t>(cp);
  cell.attr = attr_;
  markDirty(curRow_);
  ++curCol_;  // cols_ here means "wrap before the next character"
}

void Terminal::control(const uint8_t c) {
  switch (c) {
    case 0x08:  // BS
      if (curCol_ >= cols_) curCol_ = static_cast<uint16_t>(cols_ - 1);
      if (curCol_ > 0) --curCol_;
      break;
    case 0x09: {  // HT
      if (curCol_ >= cols_) break;
      const uint16_t next = static_cast<uint16_t>((curCol_ / TAB_WIDTH + 1) * TAB_WIDTH);
      curCol_ = std::min<uint16_t>(next, static_cast<uint16_t>(cols_ - 1));
      break;
    }
    case 0x0A:  // LF
    case 0x0B:  // VT
    case 0x0C:  // FF
      if (curCol_ >= cols_) curCol_ = static_cast<uint16_t>(cols_ - 1);
      lineFeed();
      break;
    case 0x0D:  // CR
      curCol_ = 0;
      break;
    default:  // BEL, NUL, SO/SI and the rest: nothing to show
      break;
  }
}

void Terminal::feed(const uint8_t* data, const size_t len) {
  if (!cells_) return;
  for (size_t i = 0; i < len; ++i) {
    const uint8_t c = data[i];
    switch (state_) {
      case State::Ground:
        if (utf8Left_ > 0) {
          if ((c & 0xC0) == 0x80) {
            utf8Code_ = (utf8Code_ << 6) | (c & 0x3F);
            if (--utf8Left_ == 0) putChar(utf8Code_);
            break;
          }
          utf8Left_ = 0;  // a broken sequence: show the byte that follows on its own
        }
        if (c == 0x1B) {
          state_ = State::Escape;
        } else if (c < 0x20 || c == 0x7F) {
          control(c);
        } else if (c < 0x80) {
          putChar(c);
        } else if ((c & 0xE0) == 0xC0) {
          utf8Code_ = c & 0x1F;
          utf8Left_ = 1;
        } else if ((c & 0xF0) == 0xE0) {
          utf8Code_ = c & 0x0F;
          utf8Left_ = 2;
        } else if ((c & 0xF8) == 0xF0) {
          utf8Code_ = c & 0x07;
          utf8Left_ = 3;
        } else {
          putChar('?');
        }
        break;
      case State::Escape:
        escape(c);
        break;
      case State::EscapeSkip:  // the one byte after ESC ( ) * + # % and ESC space
        state_ = State::Ground;
        break;
      case State::Csi:
        csiByte(c);
        break;
      case State::StringSeq:  // OSC / DCS / APC / PM text: until BEL or ESC backslash
        if (c == 0x07) {
          state_ = State::Ground;
        } else if (c == 0x1B) {
          state_ = State::StringEsc;
        }
        break;
      case State::StringEsc:
        state_ = c == '\\' ? State::Ground : (c == 0x1B ? State::StringEsc : State::StringSeq);
        break;
    }
  }
}

void Terminal::escape(const uint8_t c) {
  state_ = State::Ground;
  switch (c) {
    case '[':
      state_ = State::Csi;
      paramCount_ = 0;
      paramStarted_ = false;
      paramOverflow_ = false;
      csiPrefix_ = 0;
      csiIntermediate_ = false;
      memset(params_, 0, sizeof(params_));
      break;
    case ']':
    case 'P':
    case 'X':
    case '^':
    case '_':
      state_ = State::StringSeq;
      break;
    case '(':
    case ')':
    case '*':
    case '+':
    case '#':
    case '%':
    case ' ':
      state_ = State::EscapeSkip;
      break;
    case '7':
      savedRow_ = curRow_;
      savedCol_ = curCol_;
      break;
    case '8':
      moveTo(savedRow_, savedCol_);
      break;
    case 'D':
      lineFeed();
      break;
    case 'E':
      curCol_ = 0;
      lineFeed();
      break;
    case 'M':
      reverseIndex();
      break;
    case 'c':
      reset();
      for (uint16_t row = 0; row < rows_; ++row) clearCells(rowPtr(row), cols_);
      markAllDirty();
      changes_.cleared = true;
      break;
    case 0x1B:
      state_ = State::Escape;
      break;
    default:  // ESC = and ESC > (keypad modes) and anything unknown
      break;
  }
}

void Terminal::csiByte(const uint8_t c) {
  if (c >= '0' && c <= '9') {
    if (!paramStarted_) {
      paramStarted_ = true;
      // A ninth parameter and those after it are dropped whole: their digits go nowhere.
      if (paramCount_ < MAX_PARAMS) {
        ++paramCount_;
      } else {
        paramOverflow_ = true;
      }
    }
    if (!paramOverflow_ && paramCount_ >= 1) {
      uint32_t value = static_cast<uint32_t>(params_[paramCount_ - 1]) * 10 + (c - '0');
      params_[paramCount_ - 1] = static_cast<uint16_t>(std::min<uint32_t>(value, 9999));
    }
  } else if (c == ';' || c == ':') {
    if (!paramStarted_) {  // an empty parameter
      if (paramCount_ < MAX_PARAMS) {
        ++paramCount_;
      } else {
        paramOverflow_ = true;
      }
    }
    paramStarted_ = false;
  } else if (c == '?' || c == '>' || c == '<' || c == '=') {
    csiPrefix_ = static_cast<char>(c);
  } else if (c >= 0x20 && c <= 0x2F) {
    csiIntermediate_ = true;
  } else if (c >= 0x40 && c <= 0x7E) {
    state_ = State::Ground;
    if (!csiIntermediate_) csiDispatch(c);
  } else if (c == 0x1B) {
    state_ = State::Escape;
  } else if (c < 0x20) {
    control(c);  // a control character inside a sequence acts at once
  } else {
    state_ = State::Ground;
  }
}

uint16_t Terminal::param(const size_t index, const uint16_t fallback) const {
  if (index >= paramCount_ || params_[index] == 0) return fallback;
  return params_[index];
}

void Terminal::setMode(const bool on) {
  if (csiPrefix_ != '?') return;
  for (size_t i = 0; i < paramCount_; ++i) {
    switch (params_[i]) {
      case 1:
        appCursor_ = on;
        break;
      case 7:
        autoWrap_ = on;
        break;
      case 25:
        cursorVisible_ = on;
        markDirty(curRow_);
        break;
      case 47:
      case 1047:
      case 1049:
        // No alternate screen here. The screen is erased so the program's picture does not mix
        // with the shell's lines, and the request is counted.
        if (on) ++altRequests_;
        for (uint16_t row = 0; row < rows_; ++row) clearCells(rowPtr(row), cols_);
        moveTo(0, 0);
        markAllDirty();
        changes_.cleared = true;
        break;
      default:
        break;
    }
  }
}

void Terminal::selectGraphics() {
  if (paramCount_ == 0) {
    attr_ = 0;
    return;
  }
  for (size_t i = 0; i < paramCount_; ++i) {
    const uint16_t p = params_[i];
    if (p == 0) {
      attr_ = 0;
    } else if (p == 1) {
      attr_ |= ATTR_BOLD;
    } else if (p == 4) {
      attr_ |= ATTR_UNDERLINE;
    } else if (p == 7) {
      attr_ |= ATTR_INVERSE;
    } else if (p == 22) {
      attr_ &= static_cast<uint8_t>(~ATTR_BOLD);
    } else if (p == 24) {
      attr_ &= static_cast<uint8_t>(~ATTR_UNDERLINE);
    } else if (p == 27) {
      attr_ &= static_cast<uint8_t>(~ATTR_INVERSE);
    } else if (p == 38 || p == 48) {
      // An extended colour: 38;5;n or 38;2;r;g;b. Its arguments are not attributes.
      if (i + 1 < paramCount_ && params_[i + 1] == 5) {
        i += 2;
      } else if (i + 1 < paramCount_ && params_[i + 1] == 2) {
        i += 4;
      }
    }
    // Colours (30-37, 40-47, 90-107) have nothing to map to on a black and white panel.
  }
}

void Terminal::csiDispatch(const uint8_t final) {
  if (csiPrefix_ != 0 && final != 'h' && final != 'l') {
    // Private sequences other than set/reset mode: only "what are you" (CSI > c) gets an answer.
    if (csiPrefix_ == '>' && final == 'c') reply_ += "\x1b[>0;0;0c";
    return;
  }
  const uint16_t col = curCol_ >= cols_ ? static_cast<uint16_t>(cols_ - 1) : curCol_;
  switch (final) {
    case 'A':
      moveTo(std::max<int>(curRow_ - param(0, 1), curRow_ >= regionTop_ ? regionTop_ : 0), col);
      break;
    case 'B':
    case 'e':
      moveTo(std::min<int>(curRow_ + param(0, 1), curRow_ <= regionBottom_ ? regionBottom_ : rows_ - 1), col);
      break;
    case 'C':
    case 'a':
      moveTo(curRow_, col + param(0, 1));
      break;
    case 'D':
      moveTo(curRow_, col - param(0, 1));
      break;
    case 'E':
      moveTo(curRow_ + param(0, 1), 0);
      break;
    case 'F':
      moveTo(curRow_ - param(0, 1), 0);
      break;
    case 'G':
    case '`':
      moveTo(curRow_, param(0, 1) - 1);
      break;
    case 'd':
      moveTo(param(0, 1) - 1, col);
      break;
    case 'H':
    case 'f':
      moveTo(param(0, 1) - 1, param(1, 1) - 1);
      break;
    case 'J': {
      const uint16_t mode = param(0, 0);
      if (mode == 0) {
        eraseRow(curRow_, col, cols_);
        for (uint16_t row = static_cast<uint16_t>(curRow_ + 1); row < rows_; ++row) eraseRow(row, 0, cols_);
      } else if (mode == 1) {
        for (uint16_t row = 0; row < curRow_; ++row) eraseRow(row, 0, cols_);
        eraseRow(curRow_, 0, static_cast<uint16_t>(col + 1));
      } else if (mode == 2 || mode == 3) {
        for (uint16_t row = 0; row < rows_; ++row) eraseRow(row, 0, cols_);
        changes_.cleared = true;
        if (mode == 3) history_ = 0;
      }
      break;
    }
    case 'K': {
      const uint16_t mode = param(0, 0);
      if (mode == 0) {
        eraseRow(curRow_, col, cols_);
      } else if (mode == 1) {
        eraseRow(curRow_, 0, static_cast<uint16_t>(col + 1));
      } else if (mode == 2) {
        eraseRow(curRow_, 0, cols_);
      }
      break;
    }
    case 'L':
      if (curRow_ >= regionTop_ && curRow_ <= regionBottom_) scrollDown(curRow_, regionBottom_, param(0, 1));
      break;
    case 'M':
      if (curRow_ >= regionTop_ && curRow_ <= regionBottom_) scrollUp(curRow_, regionBottom_, param(0, 1));
      break;
    case 'S':
      scrollUp(regionTop_, regionBottom_, param(0, 1));
      break;
    case 'T':
      scrollDown(regionTop_, regionBottom_, param(0, 1));
      break;
    case 'P': {  // delete characters: the rest of the line moves left
      const uint16_t n = std::min<uint16_t>(param(0, 1), static_cast<uint16_t>(cols_ - col));
      Cell* row = rowPtr(curRow_);
      memmove(row + col, row + col + n, sizeof(Cell) * (cols_ - col - n));
      clearCells(row + cols_ - n, n);
      markDirty(curRow_);
      break;
    }
    case '@': {  // insert blanks: the rest of the line moves right
      const uint16_t n = std::min<uint16_t>(param(0, 1), static_cast<uint16_t>(cols_ - col));
      Cell* row = rowPtr(curRow_);
      memmove(row + col + n, row + col, sizeof(Cell) * (cols_ - col - n));
      clearCells(row + col, n);
      markDirty(curRow_);
      break;
    }
    case 'X':
      eraseRow(curRow_, col, static_cast<uint16_t>(std::min<int>(col + param(0, 1), cols_)));
      break;
    case 'm':
      selectGraphics();
      break;
    case 'r': {
      const uint16_t top = static_cast<uint16_t>(param(0, 1) - 1);
      const uint16_t bottom = static_cast<uint16_t>(std::min<uint16_t>(param(1, rows_), rows_) - 1);
      if (top < bottom) {
        regionTop_ = top;
        regionBottom_ = bottom;
      } else {
        regionTop_ = 0;
        regionBottom_ = static_cast<uint16_t>(rows_ - 1);
      }
      moveTo(0, 0);
      break;
    }
    case 's':
      savedRow_ = curRow_;
      savedCol_ = curCol_;
      break;
    case 'u':
      moveTo(savedRow_, savedCol_);
      break;
    case 'h':
      setMode(true);
      break;
    case 'l':
      setMode(false);
      break;
    case 'n':
      if (param(0, 0) == 6) {
        char buf[24];
        snprintf(buf, sizeof(buf), "\x1b[%u;%uR", static_cast<unsigned>(curRow_ + 1), static_cast<unsigned>(col + 1));
        reply_ += buf;
      } else if (param(0, 0) == 5) {
        reply_ += "\x1b[0n";
      }
      break;
    case 'c':
      if (param(0, 0) == 0) reply_ += "\x1b[?6c";  // "I am a VT102"
      break;
    default:
      break;
  }
}

// ---- the redraw policy ----

void RedrawPolicy::note(const Terminal::Changes& changes) {
  if (!changes.any()) return;
  pending_ = true;
  linesSinceFull_ += changes.scrolledLines;
  if (changes.cleared || linesSinceFull_ >= fullAfterLines_) wantFull_ = true;
}

RedrawPolicy::Action RedrawPolicy::poll(const uint32_t nowMs) {
  if (!pending_) return Action::None;
  if (drawnOnce_ && static_cast<uint32_t>(nowMs - lastDrawMs_) < minIntervalMs_) return Action::None;
  drawnOnce_ = true;
  lastDrawMs_ = nowMs;
  pending_ = false;
  if (wantFull_) {
    wantFull_ = false;
    linesSinceFull_ = 0;
    return Action::Full;
  }
  return Action::Fast;
}

}  // namespace sshterm
