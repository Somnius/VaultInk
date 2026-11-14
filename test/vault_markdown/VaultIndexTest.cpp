#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "VaultIndex.h"

namespace {

// In-memory SD card: files with contents, and folders.
struct MemFs {
  std::map<std::string, std::string> files;
  std::set<std::string> dirs;
  std::map<std::string, int> edits;
  std::string pendingPath, pending;
  // What a build cost: files opened by path for their stamp, notes read, files written.
  int stamps = 0, reads = 0;
  std::vector<std::string> written;
  std::set<std::string> unreadable;
  bool stampedListing = false;  // fs() also offers listStamped, like the SD card does

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
      ++stamps;
      return stampOf(p);
    };
    if (stampedListing) {
      out.listStamped = [this, list = out.list](const std::string& dir,
                                                const std::function<void(const std::string&, bool, uint64_t)>& entry) {
        list(dir, [&](const std::string& name, const bool isDir) {
          entry(name, isDir, isDir ? 0 : stampOf((dir == "/" ? "" : dir) + "/" + name));
        });
      };
    }
    out.read = [this](const std::string& p, size_t cap, std::string& o) {
      ++reads;
      if (!files.count(p) || unreadable.count(p)) return false;
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
      if (keep) written.push_back(pendingPath);
      return true;
    };
    return out;
  }
  uint64_t stampOf(const std::string& p) {
    return files.count(p) ? (static_cast<uint64_t>(edits[p]) << 32) | (files[p].size() + 1) : 0;
  }
  void resetCost() {
    stamps = reads = 0;
    written.clear();
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

TEST(VaultIndex, BuildWritesFileList) {
  auto m = demo();
  m.files["/V/Books/cover.png"] = "png";
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  const std::string& list = m.files[vaultindex::fileListPathFor("/V")];
  EXPECT_NE(list.find("/V/Welcome.md\n"), std::string::npos);
  EXPECT_NE(list.find("/V/Books/cover.png\n"), std::string::npos);
  EXPECT_EQ(list.find(".trash"), std::string::npos);
  EXPECT_EQ(list.find(".obsidian"), std::string::npos);
  EXPECT_EQ(vaultindex::fileListPathFor("/V").substr(0, 11), "/.vaultink/");
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

TEST(VaultIndex, LinksAndBacklinks) {
  const auto note = vaultindex::scanNote("/V/A.md",
                                         "See [[Target|the target]], ![[Pics/cat.png]], [[Deep/Note#Part]] and "
                                         "[md](Other%20Note.md) and [web](https://x.org/a.md). [[Target]] again.");
  EXPECT_EQ(note.links, (std::vector<std::string>{"Target", "Pics/cat.png", "Deep/Note", "Other Note"}));
  vaultindex::Note back;
  ASSERT_TRUE(vaultindex::fromLine(vaultindex::toLine(note), back));
  EXPECT_EQ(back.links, note.links);

  MemFs m;
  m.dirs = {"/V", "/V/Deep", "/V/Other"};
  m.files["/V/A.md"] = "Links to [[target]] and [[Deep/Note]].";
  m.files["/V/B.md"] = "Only [[Note]].";
  m.files["/V/C.md"] = "Nothing here, [[Otherdeep/Note]] is a different folder.";
  m.files["/V/Target.md"] = "Back to [[A]].";
  m.files["/V/Deep/Note.md"] = "x";
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  auto hits = vaultindex::backlinks(INDEX, "/V", "/V/Target.md", m.fs());
  ASSERT_EQ(hits.size(), 1u);  // case doesn't matter
  EXPECT_EQ(hits[0].title, "A");
  hits = vaultindex::backlinks(INDEX, "/V", "/V/Deep/Note.md", m.fs());
  ASSERT_EQ(hits.size(), 2u);  // by name and by path, not by a look-alike folder
  EXPECT_EQ(hits[0].title, "A");
  EXPECT_EQ(hits[1].title, "B");
  EXPECT_EQ(vaultindex::backlinks(INDEX, "/V", "/V/A.md", m.fs()).size(), 1u);
}

TEST(VaultIndex, BacklinksByRelativePathAndGreekCase) {
  MemFs m;
  m.dirs = {"/V", "/V/Deep", "/V/Other", "/V/Other/Deep"};
  m.files["/V/Deep/Note.md"] = "x";
  m.files["/V/Other/Deep/Note.md"] = "y";
  m.files["/V/Other/Sib.md"] = "Up: [[../Deep/Note]] and [[./Deep/Note]].";  // the second is Other/Deep/Note
  m.files["/V/Root.md"] = "[[Deep/Note]] from the root, and [[ΚΑΛΗΜΕΡΑ]].";
  m.files["/V/Καλημέρα.md"] = "z";
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  auto hits = vaultindex::backlinks(INDEX, "/V", "/V/Deep/Note.md", m.fs());
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].title, "Root");
  EXPECT_EQ(hits[1].title, "Sib");
  hits = vaultindex::backlinks(INDEX, "/V", "/V/Other/Deep/Note.md", m.fs());
  ASSERT_EQ(hits.size(), 1u);  // [[Deep/Note]] from the root is the other one
  EXPECT_EQ(hits[0].title, "Sib");
  EXPECT_EQ(vaultindex::backlinks(INDEX, "/V", "/V/Καλημέρα.md", m.fs()).size(), 1u);
}

TEST(VaultIndex, BacklinksByTheEndOfAPath) {
  MemFs m;
  m.dirs = {"/V", "/V/Notes", "/V/Notes/People", "/V/Deep"};
  m.files["/V/Notes/People/Nikos.md"] = "x";
  m.files["/V/Deep/Note.md"] = "y";
  m.files["/V/Welcome.md"] = "Part of a path: [[People/Nikos]], and [[Deep/Note]].";
  m.files["/V/Other.md"] = "[[Notes/People/Nikos]] in full, [[ople/Nikos]] is not a folder.";
  m.files["/V/Shadow.md"] = "[[Note]]";
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  auto hits = vaultindex::backlinks(INDEX, "/V", "/V/Notes/People/Nikos.md", m.fs());
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].title, "Other");
  EXPECT_EQ(hits[1].title, "Welcome");
}

TEST(VaultIndex, HeadingsAreIndexedAndMatched) {
  MemFs m;
  m.dirs = {"/V"};
  m.files["/V/Kitchen.md"] =
      "# Kitchen\n\nintro\n\n## Bread recipes ##\n\nflour\n\n```\n## Not a heading\n```\n\n### Ψωμί με προζύμι\ntext\n";
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  auto hits = vaultindex::search(INDEX, "bread", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].heading, "Bread recipes");
  hits = vaultindex::search(INDEX, "προζυμι", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].heading, "Ψωμί με προζύμι");
  // Words found only in the body (or a code block) open the note at its start.
  EXPECT_EQ(vaultindex::search(INDEX, "flour", m.fs())[0].heading, "");
  EXPECT_EQ(vaultindex::search(INDEX, "not heading", m.fs()).size(), 1u);
  EXPECT_EQ(vaultindex::search(INDEX, "not heading", m.fs())[0].heading, "");
  // The title matching every word wins over its own "# Kitchen" heading.
  EXPECT_EQ(vaultindex::search(INDEX, "kitchen", m.fs())[0].heading, "");
}

TEST(VaultIndex, BlockIdsAreIndexedAndMatched) {
  MemFs m;
  m.dirs = {"/V"};
  m.files["/V/Quotes.md"] =
      "Some text\n\nA line to remember ^keeper\n\n| a | b |\n|---|---|\n^table-2\n\n```\nnot ^code\n```\n"
      "x^notid\n\n## Title line ^headid\n";
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  auto hits = vaultindex::search(INDEX, "keeper", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].heading, "^keeper");
  hits = vaultindex::search(INDEX, "^table-2", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].heading, "^table-2");
  hits = vaultindex::search(INDEX, "code", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].heading, "");
  hits = vaultindex::search(INDEX, "notid", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].heading, "");
  // A heading keeps its text as the anchor, an id at its end doesn't make it a block.
  hits = vaultindex::search(INDEX, "headid", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].heading, "Title line ^headid");
}

TEST(VaultIndex, HeadingsRoundTrip) {
  vaultindex::Note n = vaultindex::scanNote("/V/A.md", "## One\n#tag not heading\n###### Six\n####### seven\n");
  EXPECT_EQ(n.headings, (std::vector<std::string>{"One", "Six"}));
  vaultindex::Note back;
  ASSERT_TRUE(vaultindex::fromLine(vaultindex::toLine(n), back));
  EXPECT_EQ(back.headings, n.headings);
  EXPECT_EQ(back.terms, n.terms);
}

// The measured case: Search opened on a vault where nothing changed. No note is read and nothing
// is written; with stamps from the folder listing no note is opened by path either.
TEST(VaultIndex, UnchangedVaultIsOnlyWalked) {
  auto m = demo();
  m.stampedListing = true;
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_TRUE(stats.rewritten);
  EXPECT_FALSE(stats.unchanged);
  EXPECT_EQ(m.stamps, 0);
  EXPECT_TRUE(m.files.count(vaultindex::statePathFor("/V")));
  const std::string before = m.files[INDEX];

  m.resetCost();
  int updates = 0;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats, [&](size_t, size_t total) { updates += total > 0; }));
  EXPECT_TRUE(stats.unchanged);
  EXPECT_FALSE(stats.rewritten);
  EXPECT_EQ(stats.notes, 3u);
  EXPECT_EQ(stats.rescanned, 0u);
  EXPECT_EQ(m.stamps, 0);
  EXPECT_EQ(m.reads, 0);
  EXPECT_TRUE(m.written.empty());
  EXPECT_EQ(updates, 0);  // no popup
  EXPECT_EQ(m.files[INDEX], before);
}

// Without stamps in the listing (the host fake's default) the result is the same.
TEST(VaultIndex, UnchangedVaultWithoutStampedListing) {
  auto m = demo();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  m.resetCost();
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_TRUE(stats.unchanged);
  EXPECT_EQ(m.reads, 0);
  EXPECT_TRUE(m.written.empty());
}

// Every kind of change gets past the fingerprint.
TEST(VaultIndex, FingerprintCatchesEveryChange) {
  auto m = demo();
  m.stampedListing = true;
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  auto rebuild = [&] {
    m.resetCost();
    EXPECT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  };

  m.files["/V/Books/Shopping.md"] = "milk, bread, eggs and coffee";  // a note grew
  rebuild();
  EXPECT_FALSE(stats.unchanged);
  EXPECT_EQ(stats.rescanned, 1u);
  EXPECT_EQ(m.reads, 1);
  EXPECT_EQ(vaultindex::search(INDEX, "coffee", m.fs()).size(), 1u);

  m.files["/V/Books/Shopping.md"] = "milk, bread, eggs and toffee";  // same length, newer time
  ++m.edits["/V/Books/Shopping.md"];
  rebuild();
  EXPECT_EQ(stats.rescanned, 1u);
  EXPECT_EQ(vaultindex::search(INDEX, "toffee", m.fs()).size(), 1u);
  EXPECT_TRUE(vaultindex::search(INDEX, "coffee", m.fs()).empty());

  m.files["/V/New.md"] = "brand new";  // a note appeared
  rebuild();
  EXPECT_EQ(stats.rescanned, 1u);
  EXPECT_EQ(vaultindex::search(INDEX, "brand", m.fs()).size(), 1u);

  m.files.erase("/V/New.md");  // and went: nothing to read, but its line must go
  rebuild();
  EXPECT_FALSE(stats.unchanged);
  EXPECT_TRUE(stats.rewritten);
  EXPECT_EQ(stats.rescanned, 0u);
  EXPECT_EQ(m.reads, 0);
  EXPECT_TRUE(vaultindex::search(INDEX, "brand", m.fs()).empty());

  m.files["/V/Moved.md"] = m.files["/V/Welcome.md"];  // renamed
  m.files.erase("/V/Welcome.md");
  rebuild();
  EXPECT_EQ(stats.rescanned, 1u);
  const auto hits = vaultindex::search(INDEX, "guide", m.fs());
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].path, "/V/Moved.md");

  rebuild();  // and then it is quiet again
  EXPECT_TRUE(stats.unchanged);
}

// A picture added to the vault changes the file list but no note: the index file is left alone.
TEST(VaultIndex, OtherFilesRefreshOnlyTheFileList) {
  auto m = demo();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  m.files["/V/Books/cover.png"] = "png";
  m.resetCost();
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_FALSE(stats.unchanged);
  EXPECT_FALSE(stats.rewritten);
  EXPECT_EQ(m.reads, 0);
  EXPECT_EQ(m.written, (std::vector<std::string>{vaultindex::fileListPathFor("/V"), vaultindex::statePathFor("/V")}));
  EXPECT_NE(m.files[vaultindex::fileListPathFor("/V")].find("/V/Books/cover.png\n"), std::string::npos);
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_TRUE(stats.unchanged);
}

// The fingerprint is trusted only while the files it stands for are there and whole.
TEST(VaultIndex, MissingPiecesAreRebuilt) {
  auto m = demo();
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));

  m.files.erase(vaultindex::fileListPathFor("/V"));  // dropped when a note is made or renamed on the device
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_FALSE(stats.unchanged);
  EXPECT_FALSE(stats.rewritten);
  EXPECT_TRUE(m.files.count(vaultindex::fileListPathFor("/V")));

  m.files.erase(INDEX);  // Settings: rebuild the search index
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.rescanned, 3u);

  m.files[INDEX] = "vaultink-index 0\n";  // an older firmware's index
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.rescanned, 3u);
  EXPECT_EQ(vaultindex::search(INDEX, "milk", m.fs()).size(), 1u);

  // An index from before the state file existed: checked once, not read again, then quiet.
  m.files.erase(vaultindex::statePathFor("/V"));
  m.resetCost();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_FALSE(stats.unchanged);
  EXPECT_FALSE(stats.rewritten);
  EXPECT_EQ(m.reads, 0);
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_TRUE(stats.unchanged);
}

// A note that couldn't be read is tried again by every build until it works.
TEST(VaultIndex, UnreadNoteIsRetried) {
  auto m = demo();
  m.unreadable.insert("/V/Books/Shopping.md");
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.rescanned, 2u);
  EXPECT_FALSE(m.files.count(vaultindex::statePathFor("/V")));
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_FALSE(stats.unchanged);
  m.unreadable.clear();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.rescanned, 1u);
  EXPECT_EQ(vaultindex::search(INDEX, "milk", m.fs()).size(), 1u);
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_TRUE(stats.unchanged);
}

// A damaged index (the same note twice, a line that isn't one) is written clean.
TEST(VaultIndex, DamagedLinesAreDropped) {
  auto m = demo();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs()));
  const std::string good = m.files[INDEX];
  const size_t firstNote = good.find('\n') + 1;
  m.files[INDEX] = good + good.substr(firstNote, good.find('\n', firstNote) + 1 - firstNote) + "not a line\n";
  m.files.erase(vaultindex::statePathFor("/V"));
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_TRUE(stats.rewritten);
  EXPECT_EQ(stats.rescanned, 0u);
  EXPECT_EQ(std::count(m.files[INDEX].begin(), m.files[INDEX].end(), '\n'), 4);  // header and 3 notes
}

// A made-up vault of 600 notes: an unchanged build walks it without reading or writing, and one
// edited note is the only one read.
TEST(VaultIndex, BigVaultStaysCheap) {
  MemFs m;
  m.stampedListing = true;
  m.dirs = {"/V"};
  for (int d = 0; d < 20; ++d) {
    const std::string dir = "/V/Folder " + std::to_string(d);
    m.dirs.insert(dir);
    for (int n = 0; n < 30; ++n) {
      m.files[dir + "/Note " + std::to_string(n) + ".md"] = "word" + std::to_string(d * 30 + n) + " shared text";
    }
  }
  vaultindex::BuildStats stats;
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.rescanned, 600u);
  m.resetCost();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_TRUE(stats.unchanged);
  EXPECT_EQ(m.stamps + m.reads, 0);
  EXPECT_TRUE(m.written.empty());

  m.files["/V/Folder 7/Note 3.md"] = "changed entirely";
  m.resetCost();
  ASSERT_TRUE(vaultindex::build("/V", INDEX, m.fs(), &stats));
  EXPECT_EQ(stats.notes, 600u);
  EXPECT_EQ(stats.rescanned, 1u);
  EXPECT_EQ(m.reads, 1);
  EXPECT_EQ(vaultindex::search(INDEX, "entirely", m.fs()).size(), 1u);
  EXPECT_EQ(vaultindex::search(INDEX, "word599", m.fs()).size(), 1u);
  EXPECT_TRUE(vaultindex::search(INDEX, "word213", m.fs()).empty());
}

// Terms stay unique and in first-seen order, and stop at the cap.
TEST(VaultIndex, TermsAreUniqueInOrderAndCapped) {
  const auto note = vaultindex::scanNote("/V/Zeta.md", "beta alpha beta gamma alpha zeta");
  EXPECT_EQ(note.terms, (std::vector<std::string>{"zeta", "beta", "alpha", "gamma"}));
  std::string big;
  for (int i = 0; i < 4000; ++i) big += "w" + std::to_string(i % 3000) + " ";
  const auto capped = vaultindex::scanNote("/V/Big.md", big);
  EXPECT_EQ(capped.terms.size(), vaultindex::MAX_TERMS_PER_NOTE);
  EXPECT_EQ(std::set<std::string>(capped.terms.begin(), capped.terms.end()).size(), capped.terms.size());
}
