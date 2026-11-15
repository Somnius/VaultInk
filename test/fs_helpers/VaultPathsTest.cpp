#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "VaultPaths.h"

TEST(VaultPaths, OnlyWhatIsInVaultsIsUnderVaults) {
  for (const char* path : {"/Vaults", "/Vaults/", "/Vaults/V1", "/Vaults/V1/a.md", "/Vaults/V1/sub/b.epub",
                           "/vaults/V1/a.md", "/VAULTS/x.txt"}) {
    EXPECT_TRUE(vaultpaths::isUnderVaults(path)) << path;
  }
  for (const char* path : {"", "/", "/Vault", "/Vaults2/a.md", "/VaultsOld", "/Books/Vaults/a.md", "/Notes/c.md",
                           "Vaults/a.md", "/.vaultink/pdf/a.md"}) {
    EXPECT_FALSE(vaultpaths::isUnderVaults(path)) << path;
  }
}

TEST(VaultPaths, AVaultIsAFolderRightInVaults) {
  EXPECT_TRUE(vaultpaths::isVault("/Vaults/V1"));
  EXPECT_TRUE(vaultpaths::isVault("/Vaults/Σημειώσεις"));
  EXPECT_TRUE(vaultpaths::isVault("/Vaults/No obsidian folder"));
  // A top-level folder is not a vault, whether or not it holds .obsidian.
  EXPECT_FALSE(vaultpaths::isVault("/Notes"));
  EXPECT_FALSE(vaultpaths::isVault("/Books/Notes"));
  EXPECT_FALSE(vaultpaths::isVault("/Vaults"));
  EXPECT_FALSE(vaultpaths::isVault("/Vaults/"));
  EXPECT_FALSE(vaultpaths::isVault("/Vaults/V1/sub"));
  EXPECT_FALSE(vaultpaths::isVault("/Vaults/.trash"));
  EXPECT_FALSE(vaultpaths::isVault("/Vaults/System Volume Information"));
  EXPECT_FALSE(vaultpaths::isVault("/Vaults2/V1"));
}

TEST(VaultPaths, SavedBookListsLoseTheirVaultEntries) {
  struct Entry {
    std::string path;
    std::string title;
  };
  std::vector<Entry> recent = {{"/Vaults/V1/a.md", "A"},
                               {"/Books/x.epub", "X"},
                               {"/Vaults/V1/sub/b.epub", "B"},
                               {"/Notes/c.md", "C"},
                               {"/.vaultink/pdf/p.md", "P"}};
  EXPECT_TRUE(vaultpaths::dropVaultEntries(recent));
  ASSERT_EQ(recent.size(), 3u);
  EXPECT_EQ(recent[0].path, "/Books/x.epub");
  EXPECT_EQ(recent[1].path, "/Notes/c.md");
  EXPECT_EQ(recent[2].path, "/.vaultink/pdf/p.md");
  EXPECT_FALSE(vaultpaths::dropVaultEntries(recent));
}

TEST(VaultPaths, RecentNotesAreNewestFirstWithoutCopies) {
  std::vector<std::string> notes;
  for (const char* note : {"/Vaults/V/1.md", "/Vaults/V/2.md", "/Vaults/V/3.md"}) {
    vaultpaths::pushRecent(notes, note, 3);
  }
  EXPECT_EQ(notes, (std::vector<std::string>{"/Vaults/V/3.md", "/Vaults/V/2.md", "/Vaults/V/1.md"}));
  vaultpaths::pushRecent(notes, "/Vaults/V/1.md", 3);
  EXPECT_EQ(notes, (std::vector<std::string>{"/Vaults/V/1.md", "/Vaults/V/3.md", "/Vaults/V/2.md"}));
  vaultpaths::pushRecent(notes, "/Vaults/V/4.md", 3);
  EXPECT_EQ(notes, (std::vector<std::string>{"/Vaults/V/4.md", "/Vaults/V/1.md", "/Vaults/V/3.md"}));
}
