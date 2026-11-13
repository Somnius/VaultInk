#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

// The translation lookup's settings (Settings → Network → Translation): the address, API key and
// Check certificate switch of a LibreTranslate server of the user's own, and whether the "what is
// sent" notice has been answered for each of the two services.
//
// Kept like the other credentials (highlight exports, KOReader sync): in a JSON file on the card,
// the key XOR-ed with the device's hardware address and base64-encoded. That stops a casual read
// and ties the file to this device; it is not encryption. The key is never logged and never sent
// to the web settings page.
class TranslateStore : public PersistableStore<TranslateStore> {
 public:
  static constexpr size_t MAX_KEY_CHARS = 128;
  static constexpr size_t MAX_URL_CHARS = 200;

  static const char* getFilePath() { return "/.crosspoint/translate.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::string& serverUrl() const { return serverUrl_; }
  const std::string& apiKey() const { return apiKey_; }
  bool verifyCert() const { return verifyCert_; }
  // With an address set the user's server is asked, and MyMemory never is.
  bool hasServer() const { return !serverUrl_.empty(); }

  // Spaces and line ends around a pasted value are dropped; anything too long or with control
  // characters is refused (kept as it was). A new address asks the notice again.
  void setServerUrl(const std::string& url);
  void setApiKey(const std::string& key);
  void setVerifyCert(bool enabled) { verifyCert_ = enabled; }

  // The notice that says what leaves the device, answered once per service.
  bool noticeSeen() const { return hasServer() ? serverNoticeSeen_ : myMemoryNoticeSeen_; }
  void setNoticeSeen();

 private:
  TranslateStore() = default;
  ~TranslateStore() = default;
  friend class PersistableStore<TranslateStore>;

  std::string serverUrl_;
  std::string apiKey_;
  bool verifyCert_ = false;  // the user's own server: encrypted only until this is on (lib/TlsPolicy)
  bool myMemoryNoticeSeen_ = false;
  bool serverNoticeSeen_ = false;
};

#define TRANSLATE_STORE TranslateStore::getInstance()
