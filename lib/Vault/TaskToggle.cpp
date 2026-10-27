#include "TaskToggle.h"

#include <cstdio>
#include <cstdlib>

namespace tasktoggle {

uint32_t lineHash(std::string_view line) {
  uint32_t h = 2166136261u;
  for (const char c : line) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

std::string href(const uint32_t line, const uint32_t hash) {
  char buf[32];
  snprintf(buf, sizeof(buf), "task:%u:%08x", static_cast<unsigned>(line), static_cast<unsigned>(hash));
  return buf;
}

bool parseHref(std::string_view href, uint32_t& line, uint32_t& hash) {
  if (href.substr(0, 5) != "task:") return false;
  const std::string rest(href.substr(5));
  char* end = nullptr;
  const unsigned long l = strtoul(rest.c_str(), &end, 10);
  if (end == rest.c_str() || *end != ':') return false;
  const char* hexStart = end + 1;
  const unsigned long h = strtoul(hexStart, &end, 16);
  if (end == hexStart || *end != '\0') return false;
  line = static_cast<uint32_t>(l);
  hash = static_cast<uint32_t>(h);
  return true;
}

size_t markOffset(std::string_view line) {
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i < line.size() && (line[i] == '-' || line[i] == '*' || line[i] == '+')) {
    ++i;
  } else {
    const size_t digits = i;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i == digits || i >= line.size() || (line[i] != '.' && line[i] != ')')) return std::string_view::npos;
    ++i;
  }
  // " [?]" after the marker, then the end of the line or a space, as in the converter.
  if (i + 3 >= line.size() || line[i] != ' ' || line[i + 1] != '[' || line[i + 3] != ']') {
    return std::string_view::npos;
  }
  if (i + 4 < line.size() && line[i + 4] != ' ') return std::string_view::npos;
  return i + 2;
}

Result toggle(std::string& text, const uint32_t line, const uint32_t hash) {
  // The converter never sees a UTF-8 byte order mark, so line 0 starts after it.
  size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
  for (uint32_t n = 0; n < line; ++n) {
    const size_t nl = text.find('\n', start);
    if (nl == std::string::npos) return Result::NotFound;
    start = nl + 1;
  }
  if (start > text.size()) return Result::NotFound;
  size_t end = text.find('\n', start);
  if (end == std::string::npos) end = text.size();
  size_t contentEnd = end;
  if (contentEnd > start && text[contentEnd - 1] == '\r') --contentEnd;
  const std::string_view current(text.data() + start, contentEnd - start);
  if (lineHash(current) != hash) return Result::Changed;
  const size_t mark = markOffset(current);
  if (mark == std::string_view::npos) return Result::NotATask;
  char& c = text[start + mark];
  c = c == ' ' ? 'x' : ' ';
  return Result::Ok;
}

}  // namespace tasktoggle
