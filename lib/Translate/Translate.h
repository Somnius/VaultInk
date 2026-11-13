#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The pure parts of the translation lookup: which two languages, the request for each of the two
// sources, and reading the translation out of the answer. No network or platform code here.
//
// Sources, in this order:
//  * A LibreTranslate server of the user's own, when its address is set: POST {server}/translate
//    with a JSON body {"q","source","target","format":"text"[,"api_key"]}; the answer is
//    {"translatedText":"..."} or, with an error status, {"error":"..."}
//    (https://docs.libretranslate.com/, read 2026-11-13).
//  * MyMemory otherwise, with no key: GET https://api.mymemory.translated.net/get?q=...&langpair=en|el
//    (https://mymemory.translated.net/doc/spec.php, read 2026-11-13: q is "Max 500 bytes",
//    langpair is "Source and language pair, separated by the | symbol"; usagelimits.php: "Free,
//    anonymous usage is limited to 5000 chars/day"). The optional "de" e-mail parameter is not
//    sent. Its terms allow "Automated and systematic API access by machines" but not "translating
//    more than one paragraph at once", which the 500 byte limit keeps to.
//
// Only the selected word or phrase goes out, with the two language codes.
namespace translate {

// No more than this much of an answer is read.
inline constexpr size_t MAX_BODY_BYTES = 8 * 1024;
// The longest selection that is sent (MyMemory's limit for q; kept for LibreTranslate too).
inline constexpr size_t MAX_QUERY_BYTES = 500;
// The longest translation that is kept.
inline constexpr size_t MAX_TEXT_BYTES = 2048;

inline constexpr char MYMEMORY_HOST[] = "api.mymemory.translated.net";

struct Pair {
  std::string source;  // empty: not known (LibreTranslate is asked to detect it; MyMemory cannot be asked)
  std::string target;  // empty: there is nothing to translate into
  bool ok() const { return !target.empty(); }
};

// From the book's language into the menu language. A book in the menu language is translated into
// English instead; an English book under English menus has no pair. An unknown menu language
// counts as English.
Pair languagePair(std::string_view bookLanguage, std::string_view uiLanguage);

// The selection as it is sent: no spaces around it, runs of spaces and line ends as one space, no
// soft hyphens or control characters. Empty when nothing is left or it is over MAX_QUERY_BYTES.
std::string cleanQuery(std::string_view text);

// Percent-encodes everything outside A-Z a-z 0-9 - . _ ~ (UTF-8 bytes one by one).
std::string percentEncode(std::string_view text);

// The MyMemory address, or empty when there is no query, no target or no source language.
std::string myMemoryUrl(std::string_view text, const Pair& pair);

// {server}/translate for the address the user typed (with or without a trailing slash or the
// /translate itself). Empty when it is not an http:// or https:// address.
std::string libreTranslateUrl(std::string_view server);

// The JSON body for LibreTranslate, or empty when there is no query or no target. An unknown
// source is sent as "auto". The key is left out when empty.
std::string libreTranslateBody(std::string_view text, const Pair& pair, std::string_view apiKey);

enum class Status : uint8_t {
  Ok,             // `text` is the translation
  NoTranslation,  // an answer with no text in it
  Quota,          // the day's free allowance is used up, or the server says slow down (429)
  BadLanguage,    // the service does not know this pair of languages
  Refused,        // 403: the key is missing or wrong, or the address is banned
  ServerError,    // 5xx, or an error the service reports about itself
  Invalid,        // not an answer we can read
};

struct Result {
  Status status = Status::Invalid;
  std::string text;
  std::string detail;      // what the service said about an error, for the log
  bool truncated = false;  // the translation was cut at the size limit
};

// Fed the answer's body as it arrives. One reader for both services: it keeps `translatedText`
// (top level, or inside `responseData`), `responseStatus`, `responseDetails`, `quotaFinished` and
// `error`, and skips everything else (MyMemory's list of matches) as it streams past.
class AnswerParser {
 public:
  // False once MAX_BODY_BYTES have been taken: stop reading.
  bool feed(const char* data, size_t len);
  // The result, given the HTTP status of the answer.
  Result finishMyMemory(int httpStatus);
  Result finishLibreTranslate(int httpStatus);

 private:
  enum class Field : uint8_t { None, Text, StatusCode, Details, Quota, Error };

  void onChar(char c);
  void onStringChar(char c);
  void endString();
  void endPrimitive();
  void appendCodepoint(uint32_t cp);
  void appendByte(char c);
  Field wanted() const;
  std::string* target(size_t& cap);
  std::string takeText(bool& truncated);

  std::string text_;
  std::string status_;
  std::string details_;
  std::string error_;
  std::string key_;
  std::string primitive_;
  size_t taken_ = 0;
  int depth_ = 0;
  uint32_t highSurrogate_ = 0;
  uint16_t unicode_ = 0;
  uint8_t unicodeLeft_ = 0;
  Field field_ = Field::None;                 // the value being read
  bool isObject_[3] = {false, false, false};  // by depth, 1 and 2 only
  bool inData_ = false;                       // depth 2 is the object under "responseData"
  bool inString_ = false;
  bool inPrimitive_ = false;
  bool escaped_ = false;
  bool stringIsKey_ = false;
  bool expectKey_ = false;
  bool closed_ = false;
  bool haveText_ = false;
  bool textCut_ = false;
  bool quota_ = false;
};

}  // namespace translate
