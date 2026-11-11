#include "HighlightExportStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>

namespace {

// A token or address as typed or pasted: no spaces or line ends around it, and no control
// characters inside (one would break the HTTP header it goes into).
bool clean(const std::string& in, const size_t maxChars, std::string& out) {
  size_t begin = 0;
  size_t end = in.size();
  while (begin < end && static_cast<uint8_t>(in[begin]) <= 0x20) begin++;
  while (end > begin && static_cast<uint8_t>(in[end - 1]) <= 0x20) end--;
  // cppcheck-suppress knownConditionTrueFalse // not so: the setters pass text of any length
  if (end - begin > maxChars) return false;
  if (std::any_of(in.begin() + static_cast<std::ptrdiff_t>(begin), in.begin() + static_cast<std::ptrdiff_t>(end),
                  [](const char c) { return static_cast<uint8_t>(c) < 0x20 || c == 0x7F; })) {
    return false;
  }
  out.assign(in, begin, end - begin);
  return true;
}

std::string readToken(JsonVariantConst value) {
  bool ok = false;
  bool tooLong = false;
  std::string token =
      obfuscation::deobfuscateFromBase64(value | "", HighlightExportStore::MAX_TOKEN_CHARS, &ok, &tooLong);
  std::string cleaned;
  // A file moved from another device decodes to noise: treat it as not set.
  if (!ok || tooLong || !clean(token, HighlightExportStore::MAX_TOKEN_CHARS, cleaned)) return {};
  return cleaned;
}

}  // namespace

void HighlightExportStore::toJson(JsonDocument& doc) const {
  doc["readwiseToken_obf"] = obfuscation::obfuscateToBase64(readwiseToken_);
  doc["memosUrl"] = memosUrl_;
  doc["memosToken_obf"] = obfuscation::obfuscateToBase64(memosToken_);
  doc["memosVerifyCert"] = memosVerifyCert_;
}

bool HighlightExportStore::fromJson(JsonVariantConst doc) {
  readwiseToken_ = readToken(doc["readwiseToken_obf"]);
  memosToken_ = readToken(doc["memosToken_obf"]);
  memosUrl_.clear();
  setMemosUrl(doc["memosUrl"] | "");
  const JsonVariantConst verifyCert = doc["memosVerifyCert"];
  memosVerifyCert_ = verifyCert.is<bool>() && verifyCert.as<bool>();
  // Only whether each is set: never the values.
  LOG_DBG("EXP", "Exports: Readwise %s, Memos %s", hasReadwise() ? "set" : "not set", hasMemos() ? "set" : "not set");
  return true;
}

void HighlightExportStore::setReadwiseToken(const std::string& token) {
  if (!clean(token, MAX_TOKEN_CHARS, readwiseToken_)) LOG_ERR("EXP", "Readwise token refused (length or characters)");
}

void HighlightExportStore::setMemosToken(const std::string& token) {
  if (!clean(token, MAX_TOKEN_CHARS, memosToken_)) LOG_ERR("EXP", "Memos token refused (length or characters)");
}

void HighlightExportStore::setMemosUrl(const std::string& url) {
  if (!clean(url, MAX_URL_CHARS, memosUrl_)) LOG_ERR("EXP", "Memos address refused (length or characters)");
}
