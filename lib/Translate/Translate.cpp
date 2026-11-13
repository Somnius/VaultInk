#include "Translate.h"

#include <algorithm>
#include <iterator>

namespace translate {

namespace {

constexpr size_t MAX_KEY_BYTES = 32;
constexpr size_t MAX_STATUS_BYTES = 8;
constexpr size_t MAX_DETAIL_BYTES = 256;
constexpr size_t MAX_PRIMITIVE_BYTES = 16;
constexpr std::string_view SOFT_HYPHEN = "\xC2\xAD";
constexpr std::string_view NO_BREAK_SPACE = "\xC2\xA0";

bool isAsciiLetter(const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAsciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// "el-GR", "EL" and "el_GR" give "el"; the three-letter codes books also use give their two-letter
// one. Empty when it is not a language code.
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
  std::transform(code.begin(), code.end(), code.begin(), lower);
  if (len == 3) {
    struct Map {
      const char* three;
      const char* two;
    };
    static constexpr Map MAP[] = {{"eng", "en"}, {"ell", "el"}, {"gre", "el"}, {"fra", "fr"}, {"fre", "fr"},
                                  {"deu", "de"}, {"ger", "de"}, {"spa", "es"}, {"ita", "it"}, {"por", "pt"},
                                  {"rus", "ru"}, {"nld", "nl"}, {"dut", "nl"}, {"pol", "pl"}, {"tur", "tr"},
                                  {"swe", "sv"}, {"nor", "no"}, {"dan", "da"}, {"fin", "fi"}, {"ces", "cs"},
                                  {"cze", "cs"}, {"ron", "ro"}, {"rum", "ro"}, {"hun", "hu"}, {"bul", "bg"}};
    const Map* const found =
        std::find_if(std::begin(MAP), std::end(MAP), [&code](const Map& m) { return code == m.three; });
    // Any other three letters (und, mul, zxx, or a language with no two-letter code) is not sent.
    return found != std::end(MAP) ? found->two : std::string{};
  }
  return code;
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

void appendUtf8(std::string& out, const uint32_t cp) {
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

// MyMemory's memory holds text as it was typed into web pages: "&#39;", "&quot;", "&#1603;". The
// five named ones and the numeric ones become characters; anything else stays as it is.
std::string decodeEntities(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] != '&') {
      out += in[i];
      continue;
    }
    const size_t semi = in.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 9) {
      out += '&';
      continue;
    }
    const std::string_view name = std::string_view(in).substr(i + 1, semi - i - 1);
    uint32_t cp = 0;
    if (name == "amp") {
      cp = '&';
    } else if (name == "lt") {
      cp = '<';
    } else if (name == "gt") {
      cp = '>';
    } else if (name == "quot") {
      cp = '"';
    } else if (name == "apos") {
      cp = '\'';
    } else if (name.size() >= 2 && name[0] == '#') {
      const bool hex = name[1] == 'x' || name[1] == 'X';
      bool valid = name.size() > (hex ? 2u : 1u);
      for (size_t k = hex ? 2 : 1; k < name.size() && valid; k++) {
        const int v = hexValue(name[k]);
        if (v < 0 || (!hex && v > 9)) {
          valid = false;
        } else {
          cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(v);
        }
      }
      if (!valid || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0;
    }
    if (cp == 0) {
      out += '&';
      continue;
    }
    // A control character would only break the layout: a space instead.
    appendUtf8(out, cp < 0x20 ? ' ' : cp);
    i = semi;
  }
  return out;
}

bool containsNoCase(const std::string_view haystack, const std::string_view needle) {
  if (needle.empty() || haystack.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= haystack.size(); i++) {
    size_t k = 0;
    while (k < needle.size() && lower(haystack[i + k]) == lower(needle[k])) k++;
    if (k == needle.size()) return true;
  }
  return false;
}

int toInt(const std::string& digits) {
  int value = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9' || value > 99999) return 0;
    value = value * 10 + (c - '0');
  }
  return value;
}

void appendJsonString(std::string& out, const std::string_view text) {
  static constexpr char HEX[] = "0123456789abcdef";
  out += '"';
  for (const char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (u < 0x20 || u == 0x7F) {
      out += "\\u00";
      out += HEX[u >> 4];
      out += HEX[u & 0x0F];
    } else {
      out += c;  // UTF-8 goes through as it is
    }
  }
  out += '"';
}

}  // namespace

Pair languagePair(const std::string_view bookLanguage, const std::string_view uiLanguage) {
  Pair pair;
  pair.source = languageCode(bookLanguage);
  pair.target = languageCode(uiLanguage);
  if (pair.target.empty()) pair.target = "en";
  if (pair.source == pair.target) pair.target = pair.source == "en" ? "" : "en";
  return pair;
}

std::string cleanQuery(const std::string_view text) {
  std::string out;
  bool pendingSpace = false;
  for (size_t i = 0; i < text.size(); i++) {
    if (text.substr(i, SOFT_HYPHEN.size()) == SOFT_HYPHEN) {  // a hyphenation hint, not a letter
      i += SOFT_HYPHEN.size() - 1;
      continue;
    }
    if (text.substr(i, NO_BREAK_SPACE.size()) == NO_BREAK_SPACE) {
      i += NO_BREAK_SPACE.size() - 1;
      pendingSpace = true;
      continue;
    }
    const char c = text[i];
    if (isAsciiSpace(c)) {
      pendingSpace = true;
      continue;
    }
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) continue;
    if (pendingSpace && !out.empty()) out += ' ';
    pendingSpace = false;
    out += c;
    if (out.size() > MAX_QUERY_BYTES) return {};
  }
  return out;
}

std::string percentEncode(const std::string_view text) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (const char c : text) {
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

std::string myMemoryUrl(const std::string_view text, const Pair& pair) {
  const std::string query = cleanQuery(text);
  if (query.empty() || pair.source.empty() || pair.target.empty()) return {};
  std::string url = "https://";
  url += MYMEMORY_HOST;
  url += "/get?q=";
  url += percentEncode(query);
  url += "&langpair=";
  url += percentEncode(pair.source);
  url += "%7C";  // "|"
  url += percentEncode(pair.target);
  return url;
}

std::string libreTranslateUrl(std::string_view server) {
  while (!server.empty() && isAsciiSpace(server.front())) server.remove_prefix(1);
  while (!server.empty() && (isAsciiSpace(server.back()) || server.back() == '/')) server.remove_suffix(1);
  constexpr std::string_view HTTP = "http://";
  constexpr std::string_view HTTPS = "https://";
  std::string scheme(server.substr(0, std::min(server.size(), HTTPS.size())));
  std::transform(scheme.begin(), scheme.end(), scheme.begin(), lower);
  const size_t schemeLen = scheme == HTTPS ? HTTPS.size() : scheme.compare(0, HTTP.size(), HTTP) == 0 ? HTTP.size() : 0;
  if (schemeLen == 0 || server.size() <= schemeLen) return {};
  // A question mark or a fragment is not a server address.
  if (server.find_first_of("?# \t\r\n") != std::string_view::npos) return {};
  constexpr std::string_view PATH = "/translate";
  std::string url(server);
  if (server.size() < PATH.size() || server.substr(server.size() - PATH.size()) != PATH ||
      server.size() - PATH.size() <= schemeLen) {
    url += PATH;
  }
  return url;
}

std::string libreTranslateBody(const std::string_view text, const Pair& pair, const std::string_view apiKey) {
  const std::string query = cleanQuery(text);
  if (query.empty() || pair.target.empty()) return {};
  std::string body = "{\"q\":";
  appendJsonString(body, query);
  body += ",\"source\":";
  appendJsonString(body, pair.source.empty() ? std::string_view("auto") : std::string_view(pair.source));
  body += ",\"target\":";
  appendJsonString(body, pair.target);
  body += ",\"format\":\"text\"";
  if (!apiKey.empty()) {
    body += ",\"api_key\":";
    appendJsonString(body, apiKey);
  }
  body += '}';
  return body;
}

bool AnswerParser::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (taken_ >= MAX_BODY_BYTES) return false;
    taken_++;
    onChar(data[i]);
  }
  return taken_ < MAX_BODY_BYTES;
}

AnswerParser::Field AnswerParser::wanted() const {
  if (depth_ == 1 && isObject_[1]) {
    if (key_ == "translatedText") return haveText_ ? Field::None : Field::Text;
    if (key_ == "responseStatus") return status_.empty() ? Field::StatusCode : Field::None;
    if (key_ == "responseDetails") return details_.empty() ? Field::Details : Field::None;
    if (key_ == "quotaFinished") return Field::Quota;
    if (key_ == "error") return error_.empty() ? Field::Error : Field::None;
  } else if (depth_ == 2 && isObject_[2] && inData_ && key_ == "translatedText") {
    return haveText_ ? Field::None : Field::Text;
  }
  return Field::None;
}

std::string* AnswerParser::target(size_t& cap) {
  if (stringIsKey_) {
    cap = MAX_KEY_BYTES;
    return &key_;
  }
  switch (field_) {
    case Field::Text:
      cap = MAX_TEXT_BYTES;
      return &text_;
    case Field::StatusCode:
      cap = MAX_STATUS_BYTES;
      return &status_;
    case Field::Details:
      cap = MAX_DETAIL_BYTES;
      return &details_;
    case Field::Error:
      cap = MAX_DETAIL_BYTES;
      return &error_;
    case Field::Quota:
    case Field::None:
    default:
      return nullptr;
  }
}

void AnswerParser::onChar(const char c) {
  if (closed_) return;
  if (inString_) {
    onStringChar(c);
    return;
  }
  if (inPrimitive_) {
    if (c != ',' && c != '}' && c != ']' && !isAsciiSpace(c)) {
      if (primitive_.size() < MAX_PRIMITIVE_BYTES) primitive_ += c;
      return;
    }
    endPrimitive();  // and `c` is handled below
  }
  const bool tracked = depth_ >= 1 && depth_ <= 2;
  switch (c) {
    case '"':
      inString_ = true;
      stringIsKey_ = expectKey_ && tracked && isObject_[depth_];
      if (stringIsKey_) {
        key_.clear();
      } else {
        field_ = expectKey_ ? Field::None : wanted();
        if (field_ == Field::Text) haveText_ = true;
      }
      break;
    case '{':
    case '[': {
      const bool data = c == '{' && depth_ == 1 && isObject_[1] && !expectKey_ && key_ == "responseData";
      depth_++;
      if (depth_ <= 2) isObject_[depth_] = c == '{';
      if (depth_ == 2) inData_ = data;
      expectKey_ = c == '{' && depth_ <= 2;
      key_.clear();
      field_ = Field::None;  // the value is a container: only "responseData" is looked into
      break;
    }
    case '}':
    case ']':
      if (tracked) isObject_[depth_] = false;
      if (depth_ == 2) inData_ = false;
      if (--depth_ <= 0) closed_ = true;
      expectKey_ = false;
      key_.clear();
      field_ = Field::None;
      break;
    case ',':
      expectKey_ = tracked && isObject_[depth_];
      field_ = Field::None;
      break;
    case ':':
      break;
    default:
      if (isAsciiSpace(c)) break;
      // A number, true, false or null.
      inPrimitive_ = true;
      primitive_.assign(1, c);
      field_ = expectKey_ ? Field::None : wanted();
      break;
  }
}

void AnswerParser::endPrimitive() {
  inPrimitive_ = false;
  if (field_ == Field::StatusCode) {
    status_ = primitive_;
  } else if (field_ == Field::Quota) {
    quota_ = primitive_ == "true";
  }
  field_ = Field::None;
  key_.clear();
}

void AnswerParser::appendByte(const char c) {
  size_t cap = 0;
  std::string* out = target(cap);
  if (!out) return;
  if (out->size() < cap) {
    *out += c;
  } else if (out == &text_) {
    textCut_ = true;
  }
}

void AnswerParser::appendCodepoint(const uint32_t cp) {
  size_t cap = 0;
  std::string* out = target(cap);
  if (!out) return;
  if (out->size() + 4 > cap) {
    if (out == &text_) textCut_ = true;
    return;
  }
  appendUtf8(*out, cp);
}

void AnswerParser::onStringChar(const char c) {
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
      appendCodepoint(unicode_ < 0x20 ? ' ' : unicode_);
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
        appendByte('\n');
        return;
      case 't':
        appendByte(' ');
        return;
      case 'r':
      case 'b':
      case 'f':
        return;
      default:
        appendByte(c);  // \" \\ \/
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
  appendByte(c);  // raw bytes pass through: the body is UTF-8 already
}

void AnswerParser::endString() {
  inString_ = false;
  highSurrogate_ = 0;
  if (stringIsKey_) {
    stringIsKey_ = false;
    expectKey_ = false;
    return;
  }
  field_ = Field::None;
  key_.clear();
}

std::string AnswerParser::takeText(bool& truncated) {
  // The answer stopped (the size limit, or a dropped connection) inside the translation.
  const bool cut = textCut_ || (inString_ && !stringIsKey_ && field_ == Field::Text);
  std::string text = text_;
  if (cut) trimPartialUtf8(text);
  text = decodeEntities(text);
  while (!text.empty() && isAsciiSpace(text.back())) text.pop_back();
  size_t lead = 0;
  while (lead < text.size() && isAsciiSpace(text[lead])) lead++;
  text.erase(0, lead);
  truncated = cut && !text.empty();
  if (truncated) text += " \xE2\x80\xA6";
  return text;
}

Result AnswerParser::finishMyMemory(const int httpStatus) {
  Result r;
  r.text = takeText(r.truncated);
  // On an error MyMemory puts its message in both responseDetails and translatedText.
  r.detail = !details_.empty() ? details_ : std::string{};
  const int code = status_.empty() ? httpStatus : toInt(status_);
  const auto fail = [&r](const Status status) {
    if (r.detail.empty()) r.detail = r.text;
    r.text.clear();
    r.truncated = false;
    r.status = status;
    return r;
  };
  constexpr std::string_view WARNING = "MYMEMORY WARNING";
  if (httpStatus == 429 || code == 429 || quota_ || r.text.compare(0, WARNING.size(), WARNING) == 0 ||
      details_.compare(0, WARNING.size(), WARNING) == 0) {
    return fail(Status::Quota);
  }
  if (httpStatus >= 500 || code >= 500) return fail(Status::ServerError);
  if (httpStatus == 403 || code == 403) {
    // Seen on 2026-11-13, both with HTTP 200 and "responseStatus":"403": "'ZZ' IS AN INVALID TARGET
    // LANGUAGE . ..." and "PLEASE SELECT TWO DISTINCT LANGUAGES".
    return fail(containsNoCase(details_.empty() ? r.text : details_, "LANGUAGE") ? Status::BadLanguage
                                                                                 : Status::Refused);
  }
  if (httpStatus < 200 || httpStatus >= 300 || code != 200) return fail(Status::Invalid);
  if (!haveText_) return fail(Status::Invalid);
  if (r.text.empty()) return fail(Status::NoTranslation);
  r.status = Status::Ok;
  return r;
}

Result AnswerParser::finishLibreTranslate(const int httpStatus) {
  Result r;
  r.detail = error_;
  if (httpStatus == 429) {
    r.status = Status::Quota;
  } else if (httpStatus == 403) {
    r.status = Status::Refused;
  } else if (httpStatus >= 500) {
    r.status = Status::ServerError;
  } else if (httpStatus == 400) {
    // The documentation gives only "Invalid request" for 400. The two wordings looked for here are
    // from memory of the server's messages, not from the documentation: anything else is Invalid.
    r.status = containsNoCase(error_, "not supported") ? Status::BadLanguage
               : containsNoCase(error_, "api key")     ? Status::Refused
                                                       : Status::Invalid;
  } else if (httpStatus < 200 || httpStatus >= 300 || !haveText_) {
    r.status = Status::Invalid;
  } else {
    r.text = takeText(r.truncated);
    r.status = r.text.empty() ? Status::NoTranslation : Status::Ok;
  }
  return r;
}

}  // namespace translate
