#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "VaultLink.h"

namespace {

// A small in-memory file tree: paths of files and folders.
struct FakeFs {
  std::set<std::string> files;
  std::set<std::string> dirs;
  vaultlink::Fs fs() {
    vaultlink::Fs out;
    out.exists = [this](const std::string& p) { return files.count(p) > 0 || dirs.count(p) > 0; };
    out.list = [this](const std::string& dir, const std::function<void(const std::string&, bool)>& entry) {
      const std::string prefix = dir == "/" ? "/" : dir + "/";
      for (const auto* set : {&dirs, &files}) {
        for (const auto& p : *set) {
          if (p.rfind(prefix, 0) == 0 && p.size() > prefix.size() && p.find('/', prefix.size()) == std::string::npos) {
            entry(p.substr(prefix.size()), set == &dirs);
          }
        }
      }
    };
    return out;
  }
};

FakeFs demoVault() {
  FakeFs f;
  f.dirs = {"/Vaults",
            "/Vaults/Demo",
            "/Vaults/Demo/.obsidian",
            "/Vaults/Demo/Books",
            "/Vaults/Demo/Books/Old",
            "/Vaults/Demo/.trash"};
  f.files = {"/Vaults/Demo/Welcome.md",    "/Vaults/Demo/Books/Reading list.md", "/Vaults/Demo/Books/Old/Notes.md",
             "/Vaults/Demo/Notes.md",      "/Vaults/Demo/.trash/Gone.md",        "/Vaults/Demo/Books/cover.png",
             "/Vaults/Demo/Books/Ιθάκη.md"};
  return f;
}

}  // namespace

TEST(VaultLink, Parse) {
  vaultlink::Target t;
  ASSERT_TRUE(vaultlink::parse("vault:Note#Heading", t));
  EXPECT_EQ(t.note, "Note");
  EXPECT_EQ(t.anchor, "Heading");
  ASSERT_TRUE(vaultlink::parse("vault:other%20note.md", t));
  EXPECT_EQ(t.note, "other note.md");
  EXPECT_EQ(t.anchor, "");
  EXPECT_FALSE(vaultlink::parse("https://example.com", t));
  EXPECT_FALSE(vaultlink::parse("vault:", t));
}

TEST(VaultLink, FileNames) {
  EXPECT_EQ(vaultlink::fileNameFor("Note"), "Note.md");
  EXPECT_EQ(vaultlink::fileNameFor("pic.png"), "pic.png");
  EXPECT_EQ(vaultlink::fileNameFor("v1.2 notes/Plan"), "v1.2 notes/Plan.md");
  EXPECT_EQ(vaultlink::fileNameFor("Dr. Smith"), "Dr. Smith.md");
  EXPECT_EQ(vaultlink::fileNameFor("Wait. Then…"), "Wait. Then….md");
  EXPECT_EQ(vaultlink::fileNameFor("sir).…"), "sir).….md");
  EXPECT_EQ(vaultlink::fileNameFor("Board/a.jpeg"), "Board/a.jpeg");
  EXPECT_EQ(vaultlink::fileNameFor("Map.canvas"), "Map.canvas");
  EXPECT_TRUE(vaultlink::sameName("Reading List.md", "reading list.md"));
}

TEST(VaultLink, VaultRoot) {
  auto f = demoVault();
  EXPECT_EQ(vaultlink::vaultRoot("/Vaults/Demo/Books/Old/Notes.md", f.fs()), "/Vaults/Demo");
  EXPECT_EQ(vaultlink::vaultRoot("/books/x.md", f.fs()), "/books");
}

TEST(VaultLink, ResolvesLikeObsidian) {
  auto f = demoVault();
  const auto fs = f.fs();
  auto resolve = [&](const std::string& note, const std::string& from) {
    vaultlink::Target t{note, ""};
    return vaultlink::resolve("/Vaults/Demo", from, t, fs);
  };
  // Same folder first, then the shortest path in the vault.
  EXPECT_EQ(resolve("Notes", "/Vaults/Demo/Books/Old"), "/Vaults/Demo/Books/Old/Notes.md");
  EXPECT_EQ(resolve("Notes", "/Vaults/Demo/Books"), "/Vaults/Demo/Notes.md");
  EXPECT_EQ(resolve("reading list", "/Vaults/Demo"), "/Vaults/Demo/Books/Reading list.md");
  // Paths, attachments, Greek names.
  EXPECT_EQ(resolve("Books/Old/Notes", "/Vaults/Demo"), "/Vaults/Demo/Books/Old/Notes.md");
  EXPECT_EQ(resolve("cover.png", "/Vaults/Demo"), "/Vaults/Demo/Books/cover.png");
  EXPECT_EQ(resolve("Ιθάκη", "/Vaults/Demo"), "/Vaults/Demo/Books/Ιθάκη.md");
  // Dot folders are not searched; missing notes resolve to nothing.
  EXPECT_EQ(resolve("Gone", "/Vaults/Demo"), "");
  EXPECT_EQ(resolve("Missing", "/Vaults/Demo"), "");
}

TEST(VaultLink, DottedNoteNames) {
  auto f = demoVault();
  f.files.insert("/Vaults/Demo/Books/Section 2.1.md");
  f.files.insert("/Vaults/Demo/Plan v1.2.md");
  f.files.insert("/Vaults/Demo/Books/scan.pdf");
  const auto fs = f.fs();
  auto resolve = [&](const std::string& note) {
    vaultlink::Target t{note, ""};
    return vaultlink::resolve("/Vaults/Demo", "/Vaults/Demo", t, fs);
  };
  EXPECT_EQ(resolve("Section 2.1"), "/Vaults/Demo/Books/Section 2.1.md");
  EXPECT_EQ(resolve("Plan v1.2"), "/Vaults/Demo/Plan v1.2.md");
  EXPECT_EQ(resolve("scan.pdf"), "/Vaults/Demo/Books/scan.pdf");
  EXPECT_EQ(resolve("Books/Section 2.1"), "/Vaults/Demo/Books/Section 2.1.md");
  EXPECT_EQ(vaultlink::fileNameFor(".profile"), ".profile.md");
}

TEST(VaultLink, IndexFileListBeforeScan) {
  auto f = demoVault();
  auto fs = f.fs();
  std::vector<std::string> listed;
  bool hasList = true;
  int scans = 0;
  const auto list = fs.list;
  fs.list = [&](const std::string& dir, const std::function<void(const std::string&, bool)>& entry) {
    ++scans;
    list(dir, entry);
  };
  fs.known = [&](const std::string& root, const std::function<void(std::string_view)>& path) {
    EXPECT_EQ(root, "/Vaults/Demo");
    for (const auto& p : listed) path(p);
    return hasList;
  };
  auto resolve = [&](const std::string& note, const std::string& from) {
    vaultlink::Target t{note, ""};
    return vaultlink::resolve("/Vaults/Demo", from, t, fs);
  };
  // A match in the list is used without scanning; fewest folders deep wins, like the scan.
  listed = {"/Vaults/Demo/Books/Old/Notes.md", "/Vaults/Demo/Books/cover.png"};
  EXPECT_EQ(resolve("notes", "/Vaults/Demo/Books"), "/Vaults/Demo/Books/Old/Notes.md");
  EXPECT_EQ(resolve("Cover.PNG", "/Vaults/Demo"), "/Vaults/Demo/Books/cover.png");
  listed.push_back("/Vaults/Demo/Notes.md");
  EXPECT_EQ(resolve("Notes", "/Vaults/Demo/Books"), "/Vaults/Demo/Notes.md");
  EXPECT_EQ(scans, 0);
  // A listed file that is gone, or one added after the list was written: the scan finds it.
  listed = {"/Vaults/Demo/Deleted/Reading list.md"};
  EXPECT_EQ(resolve("Reading list", "/Vaults/Demo"), "/Vaults/Demo/Books/Reading list.md");
  EXPECT_GT(scans, 0);
  hasList = false;
  listed.clear();
  EXPECT_EQ(resolve("Ιθάκη", "/Vaults/Demo"), "/Vaults/Demo/Books/Ιθάκη.md");
}
