#include <gtest/gtest.h>

#include <algorithm>

#include "FileSort.h"

using filesort::Entry;
using filesort::Mode;

namespace {
std::vector<std::string> sorted(const Mode mode, std::vector<Entry> entries) {
  std::sort(entries.begin(), entries.end(),
            [mode](const Entry& a, const Entry& b) { return filesort::before(mode, a, b); });
  std::vector<std::string> names;
  for (const auto& e : entries) names.push_back(e.name);
  return names;
}
std::vector<Entry> sample() {
  return {{"b.epub", 300, 20260102},
          {"a.epub", 100, 20260105},
          {"Docs/", 0, 20250101},
          {"c.epub", 200, 20260101},
          {"Art/", 0, 20260301}};
}
}  // namespace

TEST(FileSort, FoldersFirstInEveryMode) {
  for (int m = 0; m < filesort::COUNT; ++m) {
    const auto names = sorted(static_cast<Mode>(m), sample());
    EXPECT_EQ(names[0].back(), '/');
    EXPECT_EQ(names[1].back(), '/');
  }
}

TEST(FileSort, NameBothWays) {
  EXPECT_EQ(sorted(filesort::NAME_AZ, sample()),
            (std::vector<std::string>{"Art/", "Docs/", "a.epub", "b.epub", "c.epub"}));
  EXPECT_EQ(sorted(filesort::NAME_ZA, sample()),
            (std::vector<std::string>{"Docs/", "Art/", "c.epub", "b.epub", "a.epub"}));
}

TEST(FileSort, DateOrdersFoldersToo) {
  EXPECT_EQ(sorted(filesort::NEWEST, sample()),
            (std::vector<std::string>{"Art/", "Docs/", "a.epub", "b.epub", "c.epub"}));
  EXPECT_EQ(sorted(filesort::OLDEST, sample()),
            (std::vector<std::string>{"Docs/", "Art/", "c.epub", "b.epub", "a.epub"}));
}

TEST(FileSort, SizeOrdersFilesAndKeepsFoldersByName) {
  EXPECT_EQ(sorted(filesort::LARGEST, sample()),
            (std::vector<std::string>{"Art/", "Docs/", "b.epub", "c.epub", "a.epub"}));
  EXPECT_EQ(sorted(filesort::SMALLEST, sample()),
            (std::vector<std::string>{"Art/", "Docs/", "a.epub", "c.epub", "b.epub"}));
}

TEST(FileSort, RecentlyOpenedPutsUnopenedLastByName) {
  auto entries = sample();
  entries[0].recentRank = 1;  // b.epub
  entries[3].recentRank = 0;  // c.epub
  EXPECT_EQ(sorted(filesort::RECENTLY_OPENED, entries),
            (std::vector<std::string>{"Art/", "Docs/", "c.epub", "b.epub", "a.epub"}));
}

TEST(FileSort, NaturalNumbersWithinAMode) {
  EXPECT_EQ(sorted(filesort::NAME_AZ, {{"10.epub"}, {"2.epub"}, {"1.epub"}}),
            (std::vector<std::string>{"1.epub", "2.epub", "10.epub"}));
}
