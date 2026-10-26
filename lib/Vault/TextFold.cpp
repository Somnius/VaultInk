#include "TextFold.h"

#include <algorithm>

#include "FoldTable.h"

namespace textfold {
namespace {

// Minimal UTF-8 decode/encode, so this library has no other dependency.
uint32_t next(std::string_view s, size_t& i) {
  const auto b = static_cast<unsigned char>(s[i]);
  int len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
  if (i + static_cast<size_t>(len) > s.size()) len = 1;
  uint32_t cp = len == 1 ? b : len == 2 ? (b & 0x1F) : len == 3 ? (b & 0x0F) : (b & 0x07);
  for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
  i += static_cast<size_t>(len);
  return cp;
}

void append(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

bool isWordCodepoint(const uint32_t cp) {
  if (cp < 0x80) return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
  if (cp < 0xC0) return false;                     // Latin-1 punctuation and symbols
  if (cp == 0xD7 || cp == 0xF7) return false;      // × ÷
  if (cp == 0x037E || cp == 0x0387) return false;  // Greek question mark, ano teleia
  if (cp >= 0x0300 && cp <= 0x036F) return true;   // combining marks stay inside a word
  if (cp >= 0x2000 && cp <= 0x2BFF) return false;  // punctuation, symbols, arrows
  if (cp >= 0x3000 && cp <= 0x303F) return false;  // CJK punctuation
  return cp != 0xFEFF;
}

}  // namespace

uint32_t foldCodepoint(const uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + ('a' - 'A');
  if (cp < 0xC0 || cp > 0xFFFF) return cp;
  const auto* begin = std::begin(foldtable::PAIRS);
  const auto* end = std::end(foldtable::PAIRS);
  const auto* it = std::lower_bound(begin, end, cp, [](const foldtable::Pair& p, uint32_t v) { return p.from < v; });
  return it != end && it->from == cp ? it->to : cp;
}

std::string fold(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const uint32_t cp = next(text, i);
    if (cp >= 0x0300 && cp <= 0x036F) continue;  // drop separate (combining) accents
    append(out, foldCodepoint(cp));
  }
  return out;
}

std::vector<std::string> words(std::string_view text, const size_t minLength) {
  std::vector<std::string> out;
  std::string word;
  size_t length = 0;
  auto flush = [&]() {
    if (length >= minLength) out.push_back(word);
    word.clear();
    length = 0;
  };
  for (size_t i = 0; i < text.size();) {
    const uint32_t cp = next(text, i);
    if (!isWordCodepoint(cp)) {
      flush();
      continue;
    }
    if (cp >= 0x0300 && cp <= 0x036F) continue;
    append(word, foldCodepoint(cp));
    ++length;
  }
  flush();
  return out;
}

}  // namespace textfold
