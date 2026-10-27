#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "LiveSyncPull.h"

// test/livesync/fixture is a dump of a CouchDB that the official Self-hosted LiveSync CLI filled
// from demo-vault/VaultInk Demo (E2EE V2, path obfuscation, chunk size 60, passphrase below).
namespace {

constexpr const char* PASSPHRASE = "vaultink-demo-pass";

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

std::string fixture(const char* name) { return readFile(std::string(LIVESYNC_FIXTURE_DIR) + "/" + name); }

}  // namespace

TEST(LiveSyncPull, RebuildsTheDemoVaultExactly) {
  JsonDocument params;
  ASSERT_EQ(deserializeJson(params, fixture("sync_parameters.json")), DeserializationError::Ok);
  std::string salt;
  ASSERT_TRUE(livesync::base64Decode(params["pbkdf2salt"] | "", salt));
  livesync::Key master;
  ASSERT_TRUE(livesync::deriveMaster(PASSPHRASE, reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), master));

  JsonDocument docs;
  ASSERT_EQ(deserializeJson(docs, fixture("docs.json")), DeserializationError::Ok);
  std::map<std::string, JsonObjectConst> byId;
  for (JsonObjectConst d : docs.as<JsonArrayConst>()) byId[d["_id"] | ""] = d;

  const auto version = byId.find("obsydian_livesync_version");
  ASSERT_NE(version, byId.end());
  EXPECT_TRUE(livesync::remoteVersionSupported(version->second));

  const livesync::ChunkGetter chunk = [&](const std::string& id, std::string& data, bool& encrypted) {
    const auto it = byId.find(id);
    if (it == byId.end()) return false;
    data = it->second["data"] | "";
    encrypted = it->second["e_"] | false;
    return true;
  };

  int files = 0;
  for (const auto& [id, doc] : byId) {
    livesync::FileEntry entry;
    livesync::DocKind kind;
    ASSERT_EQ(livesync::readFileDoc(doc, master, entry, kind), livesync::Result::Ok) << id;
    if (kind != livesync::DocKind::File) continue;
    ++files;
    std::string content;
    ASSERT_EQ(livesync::assemble(entry, master, chunk,
                                 [&](const char* p, size_t n) {
                                   content.append(p, n);
                                   return true;
                                 }),
              livesync::Result::Ok)
        << entry.path;
    const std::string original = readFile(std::string(DEMO_VAULT_DIR) + "/" + entry.path);
    ASSERT_FALSE(original.empty()) << entry.path;
    EXPECT_EQ(content.size(), entry.size) << entry.path;
    EXPECT_TRUE(content == original) << entry.path;
  }
  EXPECT_EQ(files, 13);
}

TEST(LiveSyncPull, RefusesNewerRemotes) {
  JsonDocument v;
  deserializeJson(v, R"({"version":14})");
  std::string why;
  EXPECT_FALSE(livesync::remoteVersionSupported(v.as<JsonObjectConst>(), &why));
  deserializeJson(v, R"({"version":13,"used_features":["something-new-v9"]})");
  EXPECT_FALSE(livesync::remoteVersionSupported(v.as<JsonObjectConst>(), &why));
  EXPECT_EQ(why, "remote feature something-new-v9");
  deserializeJson(v, R"({"version":12})");
  EXPECT_TRUE(livesync::remoteVersionSupported(v.as<JsonObjectConst>()));
}

TEST(LiveSyncPull, WrongPassphraseFailsCleanly) {
  JsonDocument docs;
  ASSERT_EQ(deserializeJson(docs, fixture("docs.json")), DeserializationError::Ok);
  livesync::Key wrong{};
  for (JsonObjectConst d : docs.as<JsonArrayConst>()) {
    const std::string id = d["_id"] | "";
    if (id.rfind("f:", 0) != 0) continue;
    livesync::FileEntry entry;
    livesync::DocKind kind;
    EXPECT_EQ(livesync::readFileDoc(d, wrong, entry, kind), livesync::Result::WrongKey);
    break;
  }
}
