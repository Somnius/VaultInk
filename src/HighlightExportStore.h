#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

// Where a book's highlights can be sent (Settings → Network → Highlight exports): the Readwise
// access token, and the address and access token of a Memos server.
//
// Kept like the other credentials (KOReader sync, OPDS): in a JSON file on the card, the tokens
// XOR-ed with the device's hardware address and base64-encoded. That stops a casual read and ties
// the file to this device; it is not encryption, and anyone holding the card and the device can
// get the tokens back. The tokens are never logged and never sent to the web settings page.
class HighlightExportStore : public PersistableStore<HighlightExportStore> {
 public:
  // Memos tokens are long (a JWT in older servers); Readwise's are about 50 characters.
  static constexpr size_t MAX_TOKEN_CHARS = 512;
  static constexpr size_t MAX_URL_CHARS = 200;

  static const char* getFilePath() { return "/.crosspoint/exports.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::string& readwiseToken() const { return readwiseToken_; }
  const std::string& memosUrl() const { return memosUrl_; }
  const std::string& memosToken() const { return memosToken_; }
  bool memosVerifyCert() const { return memosVerifyCert_; }

  // Spaces and line ends around a pasted token are dropped; anything too long is refused (kept as it was).
  void setReadwiseToken(const std::string& token);
  void setMemosUrl(const std::string& url);
  void setMemosToken(const std::string& token);
  void setMemosVerifyCert(bool enabled) { memosVerifyCert_ = enabled; }

  bool hasReadwise() const { return !readwiseToken_.empty(); }
  bool hasMemos() const { return !memosUrl_.empty() && !memosToken_.empty(); }

 private:
  HighlightExportStore() = default;
  ~HighlightExportStore() = default;
  friend class PersistableStore<HighlightExportStore>;

  std::string readwiseToken_;
  std::string memosUrl_;
  std::string memosToken_;
  bool memosVerifyCert_ = false;  // the user's own server: encrypted only until this is on (lib/TlsPolicy)
};

#define EXPORT_STORE HighlightExportStore::getInstance()
