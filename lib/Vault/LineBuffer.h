#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A note as editable lines (Phase 4b). Saving gives back the same bytes for every line not
// touched: each line keeps its own ending (LF, CRLF or none at the end), and a byte order mark
// stays. New lines take the ending most of the note uses. Undo keeps the last edits as small
// records, not copies of the note.
class LineBuffer {
 public:
  static constexpr size_t MAX_LINE_BYTES = 2000;  // longer lines can't go through the keyboard
  static constexpr size_t MAX_UNDO = 20;

  // False when the note can't be edited here (a line over MAX_LINE_BYTES); it is still loaded.
  bool load(std::string_view bytes);
  std::string serialize() const;

  size_t lineCount() const { return lines.size(); }
  const std::string& line(size_t i) const { return lines[i].text; }

  // Each returns false (and changes nothing) for an index out of range or a line too long.
  bool replace(size_t i, std::string text);
  bool insert(size_t i, std::string text);  // before line i; i == lineCount() appends
  bool remove(size_t i);
  bool moveUp(size_t i);    // swaps lines i-1 and i
  bool moveDown(size_t i);  // swaps lines i and i+1
  // Line i and the next one become a single line, joined by a space (none when either is empty).
  // False for the last line or a joined line over MAX_LINE_BYTES.
  bool join(size_t i);
  bool undo();
  size_t undoDepth() const { return history.size(); }
  bool changed() const { return dirty; }
  // The line the last change or undo was about, to show it again.
  size_t lastLine() const { return last; }

 private:
  struct Line {
    std::string text;
    std::string ending;  // "\n", "\r\n", or "" (last line without one)
  };
  enum class Op : uint8_t { Replace, Insert, Remove, Swap, Join };
  struct Edit {
    Op op;
    size_t index;
    Line before;             // Replace: the old line; Remove: the removed line
    std::string prevEnding;  // Remove of the last line: the ending the line before it had; Join: line i's ending
    std::string prevText;    // Join: line i's text before the join (before = the line that was merged in)
  };

  void record(Edit edit);
  const std::string& newEnding() const { return crlf ? CRLF : LF; }

  static const std::string LF;
  static const std::string CRLF;
  std::vector<Line> lines;
  std::vector<Edit> history;
  bool bom = false;
  bool crlf = false;
  bool dirty = false;
  bool historyLost = false;  // an edit fell off the undo list: undoing all no longer restores the file
  size_t last = 0;
};
