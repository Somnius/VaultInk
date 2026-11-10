#include "Wikipedia.h"

namespace wikipedia {

namespace {

constexpr size_t MAX_KEY_BYTES = 32;
constexpr size_t MAX_TYPE_BYTES = 32;
constexpr size_t MAX_SHOWN_TITLE_BYTES = 256;

bool isAsciiLetter(const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAsciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// "el-GR" -> "el"; empty when it is not a 2-3 letter code.
std::string languageCode(const std::string_view tag) {
  size_t start = 0;
  while (start < tag.size() && isAsciiSpace(tag[start])) start++;
  size_t end = start;
  while (end < tag.size() && isAsciiLetter(tag[end])) end++;
  const size_t len = end - start;
  if (len < 2 || len > 3) return {};
  while (end < tag.size() && isAsciiSpace(tag[end])) end++;
  if (end < tag.size() && tag[end] != '-' && tag[end] != '_') return {};
  std::string code(tag.substr(start, len));
  for (char& c : code) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  if (code == "nb") code = "no";  // Norwegian Bokmål lives at no.wikipedia.org
  return code;
}

// Things that sit around a selected word and are not part of a title. The multi-byte ones are
// the quotes, dashes and the ellipsis that books use.
constexpr std::string_view EDGE_JUNK[] = {
    "\xC2\xAB",     "\xC2\xBB",     "\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C", "\xE2\x80\x9D",
    "\xE2\x80\x9E", "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\xA6", "\xC2\xA0",     "\xCE\x87",
};
constexpr std::string_view SOFT_HYPHEN = "\xC2\xAD";

bool isEdgePunct(const char c) {
  switch (c) {
    case '.':
    case ',':
    case ';':
    case ':':
    case '!':
    case '?':
    case '"':
    case '\'':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '<':
    case '>':
    case '-':
    case '_':
    case '*':
    case '/':
    case '\\':
    case '|':
      return true;
    default:
      return isAsciiSpace(c);
  }
}

std::string_view trimEdges(std::string_view text) {
  for (bool again = true; again;) {
    again = false;
    while (!text.empty() && isEdgePunct(text.front())) text.remove_prefix(1);
    while (!text.empty() && isEdgePunct(text.back())) {
      // "Mercury (planet)" keeps its bracket; "(word)" lost the opening one just above.
      if (text.back() == ')' && text.find('(') != std::string_view::npos) break;
      text.remove_suffix(1);
    }
    for (const auto junk : EDGE_JUNK) {
      if (text.substr(0, junk.size()) == junk) {
        text.remove_prefix(junk.size());
        again = true;
      }
      if (text.size() >= junk.size() && text.substr(text.size() - junk.size()) == junk) {
        text.remove_suffix(junk.size());
        again = true;
      }
    }
  }
  return text;
}

bool isUnreserved(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
         c == '_' || c == '~';
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Drops a multi-byte character the cut left unfinished at the end.
void trimPartialUtf8(std::string& s) {
  size_t i = s.size();
  int back = 0;
  while (i > 0 && back < 4 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) {
    i--;
    back++;
  }
  if (i == 0) {
    s.clear();
    return;
  }
  const unsigned char lead = static_cast<unsigned char>(s[i - 1]);
  const size_t need = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
  if (need == 0 || s.size() - (i - 1) != need) s.erase(i - 1);
}

}  // namespace

std::string languageFor(const std::string_view bookLanguage, const std::string_view uiLanguage) {
  std::string code = languageCode(bookLanguage);
  if (code.empty()) code = languageCode(uiLanguage);
  return code.empty() ? "en" : code;
}

std::string encodeTitle(const std::string_view text) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  const std::string_view title = trimEdges(text);
  std::string out;
  size_t rawBytes = 0;
  bool pendingSpace = false;
  for (size_t i = 0; i < title.size(); i++) {
    if (title.substr(i, SOFT_HYPHEN.size()) == SOFT_HYPHEN) {  // a hyphenation hint, not a letter
      i += SOFT_HYPHEN.size() - 1;
      continue;
    }
    const char c = title[i];
    if (isAsciiSpace(c)) {
      pendingSpace = true;
      continue;
    }
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) continue;
    if (pendingSpace) {
      out += '_';
      rawBytes++;
      pendingSpace = false;
    }
    if (++rawBytes > MAX_TITLE_BYTES) return {};
    if (isUnreserved(c)) {
      out += c;
    } else {
      out += '%';
      out += HEX[static_cast<unsigned char>(c) >> 4];
      out += HEX[static_cast<unsigned char>(c) & 0x0F];
    }
  }
  return out;
}

std::string summaryUrl(const std::string_view language, const std::string_view text) {
  const std::string title = encodeTitle(text);
  if (title.empty()) return {};
  std::string url = "https://";
  url += languageFor(language, {});
  url += ".wikipedia.org/api/rest_v1/page/summary/";
  url += title;
  return url;
}

bool SummaryParser::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (taken_ >= MAX_BODY_BYTES) return false;
    taken_++;
    onChar(data[i]);
  }
  return taken_ < MAX_BODY_BYTES;
}

std::string* SummaryParser::target() {
  switch (field_) {
    case Field::Type:
      return &type_;
    case Field::Title:
      return &title_;
    case Field::Extract:
      return &extract_;
    case Field::None:
    default:
      return nullptr;
  }
}

void SummaryParser::onChar(const char c) {
  if (closed_) return;
  if (inString_) {
    onStringChar(c);
    return;
  }
  switch (c) {
    case '"':
      inString_ = true;
      stringIsKey_ = depth_ == 1 && expectKey_;
      if (stringIsKey_) key_.clear();
      // A string that is not the value of a wanted top-level key goes nowhere.
      if (!stringIsKey_ && depth_ != 1) field_ = Field::None;
      break;
    case '{':
    case '[':
      depth_++;
      expectKey_ = depth_ == 1 && c == '{';
      field_ = Field::None;  // the value is a container: nothing in it is ours
      break;
    case '}':
    case ']':
      if (--depth_ <= 0) closed_ = true;
      break;
    case ',':
      if (depth_ == 1) {
        expectKey_ = true;
        field_ = Field::None;
      }
      break;
    default:
      break;  // numbers, true/false/null, colons and spaces
  }
}

void SummaryParser::appendCodepoint(const uint32_t cp) {
  std::string* out = stringIsKey_ ? &key_ : target();
  if (!out) return;
  const size_t cap = stringIsKey_             ? MAX_KEY_BYTES
                     : field_ == Field::Type  ? MAX_TYPE_BYTES
                     : field_ == Field::Title ? MAX_SHOWN_TITLE_BYTES
                                              : MAX_BODY_BYTES;
  if (out->size() + 4 > cap) return;
  if (cp < 0x80) {
    *out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    *out += static_cast<char>(0xC0 | (cp >> 6));
    *out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    *out += static_cast<char>(0xE0 | (cp >> 12));
    *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    *out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    *out += static_cast<char>(0xF0 | (cp >> 18));
    *out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    *out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    *out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

void SummaryParser::onStringChar(const char c) {
  if (unicodeLeft_ > 0) {  // inside \uXXXX
    const int v = hexValue(c);
    if (v < 0) {
      unicodeLeft_ = 0;
      highSurrogate_ = 0;
      return;
    }
    unicode_ = static_cast<uint16_t>((unicode_ << 4) | v);
    if (--unicodeLeft_ > 0) return;
    if (unicode_ >= 0xD800 && unicode_ <= 0xDBFF) {
      highSurrogate_ = unicode_;
    } else if (unicode_ >= 0xDC00 && unicode_ <= 0xDFFF) {
      if (highSurrogate_ != 0) appendCodepoint(0x10000 + ((highSurrogate_ - 0xD800) << 10) + (unicode_ - 0xDC00));
      highSurrogate_ = 0;
    } else {
      highSurrogate_ = 0;
      appendCodepoint(unicode_);
    }
    return;
  }
  if (escaped_) {
    escaped_ = false;
    switch (c) {
      case 'u':
        unicodeLeft_ = 4;
        unicode_ = 0;
        return;
      case 'n':
        appendCodepoint('\n');
        return;
      case 't':
        appendCodepoint(' ');
        return;
      case 'r':
      case 'b':
      case 'f':
        return;
      default:
        appendCodepoint(static_cast<unsigned char>(c));  // \" \\ \/
        return;
    }
  }
  if (c == '\\') {
    escaped_ = true;
    return;
  }
  if (c == '"') {
    endString();
    return;
  }
  // Raw bytes pass through as they are: the body is UTF-8 already.
  std::string* out = stringIsKey_ ? &key_ : target();
  if (!out) return;
  const size_t cap = stringIsKey_             ? MAX_KEY_BYTES
                     : field_ == Field::Type  ? MAX_TYPE_BYTES
                     : field_ == Field::Title ? MAX_SHOWN_TITLE_BYTES
                                              : MAX_BODY_BYTES;
  if (out->size() < cap) *out += c;
}

void SummaryParser::endString() {
  inString_ = false;
  highSurrogate_ = 0;
  if (!stringIsKey_) {
    field_ = Field::None;
    return;
  }
  stringIsKey_ = false;
  expectKey_ = false;
  // The first of each wins; a second "extract" key would not be a summary anyway.
  if (key_ == "type" && type_.empty()) {
    field_ = Field::Type;
  } else if (key_ == "title" && title_.empty()) {
    field_ = Field::Title;
  } else if (key_ == "extract" && extract_.empty()) {
    field_ = Field::Extract;
  } else {
    field_ = Field::None;
  }
}

Summary SummaryParser::finish(const int httpStatus) {
  Summary s;
  if (httpStatus == 404) {
    s.kind = Kind::NotFound;
    return s;
  }
  if (httpStatus < 200 || httpStatus >= 300) return s;
  trimPartialUtf8(title_);
  s.title = title_;
  if (type_ == "disambiguation") {
    s.kind = Kind::Disambiguation;
    return s;
  }
  // The answer stopped (the size limit, or a dropped connection) inside the extract: show what came.
  const bool cut = inString_ && !stringIsKey_ && field_ == Field::Extract;
  if (cut) trimPartialUtf8(extract_);
  while (!extract_.empty() && isAsciiSpace(extract_.back())) extract_.pop_back();
  if (extract_.empty()) return s;
  s.extract = extract_;
  if (cut) {
    s.extract += " \xE2\x80\xA6";
    s.truncated = true;
  }
  s.kind = Kind::Article;
  return s;
}

}  // namespace wikipedia
