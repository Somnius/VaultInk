#include <gtest/gtest.h>

#include <string>

#include "RemoteFiles.h"

using remotefiles::Entry;
using remotefiles::Listing;

TEST(RemoteFiles, SafeNameRefusesWhatTheCardOrAPathCannotTake) {
  EXPECT_TRUE(remotefiles::safeName("book.epub"));
  EXPECT_TRUE(remotefiles::safeName("Ανάγνωση (1).epub"));
  EXPECT_FALSE(remotefiles::safeName(""));
  EXPECT_FALSE(remotefiles::safeName("."));
  EXPECT_FALSE(remotefiles::safeName(".."));
  EXPECT_FALSE(remotefiles::safeName("a/b"));
  EXPECT_FALSE(remotefiles::safeName("a\\b"));
  EXPECT_FALSE(remotefiles::safeName("a:b"));
  EXPECT_FALSE(remotefiles::safeName("what?.txt"));
  EXPECT_FALSE(remotefiles::safeName("line\nbreak"));
  EXPECT_FALSE(remotefiles::safeName(std::string("nul\0x", 5)));
  EXPECT_FALSE(remotefiles::safeName("trailing."));
  EXPECT_FALSE(remotefiles::safeName("trailing "));
  EXPECT_FALSE(remotefiles::safeName(std::string(256, 'a')));
  EXPECT_TRUE(remotefiles::safeName(std::string(255, 'a')));
}

TEST(RemoteFiles, ListingStopsAtTheEntryCap) {
  Listing l;
  for (size_t i = 0; i < remotefiles::MAX_ENTRIES; ++i) {
    Entry e;
    e.name = "f" + std::to_string(i);
    EXPECT_TRUE(l.add(e));
  }
  EXPECT_FALSE(l.truncated);
  Entry extra;
  extra.name = "one-more";
  EXPECT_FALSE(l.add(extra));
  EXPECT_TRUE(l.truncated);
  EXPECT_EQ(l.entries.size(), remotefiles::MAX_ENTRIES);
}

TEST(RemoteFiles, ListingStopsAtTheNameByteCap) {
  Listing l;
  size_t added = 0;
  while (!l.truncated && added < remotefiles::MAX_ENTRIES) {
    Entry e;
    e.name = std::string(200, 'n') + std::to_string(added);
    if (l.add(e)) ++added;
  }
  EXPECT_TRUE(l.truncated);
  EXPECT_LT(added, remotefiles::MAX_ENTRIES);
  EXPECT_LE(l.nameBytes, remotefiles::MAX_NAME_BYTES_TOTAL);
}

TEST(RemoteFiles, ListingCountsUnsafeNamesWithoutKeepingThem) {
  Listing l;
  Entry bad;
  bad.name = "..";
  EXPECT_FALSE(l.add(bad));
  EXPECT_EQ(l.skipped, 1u);
  EXPECT_FALSE(l.truncated);
  EXPECT_TRUE(l.entries.empty());
}

TEST(RemoteFiles, SortPutsFoldersFirstThenNamesWithoutCase) {
  std::vector<Entry> v(4);
  v[0].name = "b.txt";
  v[1].name = "Zeta";
  v[1].isDir = true;
  v[2].name = "A.txt";
  v[3].name = "alpha";
  v[3].isDir = true;
  remotefiles::sortEntries(v);
  EXPECT_EQ(v[0].name, "alpha");
  EXPECT_EQ(v[1].name, "Zeta");
  EXPECT_EQ(v[2].name, "A.txt");
  EXPECT_EQ(v[3].name, "b.txt");
}

TEST(RemoteFiles, Paths) {
  EXPECT_EQ(remotefiles::joinPath("/", "a"), "/a");
  EXPECT_EQ(remotefiles::joinPath("/a", "b c"), "/a/b c");
  EXPECT_EQ(remotefiles::parentPath("/a/b"), "/a");
  EXPECT_EQ(remotefiles::parentPath("/a"), "/");
  EXPECT_EQ(remotefiles::parentPath("/"), "/");
  std::string out;
  EXPECT_TRUE(remotefiles::normalizePath("/a//b/./c/../d/", out));
  EXPECT_EQ(out, "/a/b/d");
  EXPECT_TRUE(remotefiles::normalizePath("", out));
  EXPECT_EQ(out, "/");
  EXPECT_FALSE(remotefiles::normalizePath("/a/../../etc", out));
}

TEST(RemoteFiles, EpochAndSizeText) {
  EXPECT_EQ(remotefiles::epochUtc(1970, 1, 1, 0, 0, 0), 0);
  EXPECT_EQ(remotefiles::epochUtc(2026, 1, 12, 10, 0, 0), 1768212000);
  EXPECT_EQ(remotefiles::epochUtc(2020, 2, 29, 12, 34, 56), 1582979696);
  EXPECT_EQ(remotefiles::epochUtc(2026, 13, 1, 0, 0, 0), 0);
  EXPECT_EQ(remotefiles::sizeText(512), "512 B");
  EXPECT_EQ(remotefiles::sizeText(1536), "1.5 KB");
  EXPECT_EQ(remotefiles::sizeText(5ULL * 1024 * 1024), "5.0 MB");
}
