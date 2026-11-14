#include <gtest/gtest.h>

#include <string>

#include "src/util/FailedKeys.h"

namespace {

// The Gallery's use: decode only what is not known to fail, remember a failure.
struct Page {
  FailedKeys failed;
  int decodes = 0;
  int secondRefreshes = 0;

  // One paint of a page holding one broken picture, the way GalleryActivity::render walks it.
  void paint(const std::string& key) {
    const bool waiting = !failed.contains(key);  // first pass: left for after the first refresh?
    if (!waiting) return;
    decodes++;
    failed.add(key);  // the decode fails
    secondRefreshes++;
  }
};

TEST(FailedKeys, ABrokenPictureIsDecodedOnceNotOnEveryPaint) {
  Page page;
  for (int i = 0; i < 5; i++) page.paint("/.crosspoint/thumbs/1_200_150x100.pxc");
  EXPECT_EQ(page.decodes, 1);
  EXPECT_EQ(page.secondRefreshes, 1);
}

TEST(FailedKeys, AReplacedFileHasAnotherKeyAndIsTriedAgain) {
  Page page;
  page.paint("/.crosspoint/thumbs/1_200_150x100.pxc");
  page.paint("/.crosspoint/thumbs/1_999_150x100.pxc");  // same path, new size
  EXPECT_EQ(page.decodes, 2);
  EXPECT_EQ(page.failed.size(), 2u);
}

TEST(FailedKeys, ClearGivesEverythingOneMoreTry) {
  Page page;
  page.paint("a");
  page.failed.clear();
  page.paint("a");
  EXPECT_EQ(page.decodes, 2);
}

TEST(FailedKeys, HoldsABoundedNumberAndDoesNotRepeatAKey) {
  FailedKeys failed;
  failed.add("x");
  failed.add("x");
  EXPECT_EQ(failed.size(), 1u);
  for (size_t i = 0; i < FailedKeys::CAPACITY; i++) failed.add(std::to_string(i));
  EXPECT_EQ(failed.size(), FailedKeys::CAPACITY);
  EXPECT_FALSE(failed.contains("x"));  // the oldest went
  EXPECT_TRUE(failed.contains("0"));
  EXPECT_FALSE(failed.contains("never added"));
}

}  // namespace
