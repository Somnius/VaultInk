#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "VaultIndex.h"

namespace {

// In-memory SD card: files with contents, and folders.
struct MemFs {
  std::map<std::string, std::string> files;
  std::set<std::string> dirs;
  std::map<std::string, int> edits;
  std::string pendingPath, pending;

  vaultindex::IndexFs fs() {
    vaultindex::IndexFs out;
    out.exists = [this](const std::string& p) { return files.count(p) || dirs.count(p); };
    out.list = [this](const std::string& dir, const std::function<void(const std::string&, bool)>& entry) {
      const std::string prefix = dir == "/" ? "/" : dir + "/";
      for (const auto& d : dirs)
        if (d.rfind(prefix, 0) == 0 && d.size() > prefix.size() && d.find('/', prefix.size()) == std::string::npos)
          entry(d.substr(prefix.size()), true);
      for (const auto& [p, c] : files)
        if (p.rfind(prefix, 0) == 0 && p.find('/', prefix.size()) == std::string::npos)
          entry(p.substr(prefix.size()), false);
    };
    // Stamp: size plus an edit counter standing in for the modification time.
    out.stamp = [this](const std::string& p) -> uint64_t {
      return files.count(p) ? (static_cast<uint64_t>(edits[p]) << 32) | (files[p].size() + 1) : 0;
    };
    out.read = [this](const std::string& p, size_t cap, std::string& o) {
      if (!files.count(p)) return false;
      o = files[p].substr(0, cap);
      return true;
    };
    out.lines = [this](const std::string& p, const std::function<bool(std::string_view)>& line) {
      if (!files.count(p)) return false;
      const std::string text = files[p];
      size_t start = 0;
      while (start < text.size()) {
        const size_t nl = text.find('\n', start);
        if (!line(std::string_view(text).substr(start, nl - start))) break;
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
      return true;
    };
    out.begin = [this](const std::string& p) {
      pendingPath = p;
      pending.clear();
      return true;
    };
    out.append = [this](std::string_view t) {
      pending.append(t);
      return true;
    };
    out.commit = [this](bool keep) {
      if (keep) files[pendingPath] = pending;
      return true;
    };
    return out;
  }
};

const std::string INDEX = "/.vaultink/demo/index.tsv";

MemFs demo() {
  MemFs m;
  m.dirs = {"/V", "/V/.obsidian", "/V/Books", "/V/.trash"};
  m.files["/V/Welcome.md"] =
      "---\naliases: [Start here, Αρχή]\ntags:\n  - demo\n---\n# Welcome\nRead the #guide first.\n";
  m.files["/V/Books/Καλημέρα κόσμε.md"] = "Ἐν ἀρχῇ ἦν ὁ λόγος. Ταξίδι στην Ιθάκη.";
  m.files["/V/Books/Shopping.md"] = "milk, bread and eggs";
  m.files["/V/.trash/Old.md"] = "should not be indexed";
  return m;
}

}  // namespace

TEST(VaultIndex, ScanNote) {
  const auto note = vaultindex::scanNote("/V/Welcome.md", demo().files["/V/Welcome.md"]);
  EXPECT_EQ(note.title, "Welcome");
  EXPECT_EQ(note.aliases, (std::vector<std::string>{"Start here", "Αρχή"}));
  EXPECT_EQ(note.tags, (std::vector<std::string>{"demo", "guide"}));
  EXPECT_NE(std::find(note.terms.begin(), note.terms.end(), "first"), note.terms.end());
}

TEST(VaultIndex, LineRoundTrip) {
  const auto note = vaultindex::scanNote("/V/Welcome.md", demo().files["/V/Welcome.md"]);
  vaultindex::Note back;
  ASSERT_TRUE(vaultindex::fromLine(vaultindex::toLine(note), back));
  EXPECT_EQ(back.path, note.path);
  EXPECT_EQ(back.aliases, note.aliases);
  EXPECT_EQ(back.tags, note.tags);
  EXPECT_EQ(back.terms, note.terms);
}

TEST(VaultIndex, BuildSkipsDotFolders) {
  auto m = demo();
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.notes, 3u);
  EXPECT_EQ(m.files[INDEX].find("Old.md"), std::string::npos);
}

TEST(VaultIndex, IncrementalRebuild) {
  auto m = demo();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  m.files["/V/Books/Shopping.md"] = "milk, bread, eggs and coffee";  // changed
  m.files["/V/New.md"] = "brand new";                                // added
  m.files.erase("/V/Welcome.md");                                    // deleted
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.notes, 3u);
  EXPECT_EQ(stats.rescanned, 2u);  // the changed and the new note; Καλημέρα kept its line
  EXPECT_EQ(vaultindex::search(INDEX, "coffee", m.fs()).size(), 1u);
  EXPECT_TRUE(vaultindex::search(INDEX, "welcome", m.fs()).empty());
  // An edit that keeps the length is caught by the modification time.
  m.files["/V/New.md"] = "brand old";
  ++m.edits["/V/New.md"];
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.rescanned, 1u);
  EXPECT_EQ(vaultindex::search(INDEX, "old", m.fs()).size(), 1u);
}

TEST(VaultIndex, ExactWordsRankFirst) {
  MemFs m;
  m.dirs = {"/V"};
  m.files["/V/Cardiology.md"] = "x";
  m.files["/V/Car.md"] = "y";
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  const auto hits = vaultindex::search(INDEX, "car", m.fs());
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].title, "Car");
}

TEST(VaultIndex, SearchFoldsGreekAndRanksTitles) {
  auto m = demo();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  auto hits = vaultindex::search(INDEX, "καλημερα", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].title, "Καλημέρα κόσμε");
  EXPECT_EQ(vaultindex::search(INDEX, "ΙΘΑΚΗ", m.fs()).size(), 1u);          // body, upper case, no accent
  EXPECT_EQ(vaultindex::search(INDEX, "αρχη", m.fs()).size(), 2u);           // alias of Welcome + polytonic body
  EXPECT_EQ(vaultindex::search(INDEX, "αρχη", m.fs())[0].title, "Welcome");  // alias beats body
  EXPECT_EQ(vaultindex::search(INDEX, "milk egg", m.fs()).size(), 1u);       // every word, prefixes
  EXPECT_TRUE(vaultindex::search(INDEX, "milk zebra", m.fs()).empty());
  EXPECT_EQ(vaultindex::search(INDEX, "guide", m.fs()).size(), 1u);  // inline tag
}
