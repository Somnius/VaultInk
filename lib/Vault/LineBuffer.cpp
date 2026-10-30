#include "LineBuffer.h"

#include <utility>

const std::string LineBuffer::LF = "\n";
const std::string LineBuffer::CRLF = "\r\n";

bool LineBuffer::load(std::string_view bytes) {
  lines.clear();
  history.clear();
  dirty = false;
  historyLost = false;
  last = 0;
  bom = bytes.substr(0, 3) == "\xEF\xBB\xBF";
  if (bom) bytes.remove_prefix(3);
  size_t crlfCount = 0;
  size_t lfCount = 0;
  bool editable = true;
  size_t start = 0;
  while (start < bytes.size()) {
    const size_t nl = bytes.find('\n', start);
    Line l;
    if (nl == std::string_view::npos) {
      l.text.assign(bytes.substr(start));
      start = bytes.size();
    } else if (nl > start && bytes[nl - 1] == '\r') {
      l.text.assign(bytes.substr(start, nl - 1 - start));
      l.ending = CRLF;
      ++crlfCount;
      start = nl + 1;
    } else {
      l.text.assign(bytes.substr(start, nl - start));
      l.ending = LF;
      ++lfCount;
      start = nl + 1;
    }
    if (l.text.size() > MAX_LINE_BYTES) editable = false;
    lines.push_back(std::move(l));
  }
  crlf = crlfCount > lfCount;
  return editable;
}

std::string LineBuffer::serialize() const {
  std::string out;
  if (bom) out = "\xEF\xBB\xBF";
  for (const auto& l : lines) {
    out += l.text;
    out += l.ending;
  }
  return out;
}

void LineBuffer::record(Edit edit) {
  dirty = true;
  last = edit.index;
  if (history.size() == MAX_UNDO) {
    history.erase(history.begin());
    historyLost = true;
  }
  history.push_back(std::move(edit));
}

bool LineBuffer::replace(const size_t i, std::string text) {
  if (i >= lines.size() || text.size() > MAX_LINE_BYTES || text.find('\n') != std::string::npos) return false;
  if (lines[i].text == text) return true;
  Line before = lines[i];
  lines[i].text = std::move(text);
  record({Op::Replace, i, std::move(before), {}});
  return true;
}

bool LineBuffer::insert(const size_t i, std::string text) {
  if (i > lines.size() || text.size() > MAX_LINE_BYTES || text.find('\n') != std::string::npos) return false;
  // Appending after a last line that had no ending: that line gets one now, and the new last
  // line keeps the note without a final newline.
  if (i == lines.size() && !lines.empty() && lines.back().ending.empty()) {
    lines.back().ending = newEnding();
    lines.push_back({std::move(text), ""});
  } else {
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i), Line{std::move(text), newEnding()});
  }
  record({Op::Insert, i, {}, {}});
  return true;
}

bool LineBuffer::remove(const size_t i) {
  if (i >= lines.size()) return false;
  Line before = lines[i];
  const bool wasLast = i + 1 == lines.size();
  lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
  // The new last line takes over the removed one's (missing) ending.
  std::string prevEnding;
  if (wasLast && !lines.empty() && before.ending.empty()) {
    prevEnding = lines.back().ending;
    lines.back().ending.clear();
  }
  record({Op::Remove, i, std::move(before), std::move(prevEnding)});
  return true;
}

bool LineBuffer::moveUp(const size_t i) {
  if (i == 0 || i >= lines.size()) return false;
  std::swap(lines[i - 1].text, lines[i].text);
  record({Op::Swap, i - 1, {}, {}});
  last = i - 1;
  return true;
}

bool LineBuffer::moveDown(const size_t i) {
  if (i + 1 >= lines.size()) return false;
  std::swap(lines[i].text, lines[i + 1].text);
  record({Op::Swap, i, {}, {}});
  last = i + 1;
  return true;
}

bool LineBuffer::join(const size_t i) {
  if (i + 1 >= lines.size()) return false;
  const std::string& a = lines[i].text;
  const std::string& b = lines[i + 1].text;
  const bool space = !a.empty() && !b.empty();
  if (a.size() + b.size() + (space ? 1 : 0) > MAX_LINE_BYTES) return false;
  Edit edit{Op::Join, i, lines[i + 1], lines[i].ending, a};
  lines[i].text = a + (space ? " " : "") + b;
  lines[i].ending = lines[i + 1].ending;  // the last line's missing ending moves up with it
  lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i + 1));
  record(std::move(edit));
  return true;
}

bool LineBuffer::undo() {
  if (history.empty()) return false;
  Edit e = std::move(history.back());
  history.pop_back();
  switch (e.op) {
    case Op::Replace:
      lines[e.index] = std::move(e.before);
      break;
    case Op::Insert: {
      const bool appendedAfterBare = e.index + 1 == lines.size() && lines.back().ending.empty() && e.index > 0;
      lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(e.index));
      if (appendedAfterBare) lines.back().ending.clear();
      break;
    }
    case Op::Remove: {
      const bool wasLast = e.index == lines.size();
      if (wasLast && !lines.empty() && e.before.ending.empty()) lines.back().ending = e.prevEnding;
      lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(e.index), std::move(e.before));
      break;
    }
    case Op::Swap:
      std::swap(lines[e.index].text, lines[e.index + 1].text);
      break;
    case Op::Join:
      lines[e.index].text = std::move(e.prevText);
      lines[e.index].ending = std::move(e.prevEnding);
      lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(e.index + 1), std::move(e.before));
      break;
  }
  last = e.index < lines.size() ? e.index : (lines.empty() ? 0 : lines.size() - 1);
  dirty = !history.empty() || historyLost;
  return true;
}
