#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The pure parts of the Wikipedia lookup: which Wikipedia to ask, the address of a page summary,
// and reading the two fields we show out of the answer. No network or platform code here.
//
// The answer comes from https://{lang}.wikipedia.org/api/rest_v1/page/summary/{title}: one JSON
// object. Only its top-level "type", "title" and "extract" strings are kept; everything else
// (pictures, links, the HTML extract) is skipped as it streams past.
namespace wikipedia {

// No more than this much of an answer is read.
inline constexpr size_t MAX_BODY_BYTES = 8 * 1024;
// A selection longer than this is not a title.
inline constexpr size_t MAX_TITLE_BYTES = 200;

// The Wikipedia language for a lookup: the book's dc:language, else the UI language, else "en".
// "el-GR", "EL" and "el_GR" all give "el"; anything that is not a 2-3 letter code is passed over.
std::string languageFor(std::string_view bookLanguage, std::string_view uiLanguage);

// The page title for a selection, percent-encoded for a URL path: trimmed of spaces and of the
// punctuation around a word, inner spaces as "_". Empty when nothing is left or it is too long.
std::string encodeTitle(std::string_view text);

// The summary address, or empty when `text` gives no title.
std::string summaryUrl(std::string_view language, std::string_view text);

enum class Kind : uint8_t {
  Article,         // title and extract are set
  Disambiguation,  // the title names several articles
  NotFound,        // no such article (HTTP 404)
  Invalid,         // not a summary, no extract, or cut off before the extract
};

struct Summary {
  Kind kind = Kind::Invalid;
  std::string title;
  std::string extract;
  bool truncated = false;  // the extract was cut at the size limit
};

// Fed the answer's body as it arrives.
class SummaryParser {
 public:
  // False once MAX_BODY_BYTES have been taken: stop reading.
  bool feed(const char* data, size_t len);
  // The result, given the HTTP status of the answer.
  Summary finish(int httpStatus);

 private:
  enum class Field : uint8_t { None, Type, Title, Extract };

  void onChar(char c);
  void onStringChar(char c);
  void endString();
  void appendCodepoint(uint32_t cp);
  std::string* target();

  std::string type_;
  std::string title_;
  std::string extract_;
  std::string key_;
  size_t taken_ = 0;
  int depth_ = 0;
  uint32_t highSurrogate_ = 0;
  uint16_t unicode_ = 0;
  uint8_t unicodeLeft_ = 0;
  Field field_ = Field::None;  // the top-level string value being read
  bool inString_ = false;
  bool escaped_ = false;
  bool stringIsKey_ = false;
  bool expectKey_ = false;  // at depth 1: the next string is a key
  bool closed_ = false;     // the top-level object ended
};

}  // namespace wikipedia
