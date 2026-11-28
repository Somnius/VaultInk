#include <gtest/gtest.h>

#include <map>
#include <string>

#include "VaultHome.h"

namespace {

// A fake SD card: full path -> contents; folders are implied by the paths.
struct FakeFs {
  std::map<std::string, std::string> files;

  vaultindex::IndexFs fs() {
    vaultindex::IndexFs out;
    out.exists = [this](const std::string& path) { return files.count(path) > 0; };
    out.list = [this](const std::string& dir, const std::function<void(const std::string& name, bool isDir)>& entry) {
      const std::string prefix = dir == "/" ? "/" : dir + "/";
      std::string lastDir;
      for (const auto& [path, text] : files) {
        if (path.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string rest = path.substr(prefix.size());
        const size_t slash = rest.find('/');
        if (slash == std::string::npos) {
          entry(rest, false);
        } else if (rest.substr(0, slash) != lastDir) {
          lastDir = rest.substr(0, slash);
          entry(lastDir, true);
        }
      }
    };
    out.read = [this](const std::string& path, size_t cap, std::string& text) {
      const auto it = files.find(path);
      if (it == files.end()) return false;
      text = it->second.substr(0, cap);
      return true;
    };
    return out;
  }
};

const std::string ROOT = "/Vaults/RPG";
const std::string SETTINGS = ROOT + "/.obsidian/plugins/homepage/data.json";

}  // namespace

TEST(VaultHome, HomepagePluginNewFormat) {
  EXPECT_EQ(vaulthome::homepageSetting(
                R"({"version":3,"homepages":{"Main Homepage":{"value":"Start Here","kind":"File","openOnStartup":true,)"
                R"("commands":[],"refreshDataview":false}},"separateMobile":false})"),
            "Start Here");
  // A workspace or graph homepage isn't a note.
  EXPECT_EQ(vaulthome::homepageSetting(R"({"homepages":{"Main Homepage":{"value":"Writing","kind":"Workspace"}}})"),
            "");
  EXPECT_EQ(
      vaulthome::homepageSetting(
          R"({"homepages":{"Main Homepage":{"value":"W","kind":"Workspace"},"Mobile":{"value":"M","kind":"File"}}})"),
      "M");
  EXPECT_EQ(vaulthome::homepageSetting(R"({"homepages":{"Main Homepage":{"value":"Café \"A\""}}})"),
            "Caf\xC3\xA9 \"A\"");
}

TEST(VaultHome, HomepagePluginOldFormat) {
  EXPECT_EQ(vaulthome::homepageSetting(R"({"defaultNote":"Dashboard","openOnStartup":true,"view":"Default"})"),
            "Dashboard");
  EXPECT_EQ(vaulthome::homepageSetting(R"({"defaultNote":null})"), "");
}

TEST(VaultHome, BrokenSettings) {
  EXPECT_EQ(vaulthome::homepageSetting(""), "");
  EXPECT_EQ(vaulthome::homepageSetting("{\"defaultNote\":\"A"), "");
  EXPECT_EQ(vaulthome::homepageSetting("[1,2]"), "");
  EXPECT_EQ(vaulthome::homepageSetting(std::string(100, '{')), "");
}

TEST(VaultHome, PluginNoteIsAPathFromTheRoot) {
  FakeFs card;
  card.files[SETTINGS] = R"({"homepages":{"Main Homepage":{"value":"Meta/Dashboard","kind":"File"}}})";
  card.files[ROOT + "/Home.md"] = "";
  card.files[ROOT + "/Meta/Dashboard.md"] = "";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), ROOT + "/Meta/Dashboard.md");
  // A path with ".md" already.
  card.files[SETTINGS] = R"({"defaultNote":"Meta/Dashboard.md"})";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), ROOT + "/Meta/Dashboard.md");
  // A bare name is not searched for through the vault: Home.md instead.
  card.files[SETTINGS] = R"({"defaultNote":"Dashboard"})";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), ROOT + "/Home.md");
}

TEST(VaultHome, PluginNoteMayNotLeaveTheVault) {
  EXPECT_FALSE(vaulthome::safeNotePath("../Other/Secret"));
  EXPECT_FALSE(vaulthome::safeNotePath("Meta/../../x"));
  EXPECT_FALSE(vaulthome::safeNotePath(".."));
  EXPECT_FALSE(vaulthome::safeNotePath("/Vaults/Other/Home"));
  EXPECT_FALSE(vaulthome::safeNotePath("Meta\\Home"));
  EXPECT_FALSE(vaulthome::safeNotePath(""));
  EXPECT_TRUE(vaulthome::safeNotePath("Meta/..Notes/Home"));
  EXPECT_TRUE(vaulthome::safeNotePath("Start Here"));
  FakeFs card;
  card.files["/Vaults/Other/Secret.md"] = "";
  card.files[ROOT + "/index.md"] = "";
  card.files[SETTINGS] = R"({"defaultNote":"../Other/Secret"})";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), ROOT + "/index.md");
}

TEST(VaultHome, MissingPluginNoteFallsBackToHome) {
  FakeFs card;
  card.files[SETTINGS] = R"({"defaultNote":"Gone"})";
  card.files[ROOT + "/home.MD"] = "";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), ROOT + "/home.MD");
}

TEST(VaultHome, HomeBeforeIndex) {
  FakeFs card;
  card.files[ROOT + "/Index.md"] = "";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), ROOT + "/Index.md");
  card.files[ROOT + "/Home.md"] = "";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), ROOT + "/Home.md");
}

TEST(VaultHome, NothingFound) {
  FakeFs card;
  card.files[ROOT + "/Notes/Home.md"] = "";  // not at the root
  card.files[ROOT + "/Homework.md"] = "";
  EXPECT_EQ(vaulthome::resolve(ROOT, card.fs()), "");
}
