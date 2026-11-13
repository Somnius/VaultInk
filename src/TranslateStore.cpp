#include "TranslateStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>

namespace {

// An address or key as typed or pasted: no spaces or line ends around it, and no control
// characters inside (one would break the request it goes into).
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

}  // namespace

void TranslateStore::toJson(JsonDocument& doc) const {
  doc["serverUrl"] = serverUrl_;
  doc["apiKey_obf"] = obfuscation::obfuscateToBase64(apiKey_);
  doc["verifyCert"] = verifyCert_;
  doc["myMemoryNoticeSeen"] = myMemoryNoticeSeen_;
  doc["serverNoticeSeen"] = serverNoticeSeen_;
}

bool TranslateStore::fromJson(JsonVariantConst doc) {
  serverUrl_.clear();
  if (!clean(doc["serverUrl"] | "", MAX_URL_CHARS, serverUrl_)) serverUrl_.clear();
  bool ok = false;
  bool tooLong = false;
  const std::string key = obfuscation::deobfuscateFromBase64(doc["apiKey_obf"] | "", MAX_KEY_CHARS, &ok, &tooLong);
  apiKey_.clear();
  // A file moved from another device decodes to noise: treat it as not set.
  if (!ok || tooLong || !clean(key, MAX_KEY_CHARS, apiKey_)) apiKey_.clear();
  const auto flag = [&doc](const char* name) {
    const JsonVariantConst value = doc[name];
    return value.is<bool>() && value.as<bool>();
  };
  verifyCert_ = flag("verifyCert");
  myMemoryNoticeSeen_ = flag("myMemoryNoticeSeen");
  serverNoticeSeen_ = flag("serverNoticeSeen");
  // Only whether each is set: never the values.
  LOG_DBG("TRN", "Translation: server %s, key %s", hasServer() ? "set" : "not set",
          apiKey_.empty() ? "not set" : "set");
  return true;
}

void TranslateStore::setServerUrl(const std::string& url) {
  std::string cleaned;
  if (!clean(url, MAX_URL_CHARS, cleaned)) {
    LOG_ERR("TRN", "Server address refused (length or characters)");
    return;
  }
  if (cleaned == serverUrl_) return;
  serverUrl_ = cleaned;
  serverNoticeSeen_ = false;  // the selection would go somewhere new: ask again
}

void TranslateStore::setApiKey(const std::string& key) {
  if (!clean(key, MAX_KEY_CHARS, apiKey_)) LOG_ERR("TRN", "API key refused (length or characters)");
}

void TranslateStore::setNoticeSeen() {
  if (hasServer()) {
    serverNoticeSeen_ = true;
  } else {
    myMemoryNoticeSeen_ = true;
  }
}
