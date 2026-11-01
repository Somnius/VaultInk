#include "WordSearch.h"

#include <Utf8.h>

#include "TextFold.h"

namespace wordsearch {

std::string prepareQuery(const std::string_view query) {
  const std::string folded = textfold::fold(query);
  std::string out;
  bool pendingSpace = false;
  for (const char c : folded) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      pendingSpace = !out.empty();
    } else {
      if (pendingSpace) out += ' ';
      pendingSpace = false;
      out += c;
    }
  }
  return out;
}

// Neither the character before `at` nor the one after the match is part of a word.
static bool atWordEdges(const std::string& text, const size_t at, const size_t len) {
  if (at > 0) {
    size_t i = at - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) i--;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data()) + i;
    if (isWordChar(utf8NextCodepoint(&p))) return false;
  }
  if (at + len < text.size()) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text.data()) + at + len;
    if (isWordChar(utf8NextCodepoint(&p))) return false;
  }
  return true;
}

std::vector<size_t> find(const std::vector<std::string>& words, const std::string& preparedQuery, const size_t maxHits,
                         const bool exactWords) {
  std::vector<size_t> hits;
  if (preparedQuery.empty() || words.empty()) return hits;
  // The folded page text, and where each word starts in it.
  std::string text;
  std::vector<size_t> starts;
  starts.reserve(words.size());
  for (const auto& w : words) {
    if (!text.empty()) text += ' ';
    starts.push_back(text.size());
    text += textfold::fold(w);
  }
  size_t from = 0;
  while (hits.size() < maxHits) {
    const size_t at = text.find(preparedQuery, from);
    if (at == std::string::npos) break;
    if (exactWords && !atWordEdges(text, at, preparedQuery.size())) {
      from = at + 1;
      continue;
    }
    // The word the match starts in (the last start at or before it).
    size_t lo = 0, hi = starts.size();
    while (lo + 1 < hi) {
      const size_t mid = (lo + hi) / 2;
      if (starts[mid] <= at) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    hits.push_back(lo);
    from = at + preparedQuery.size();
  }
  return hits;
}

std::string snippet(const std::vector<std::string>& words, const size_t at, const size_t before, const size_t after) {
  if (at >= words.size()) return "";
  const size_t first = at > before ? at - before : 0;
  const size_t last = at + after < words.size() ? at + after : words.size() - 1;
  std::string out;
  if (first > 0) out = "... ";
  for (size_t i = first; i <= last; ++i) {
    if (i > first) out += ' ';
    out += words[i];
  }
  if (last + 1 < words.size()) out += " ...";
  return out;
}

}  // namespace wordsearch
