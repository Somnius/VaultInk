#include "LinkRewrite.h"

namespace linkrewrite {
namespace {

bool startsFence(std::string_view line) {
  size_t i = 0;
  while (i < line.size() && i < 3 && line[i] == ' ') ++i;  // up to three spaces of indent
  return line.substr(i, 3) == "```" || line.substr(i, 3) == "~~~";
}

bool endsWithMd(std::string_view name) {
  if (name.size() < 3) return false;
  const std::string_view tail = name.substr(name.size() - 3);
  return tail[0] == '.' && (tail[1] == 'm' || tail[1] == 'M') && (tail[2] == 'd' || tail[2] == 'D');
}

// One line outside a fence: rewrites its links, skipping `code` spans. Returns links changed.
size_t rewriteLine(std::string& line, const std::function<bool(std::string_view)>& points, std::string_view newName) {
  size_t changed = 0;
  size_t i = 0;
  while (i < line.size()) {
    if (line[i] == '`') {
      // A code span ends at the next run of the same number of backticks.
      size_t run = 0;
      while (i + run < line.size() && line[i + run] == '`') ++run;
      const size_t close = line.find(std::string(run, '`'), i + run);
      if (close == std::string::npos) break;  // unclosed: the rest of the line is literal text
      i = close + run;
      continue;
    }
    if (line.compare(i, 2, "[[") != 0) {
      ++i;
      continue;
    }
    const size_t start = i + 2;
    const size_t end = line.find("]]", start);
    if (end == std::string::npos) break;
    size_t targetEnd = start;
    while (targetEnd < end && line[targetEnd] != '|' && line[targetEnd] != '#') ++targetEnd;
    // Inside a table the alias bar is written "\|"; the backslash isn't part of the name.
    if (targetEnd < end && line[targetEnd] == '|' && targetEnd > start && line[targetEnd - 1] == '\\') --targetEnd;
    const std::string_view target = std::string_view(line).substr(start, targetEnd - start);
    if (!target.empty() && points(target)) {
      const size_t slash = target.find_last_of('/');
      const size_t nameStart = slash == std::string_view::npos ? 0 : slash + 1;
      std::string replacement(target.substr(0, nameStart));
      replacement += newName;
      if (endsWithMd(target)) replacement += ".md";
      line.replace(start, targetEnd - start, replacement);
      ++changed;
      i = line.find("]]", start) + 2;
    } else {
      i = end + 2;
    }
  }
  return changed;
}

}  // namespace

size_t rewrite(std::string& text, const std::function<bool(std::string_view target)>& points,
               const std::string_view newName) {
  size_t changed = 0;
  bool inFence = false;
  std::string out;
  out.reserve(text.size() + 64);
  size_t at = 0;
  while (at < text.size()) {
    const size_t newline = text.find('\n', at);
    const size_t lineEnd = newline == std::string::npos ? text.size() : newline + 1;  // keeps the '\n'
    std::string line = text.substr(at, lineEnd - at);
    if (startsFence(line)) {
      inFence = !inFence;
    } else if (!inFence) {
      changed += rewriteLine(line, points, newName);
    }
    out += line;
    at = lineEnd;
  }
  if (changed > 0) text.swap(out);
  return changed;
}

}  // namespace linkrewrite
