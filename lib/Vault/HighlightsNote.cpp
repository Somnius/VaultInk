#include "HighlightsNote.h"

namespace highlightsnote {
namespace {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
  return s.substr(a, b - a);
}

}  // namespace

std::string build(const std::string& title, const std::string& author, const std::vector<Highlight>& highlights) {
  std::string out = "# " + trim(title) + "\n";
  if (!trim(author).empty()) out += "*" + trim(author) + "*\n";
  out += std::string(MARKER) + "\n";
  std::string chapter;
  bool first = true;
  for (const Highlight& h : highlights) {
    const std::string text = trim(h.text);
    if (text.empty()) continue;
    if (first || h.chapter != chapter) {
      chapter = h.chapter;
      if (!trim(chapter).empty()) out += "\n## " + trim(chapter) + "\n";
    }
    first = false;
    out += "\n";
    size_t start = 0;
    bool blankBefore = false;
    while (start <= text.size()) {
      size_t end = text.find('\n', start);
      if (end == std::string::npos) end = text.size();
      std::string line = text.substr(start, end - start);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const bool blank = trim(line).empty();
      if (!(blank && blankBefore)) out += blank ? ">\n" : "> " + line + "\n";
      blankBefore = blank;
      start = end + 1;
    }
  }
  return out;
}

bool isExport(const std::string& existing) {
  if (existing.compare(0, 2, "# ") != 0) return false;
  const size_t firstLineEnd = existing.find('\n');
  if (firstLineEnd == std::string::npos) return false;
  // The marker is the next line, or the one after it when an author line is there.
  const size_t second = firstLineEnd + 1;
  const size_t secondEnd = existing.find('\n', second);
  if (existing.compare(second, std::string(MARKER).size(), MARKER) == 0) return true;
  if (secondEnd == std::string::npos) return false;
  return existing.compare(secondEnd + 1, std::string(MARKER).size(), MARKER) == 0;
}

std::string fileNameFor(const std::string& title) {
  std::string out;
  for (const char c : title) {
    const bool forbidden = c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                           c == '>' || c == '|' || static_cast<unsigned char>(c) < 32;
    out += forbidden ? '_' : c;
  }
  size_t lead = 0;
  while (lead < out.size() && (out[lead] == '.' || out[lead] == ' ')) ++lead;
  out.erase(0, lead);
  if (out.size() > 100) {
    size_t cut = 100;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
    out.resize(cut);
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  return out.empty() ? "Highlights" : out;
}

}  // namespace highlightsnote
