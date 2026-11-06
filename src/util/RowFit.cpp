#include "RowFit.h"

#include <GfxRenderer.h>

namespace {
constexpr const char* ELLIPSIS = "\xe2\x80\xa6";

// Whether the text wraps into at most maxLines without the list having to cut it.
bool fits(GfxRenderer& renderer, const int fontId, const int width, const int maxLines, const std::string& text) {
  if (renderer.getTextWidth(fontId, text.c_str()) <= width) return true;
  const auto lines = renderer.wrappedText(fontId, text.c_str(), width, maxLines);
  if (static_cast<int>(lines.size()) > maxLines) return false;
  const std::string& last = lines.empty() ? std::string() : lines.back();
  return last.size() < 3 || last.compare(last.size() - 3, 3, ELLIPSIS) != 0;
}

// Byte length of the longest prefix of `name` that is still a whole character.
size_t boundary(const std::string& name, size_t at) {
  while (at > 0 && at < name.size() && (static_cast<unsigned char>(name[at]) & 0xC0) == 0x80) --at;
  return at;
}
}  // namespace

namespace rowfit {

std::string nameAndType(GfxRenderer& renderer, const int fontId, const int width, const int maxLines,
                        const std::string& star, const std::string& name, const std::string& ext) {
  const std::string plain = star + name;
  if (ext.empty()) return plain;
  const std::string whole = plain + " " + ext;
  if (fits(renderer, fontId, width, maxLines, whole)) return whole;
  if (fits(renderer, fontId, width, maxLines, plain)) return plain;  // the name fills both lines on its own
  // Cut the name where `name..type` still fits: the longest prefix, found by halving.
  size_t lo = 0;
  size_t hi = name.size();
  while (lo < hi) {
    const size_t mid = boundary(name, lo + (hi - lo + 1) / 2);
    if (mid <= lo) break;
    std::string head = name.substr(0, mid);
    while (!head.empty() && (head.back() == '.' || head.back() == ' ')) head.pop_back();
    if (fits(renderer, fontId, width, maxLines, star + head + ".." + ext)) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  std::string head = name.substr(0, boundary(name, lo));
  while (!head.empty() && (head.back() == '.' || head.back() == ' ')) head.pop_back();
  return star + head + ".." + ext;
}

}  // namespace rowfit
