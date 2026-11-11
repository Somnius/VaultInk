#include "HighlightExport.h"

#include <algorithm>
#include <cstdio>

namespace highlightexport {

namespace {

constexpr char HEX_DIGITS[] = "0123456789abcdef";
constexpr char REPLACEMENT[] = "\xEF\xBF\xBD";  // U+FFFD

// Length of the UTF-8 sequence that starts at text[i], or 0 when it is not valid UTF-8
// (a stray continuation byte, a cut sequence, an overlong form, a surrogate, past U+10FFFF).
size_t utf8SequenceLength(const std::string_view text, const size_t i) {
  const auto byte = [&](const size_t at) { return static_cast<uint8_t>(text[at]); };
  const uint8_t lead = byte(i);
  if (lead < 0x80) return 1;
  size_t length = 0;
  uint32_t cp = 0;
  if ((lead & 0xE0) == 0xC0) {
    length = 2;
    cp = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    length = 3;
    cp = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    length = 4;
    cp = lead & 0x07;
  } else {
    return 0;
  }
  if (i + length > text.size()) return 0;
  for (size_t k = 1; k < length; k++) {
    if ((byte(i + k) & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (byte(i + k) & 0x3F);
  }
  static constexpr uint32_t MIN_FOR_LENGTH[] = {0, 0, 0x80, 0x800, 0x10000};
  if (cp < MIN_FOR_LENGTH[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return length;
}

void appendField(std::string& out, const char* name, const std::string_view value, const size_t maxChars) {
  out += ",\"";
  out += name;
  out += "\":";
  appendJsonString(out, value, maxChars);
}

// Line ends as "\n" only, and no other control characters: for Markdown.
std::string cleanLines(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); i++) {
    const char c = text[i];
    if (c == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') continue;
      out += '\n';
    } else if (c == '\n' || c == '\t' || static_cast<uint8_t>(c) >= 0x20) {
      if (c != 0x7F) out += c;
    }
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return out;
}

// One line of plain text: line ends and tabs become spaces.
std::string oneLine(const std::string_view text) {
  std::string out = cleanLines(text);
  std::replace_if(out.begin(), out.end(), [](const char c) { return c == '\n' || c == '\t'; }, ' ');
  return out;
}

// A Memos tag from a title or a name: letters and digits stay (every non-ASCII character too),
// anything else becomes one "_". At most 60 bytes, cut on a whole character. Empty when nothing is left.
std::string tagFrom(const std::string_view text) {
  constexpr size_t MAX_BYTES = 60;
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    const size_t length = utf8SequenceLength(text, i);
    if (length == 0) {
      i++;
      continue;
    }
    if (out.size() + length > MAX_BYTES) break;
    const char c = text[i];
    const bool keep = length > 1 || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (keep) {
      out.append(text.substr(i, length));
    } else if (!out.empty() && out.back() != '_') {
      out += '_';
    }
    i += length;
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  return out;
}

uint64_t fnv1a64(uint64_t hash, const std::string_view text) {
  for (const char c : text) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 0x100000001B3ULL;
  }
  return hash;
}
constexpr uint64_t FNV64_BASIS = 0xCBF29CE484222325ULL;

uint32_t fnv1a32(const uint8_t* data, const size_t size) {
  uint32_t hash = 0x811C9DC5U;
  for (size_t i = 0; i < size; i++) {
    hash ^= data[i];
    hash *= 0x01000193U;
  }
  return hash;
}

// The marker file: "VSNT", version 1, a zero byte, the count (u16), the keys (u64 each), then the
// FNV-1a 32 of everything before it. Little endian.
constexpr char MAGIC[4] = {'V', 'S', 'N', 'T'};
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_BYTES = 8;
constexpr size_t CHECK_BYTES = 4;

template <typename T>
void putLe(std::string& out, T value) {
  for (size_t i = 0; i < sizeof(T); i++) {
    out += static_cast<char>(value & 0xFF);
    value = static_cast<T>(value >> 8);
  }
}

template <typename T>
T getLe(const uint8_t* data) {
  T value = 0;
  for (size_t i = sizeof(T); i > 0; i--) value = static_cast<T>((value << 8) | data[i - 1]);
  return value;
}

}  // namespace

void appendJsonString(std::string& out, const std::string_view text, const size_t maxChars) {
  out += '"';
  size_t chars = 0;
  size_t i = 0;
  while (i < text.size() && chars < maxChars) {
    const size_t length = utf8SequenceLength(text, i);
    chars++;
    if (length == 0) {
      out += REPLACEMENT;
      i++;
      continue;
    }
    if (length > 1) {
      out.append(text.substr(i, length));
      i += length;
      continue;
    }
    const char c = text[i++];
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if (static_cast<uint8_t>(c) < 0x20 || c == 0x7F) {
          out += "\\u00";
          out += HEX_DIGITS[(c >> 4) & 0xF];
          out += HEX_DIGITS[c & 0xF];
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

std::string iso8601(const uint32_t epochSeconds) {
  const uint32_t days = epochSeconds / 86400;
  const uint32_t secs = epochSeconds % 86400;
  // Days to a civil date (Howard Hinnant's algorithm), for dates from 1970 on.
  const uint32_t z = days + 719468;
  const uint32_t era = z / 146097;
  const uint32_t doe = z - era * 146097;
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t month = mp < 10 ? mp + 3 : mp - 9;
  const uint32_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u+00:00", static_cast<unsigned>(year),
           static_cast<unsigned>(month), static_cast<unsigned>(day), static_cast<unsigned>(secs / 3600),
           static_cast<unsigned>(secs % 3600 / 60), static_cast<unsigned>(secs % 60));
  return buf;
}

bool ReadwiseBatch::add(const Highlight& highlight) {
  if (highlight.text.empty()) return true;
  if (count_ >= READWISE_MAX_BATCH || (count_ > 0 && items_.size() >= READWISE_SOFT_BODY_BYTES)) return false;
  if (count_ > 0) items_ += ',';
  items_ += "{\"text\":";
  appendJsonString(items_, highlight.text, READWISE_TEXT_MAX);
  // Without a title Readwise files the highlight under a book of its own naming, so an empty one is left out.
  if (!book_.title.empty()) appendField(items_, "title", book_.title, READWISE_TITLE_MAX);
  if (!book_.author.empty()) appendField(items_, "author", book_.author, READWISE_AUTHOR_MAX);
  items_ += ",\"source_type\":\"vaultink\",\"category\":\"books\"";
  if (!highlight.note.empty()) appendField(items_, "note", highlight.note, READWISE_NOTE_MAX);
  if (highlight.order != 0) {
    items_ += ",\"location\":";
    items_ += std::to_string(highlight.order);
    items_ += ",\"location_type\":\"order\"";
  }
  if (highlight.timestamp >= MIN_TRUSTED_EPOCH) {
    items_ += ",\"highlighted_at\":\"";
    items_ += iso8601(highlight.timestamp);
    items_ += '"';
  }
  if (!highlight.url.empty()) appendField(items_, "highlight_url", highlight.url, READWISE_URL_MAX);
  items_ += '}';
  count_++;
  return true;
}

std::string ReadwiseBatch::body() const {
  std::string out;
  out.reserve(items_.size() + 20);
  out += "{\"highlights\":[";
  out += items_;
  out += "]}";
  return out;
}

void ReadwiseBatch::reset() {
  items_.clear();
  count_ = 0;
}

size_t readwiseBatchCount(const size_t highlights) {
  return (highlights + READWISE_MAX_BATCH - 1) / READWISE_MAX_BATCH;
}

std::string memosContent(const Book& book, const Highlight& highlight) {
  const std::string quote = cleanLines(highlight.text);
  std::string out;
  out.reserve(quote.size() + 200);
  out += "> ";
  for (const char c : quote) {
    out += c;
    if (c == '\n') out += "> ";
  }
  // "> " before an empty line of the quote is fine in Markdown; drop the space so no line ends in one.
  for (size_t at = out.find("> \n"); at != std::string::npos; at = out.find("> \n", at)) out.erase(at + 1, 1);

  const std::string title = oneLine(book.title);
  const std::string author = oneLine(book.author);
  const std::string chapter = oneLine(highlight.chapter);
  std::string source = title;
  if (!author.empty()) source += source.empty() ? author : ", " + author;
  if (!chapter.empty()) source += source.empty() ? chapter : " (" + chapter + ")";
  if (!source.empty()) {
    out += "\n\n";
    out += source;
  }
  const std::string note = cleanLines(highlight.note);
  if (!note.empty()) {
    out += "\n\n";
    out += note;
  }
  out += "\n\n#highlights #vaultink";
  for (const std::string& tag : {tagFrom(title), tagFrom(author)}) {
    if (tag.empty()) continue;
    out += " #";
    out += tag;
  }
  return out;
}

std::string memosBody(const Book& book, const Highlight& highlight) {
  std::string out = "{\"content\":";
  appendJsonString(out, memosContent(book, highlight));
  out += ",\"visibility\":\"PRIVATE\"}";
  return out;
}

std::string memosUrl(std::string_view server) {
  while (!server.empty() && (server.front() == ' ' || server.front() == '\t')) server.remove_prefix(1);
  while (!server.empty() && (server.back() == ' ' || server.back() == '\t')) server.remove_suffix(1);
  std::string url;
  const size_t scheme = server.find("://");
  if (scheme == std::string_view::npos) {
    url = "https://";
  } else {
    std::string name(server.substr(0, scheme));
    std::transform(name.begin(), name.end(), name.begin(),
                   [](const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (name != "http" && name != "https") return {};
    url = name + "://";
    server.remove_prefix(scheme + 3);
  }
  while (!server.empty() && server.back() == '/') server.remove_suffix(1);
  if (server.empty() || server.front() == '/' || server.front() == ':') return {};
  // A space or a control character is a typo.
  if (std::any_of(server.begin(), server.end(),
                  [](const char c) { return static_cast<uint8_t>(c) <= 0x20 || c == 0x7F; })) {
    return {};
  }
  url.append(server);
  // Someone who pasted the API address already gets it once, not twice.
  constexpr std::string_view API = "/api/v1/memos";
  if (url.size() < API.size() || url.compare(url.size() - API.size(), API.size(), API) != 0) url.append(API);
  return url;
}

Outcome mapResponse(const int status, std::string_view retryAfter) {
  Outcome out;
  if (status <= 0) {
    out.result = Result::NotConnected;
  } else if (status >= 200 && status < 300) {
    out.result = Result::Ok;
  } else if (status == 401 || status == 403) {
    out.result = Result::BadToken;
  } else if (status == 404) {
    out.result = Result::NotFound;
  } else if (status == 429) {
    out.result = Result::RateLimited;
    while (!retryAfter.empty() && retryAfter.front() == ' ') retryAfter.remove_prefix(1);
    uint32_t seconds = 0;
    size_t digits = 0;
    while (digits < retryAfter.size() && retryAfter[digits] >= '0' && retryAfter[digits] <= '9' && digits < 6) {
      seconds = seconds * 10 + static_cast<uint32_t>(retryAfter[digits] - '0');
      digits++;
    }
    // Digits only (and spaces after them): "Wed, 21 Oct ..." and "12abc" are not a number of seconds.
    std::string_view rest = retryAfter.substr(digits);
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    out.retryAfterSeconds = digits > 0 && rest.empty() ? seconds : 0;
  } else if (status >= 300 && status < 500) {
    out.result = Result::Rejected;
  } else {
    out.result = Result::ServerError;
  }
  return out;
}

uint64_t sentKey(const std::string_view clippingId, const Destination destination) {
  uint64_t hash = fnv1a64(FNV64_BASIS, clippingId);
  const char tail[2] = {'\n', static_cast<char>('0' + static_cast<uint8_t>(destination))};
  hash = fnv1a64(hash, std::string_view(tail, sizeof(tail)));
  return hash == 0 ? 1 : hash;
}

bool SentMarkers::parse(const uint8_t* data, const size_t size) {
  keys_.clear();
  if (!data || size < HEADER_BYTES + CHECK_BYTES) return false;
  for (size_t i = 0; i < sizeof(MAGIC); i++) {
    if (data[i] != static_cast<uint8_t>(MAGIC[i])) return false;
  }
  if (data[4] != VERSION) return false;
  const size_t count = getLe<uint16_t>(data + 6);
  if (count > MAX_ENTRIES || size != HEADER_BYTES + count * 8 + CHECK_BYTES) return false;
  if (getLe<uint32_t>(data + size - CHECK_BYTES) != fnv1a32(data, size - CHECK_BYTES)) return false;
  keys_.reserve(count);
  for (size_t i = 0; i < count; i++) keys_.push_back(getLe<uint64_t>(data + HEADER_BYTES + i * 8));
  return true;
}

std::string SentMarkers::serialize() const {
  std::string out;
  out.reserve(HEADER_BYTES + keys_.size() * 8 + CHECK_BYTES);
  out.append(MAGIC, sizeof(MAGIC));
  out += static_cast<char>(VERSION);
  out += '\0';
  putLe<uint16_t>(out, static_cast<uint16_t>(keys_.size()));
  for (const uint64_t key : keys_) putLe<uint64_t>(out, key);
  putLe<uint32_t>(out, fnv1a32(reinterpret_cast<const uint8_t*>(out.data()), out.size()));
  return out;
}

bool SentMarkers::contains(const uint64_t key) const {
  return std::any_of(keys_.begin(), keys_.end(), [key](const uint64_t k) { return k == key; });
}

bool SentMarkers::add(const uint64_t key) {
  if (contains(key)) return false;
  if (keys_.size() >= MAX_ENTRIES) keys_.erase(keys_.begin());
  keys_.push_back(key);
  return true;
}

std::string sentFileName(const std::string_view bookPath) {
  uint64_t hash = fnv1a64(FNV64_BASIS, bookPath);
  std::string out(16, '0');
  for (size_t i = 16; i > 0; i--) {
    out[i - 1] = HEX_DIGITS[hash & 0xF];
    hash >>= 4;
  }
  out += ".sent";
  return out;
}

}  // namespace highlightexport
