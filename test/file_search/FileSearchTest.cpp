#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "FileSearch.h"

namespace {

// A card in a map: folder path -> entries.
class FakeCard : public filesearch::DirectorySource {
 public:
  std::map<std::string, std::vector<filesearch::Entry>> folders;
  int listCalls = 0;

  void file(const std::string& folder, const std::string& name) { folders[folder].push_back({name, false}); }
  void dir(const std::string& folder, const std::string& name) {
    folders[folder].push_back({name, true});
    folders.try_emplace(folder == "/" ? "/" + name : folder + "/" + name);
  }

  bool list(const std::string& path, std::vector<filesearch::Entry>& out) override {
    ++listCalls;
    const auto it = folders.find(path);
    if (it == folders.end()) return false;
    out = it->second;
    return true;
  }
};

std::vector<filesearch::Hit> runAll(filesearch::Walker& walker) {
  while (walker.step()) {
  }
  return walker.results();
}

}  // namespace

TEST(FileSearch, FoldsCaseAndAccents) {
  FakeCard card;
  card.dir("/", "Ελληνικά");
  card.file("/Ελληνικά", "Καλημέρα.epub");
  card.file("/Ελληνικά", "ΚΑΛΗΜΕΡΑ ΚΟΣΜΕ.txt");
  card.file("/", "Odyssey.epub");
  filesearch::Walker walker(card, "/", "καλημερα", false);
  const auto hits = runAll(walker);
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].name, "Καλημέρα.epub");
  EXPECT_EQ(hits[0].folder, "/Ελληνικά");
  EXPECT_FALSE(hits[0].isDirectory);
  EXPECT_EQ(hits[1].name, "ΚΑΛΗΜΕΡΑ ΚΟΣΜΕ.txt");
}

TEST(FileSearch, DecomposedNameMatchesComposedQuery) {
  FakeCard card;
  card.file("/", "Cafe\xCC\x81.md");  // e + combining acute, as macOS writes it
  filesearch::Walker walker(card, "/", "CAF\xC3\x89", false);
  const auto hits = runAll(walker);
  ASSERT_EQ(hits.size(), 1u);
}

TEST(FileSearch, MatchesFoldersAndAnyExtension) {
  FakeCard card;
  card.dir("/", "Classics");
  card.file("/Classics", "classics-notes.bin");
  card.file("/Classics", "Other.epub");
  filesearch::Walker walker(card, "/", "classic", false);
  const auto hits = runAll(walker);
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].name, "Classics");
  EXPECT_TRUE(hits[0].isDirectory);
  EXPECT_EQ(hits[0].folder, "/");
  EXPECT_EQ(hits[1].name, "classics-notes.bin");
  EXPECT_EQ(hits[1].folder, "/Classics");
}

TEST(FileSearch, AFileFilterDropsFilesButNotFolders) {
  FakeCard card;
  card.dir("/", "Books");
  card.file("/Books", "Books-1.epub");
  card.file("/Books", "Books-2.pdf");
  filesearch::Walker walker(card, "/", "books", false);
  walker.setFileFilter(
      [](const std::string& name) { return name.size() > 4 && name.substr(name.size() - 4) == ".pdf"; });
  const auto hits = runAll(walker);
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].name, "Books");  // a folder always stays
  EXPECT_EQ(hits[1].name, "Books-2.pdf");
}

TEST(FileSearch, StepsOneFolderAtATime) {
  FakeCard card;
  card.dir("/", "A");
  card.dir("/", "B");
  card.file("/A", "x.txt");
  card.file("/B", "x.txt");
  filesearch::Walker walker(card, "/", "x", false);
  EXPECT_TRUE(walker.step());  // root
  EXPECT_EQ(card.listCalls, 1);
  EXPECT_EQ(walker.results().size(), 0u);
  EXPECT_TRUE(walker.step());  // A (listing order)
  EXPECT_EQ(walker.results().size(), 1u);
  EXPECT_EQ(walker.results()[0].folder, "/A");
  EXPECT_FALSE(walker.step());  // B, the last
  EXPECT_TRUE(walker.done());
  EXPECT_EQ(walker.results().size(), 2u);
  EXPECT_EQ(walker.foldersRead(), 3u);
  EXPECT_FALSE(walker.step());
}

TEST(FileSearch, SkipsHiddenAndSystemVolumeInformation) {
  FakeCard card;
  card.dir("/", ".crosspoint");
  card.file("/.crosspoint", "match.idx");
  card.dir("/", "System Volume Information");
  card.file("/System Volume Information", "match.dat");
  card.file("/", ".match-hidden");
  card.file("/", "match.txt");
  filesearch::Walker walker(card, "/", "match", false);
  const auto hits = runAll(walker);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].name, "match.txt");

  filesearch::Walker shown(card, "/", "match", true);
  const auto shownHits = runAll(shown);
  EXPECT_EQ(shownHits.size(), 3u);  // the dot entries now count; the system folder never does
}

TEST(FileSearch, DepthCapped) {
  FakeCard card;
  std::string folder = "/";
  for (int depth = 1; depth <= 15; ++depth) {
    const std::string name = "d" + std::to_string(depth);
    card.dir(folder, name);
    folder = folder == "/" ? "/" + name : folder + "/" + name;
    card.file(folder, "leaf.txt");
  }
  filesearch::Walker walker(card, "/", "leaf", false);
  const auto hits = runAll(walker);
  EXPECT_EQ(hits.size(), static_cast<size_t>(filesearch::Walker::MAX_DEPTH));
  EXPECT_EQ(hits.back().folder.find("/d12"), hits.back().folder.size() - 4);
}

TEST(FileSearch, StartsFromTheBrowsedFolder) {
  FakeCard card;
  card.file("/", "a.txt");
  card.dir("/", "Books");
  card.file("/Books", "a.txt");
  card.dir("/Books", "Sub");
  card.file("/Books/Sub", "a.txt");
  filesearch::Walker walker(card, "/Books/", "a", false);  // a trailing slash is fine
  const auto hits = runAll(walker);
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].folder, "/Books");
  EXPECT_EQ(hits[1].folder, "/Books/Sub");
}

TEST(FileSearch, CapsHitsAndErases) {
  FakeCard card;
  for (size_t i = 0; i < filesearch::Walker::MAX_HITS + 50; ++i) card.file("/", "n" + std::to_string(i));
  filesearch::Walker walker(card, "/", "n", false);
  runAll(walker);
  EXPECT_EQ(walker.results().size(), filesearch::Walker::MAX_HITS);
  EXPECT_TRUE(walker.done());
  walker.erase(0);
  EXPECT_EQ(walker.results().size(), filesearch::Walker::MAX_HITS - 1);
  EXPECT_EQ(walker.results()[0].name, "n1");
  walker.erase(9999);  // out of range: nothing happens
  EXPECT_EQ(walker.results().size(), filesearch::Walker::MAX_HITS - 1);
}

TEST(FileSearch, EmptyQueryAndUnreadableFolder) {
  FakeCard card;
  card.file("/", "a.txt");
  filesearch::Walker spaces(card, "/", "   ", false);
  EXPECT_TRUE(runAll(spaces).empty());  // spaces are a query like any other; nothing has them
  filesearch::Walker blank(card, "/", "", false);
  EXPECT_TRUE(blank.done());
  EXPECT_FALSE(blank.step());

  filesearch::Walker missing(card, "/nowhere", "a", false);
  EXPECT_FALSE(missing.step());
  EXPECT_TRUE(missing.results().empty());
}
