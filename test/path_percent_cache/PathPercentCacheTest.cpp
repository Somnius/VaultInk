#include <gtest/gtest.h>

#include <string>

#include "src/util/PathPercentCache.h"

namespace {

struct Loader {
  int calls = 0;
  int value = 42;
  int operator()(const std::string&) {
    calls++;
    return value;
  }
};

TEST(PathPercentCache, LoadsABookOnceAndAnswersFromMemoryAfterwards) {
  PathPercentCache cache;
  Loader loader;
  bool skipped = false;
  EXPECT_EQ(cache.get("/books/a.epub", false, skipped, loader), 42);
  loader.value = 77;  // the book is not read again while the list is on screen
  EXPECT_EQ(cache.get("/books/a.epub", false, skipped, loader), 42);
  EXPECT_EQ(loader.calls, 1);
  EXPECT_FALSE(skipped);
  EXPECT_EQ(cache.get("/books/b.epub", false, skipped, loader), 77);
  EXPECT_EQ(loader.calls, 2);
}

TEST(PathPercentCache, DeferredLookupLoadsNothingAndSaysSo) {
  PathPercentCache cache;
  Loader loader;
  bool skipped = false;
  EXPECT_EQ(cache.get("/books/a.mobi", true, skipped, loader), -1);
  EXPECT_TRUE(skipped);
  EXPECT_EQ(loader.calls, 0);
  EXPECT_EQ(cache.size(), 0u);  // the -1 was not kept: the next paint loads the real value

  skipped = false;
  EXPECT_EQ(cache.get("/books/a.mobi", false, skipped, loader), 42);
  EXPECT_FALSE(skipped);
  // Once known, a deferred paint shows it too.
  EXPECT_EQ(cache.get("/books/a.mobi", true, skipped, loader), 42);
  EXPECT_FALSE(skipped);
  EXPECT_EQ(loader.calls, 1);
}

TEST(PathPercentCache, KeepsABookWithoutAPercentSoItIsNotAskedAgain) {
  PathPercentCache cache;
  Loader loader;
  loader.value = -1;
  bool skipped = false;
  EXPECT_EQ(cache.get("/books/new.fb2", false, skipped, loader), -1);
  EXPECT_EQ(cache.get("/books/new.fb2", false, skipped, loader), -1);
  EXPECT_EQ(loader.calls, 1);
}

TEST(PathPercentCache, ClearForgetsAndTheOldestGoesFirstWhenFull) {
  PathPercentCache cache;
  Loader loader;
  bool skipped = false;
  for (size_t i = 0; i < PathPercentCache::CAPACITY + 1; i++) {
    cache.get("/books/" + std::to_string(i), false, skipped, loader);
  }
  EXPECT_EQ(cache.size(), PathPercentCache::CAPACITY);
  const int before = loader.calls;
  cache.get("/books/1", false, skipped, loader);  // still held
  EXPECT_EQ(loader.calls, before);
  cache.get("/books/0", false, skipped, loader);  // pushed out, read again
  EXPECT_EQ(loader.calls, before + 1);

  cache.clear();
  EXPECT_EQ(cache.size(), 0u);
  cache.get("/books/1", false, skipped, loader);
  EXPECT_EQ(loader.calls, before + 2);
}

TEST(PathPercentCache, ClampsWhatTheLoaderReturns) {
  PathPercentCache cache;
  Loader loader;
  bool skipped = false;
  loader.value = 250;
  EXPECT_EQ(cache.get("/a", false, skipped, loader), 100);
  loader.value = -9;
  EXPECT_EQ(cache.get("/b", false, skipped, loader), -1);
}

}  // namespace
