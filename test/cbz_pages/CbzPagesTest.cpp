#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CbzPages.h"

using Strings = std::vector<std::string>;

TEST(CbzPagesTest, NaturalOrderAndCase) {
  EXPECT_TRUE(cbz::naturalLess("page2.jpg", "page10.jpg"));
  EXPECT_FALSE(cbz::naturalLess("page10.jpg", "page2.jpg"));
  EXPECT_TRUE(cbz::naturalLess("Page01.jpg", "page2.jpg"));
  EXPECT_TRUE(cbz::naturalLess("a.jpg", "B.jpg"));
  EXPECT_TRUE(cbz::naturalLess("p9", "p9a"));
  const auto book = cbz::pages({"10.jpg", "2.png", "1.JPG", "003.jpeg"});
  EXPECT_EQ(book.pages, (Strings{"1.JPG", "2.png", "003.jpeg", "10.jpg"}));
  EXPECT_TRUE(book.chapters.empty());
}

TEST(CbzPagesTest, SkipsJunkAndCountsOtherFormats) {
  const auto book = cbz::pages({"__MACOSX/._1.jpg", "._2.jpg", "Thumbs.db", ".hidden.jpg", "ComicInfo.xml", "folder/",
                                "../evil.jpg", "/abs.jpg", "a/../b.jpg", "cover.webp", "x.gif", "1.jpg"});
  EXPECT_EQ(book.pages, (Strings{"1.jpg"}));
  EXPECT_EQ(book.skipped, 2u);
}

TEST(CbzPagesTest, FoldersAreChaptersInNaturalOrder) {
  const auto book = cbz::pages({"Vol 1/Ch 10/01.jpg", "Vol 1/Ch 2/02.jpg", "Vol 1/Ch 2/01.jpg", "Vol 1/Ch 10/02.jpg"});
  EXPECT_EQ(book.pages,
            (Strings{"Vol 1/Ch 2/01.jpg", "Vol 1/Ch 2/02.jpg", "Vol 1/Ch 10/01.jpg", "Vol 1/Ch 10/02.jpg"}));
  ASSERT_EQ(book.chapters.size(), 2u);
  EXPECT_EQ(book.chapters[0].title, "Ch 2");
  EXPECT_EQ(book.chapters[0].firstPage, 0u);
  EXPECT_EQ(book.chapters[1].title, "Ch 10");
  EXPECT_EQ(book.chapters[1].firstPage, 2u);
}

TEST(CbzPagesTest, OneFolderIsOneChapter) {
  const auto book = cbz::pages({"Issue 1/2.jpg", "Issue 1/1.jpg"});
  EXPECT_EQ(book.pages, (Strings{"Issue 1/1.jpg", "Issue 1/2.jpg"}));
  ASSERT_EQ(book.chapters.size(), 1u);
  EXPECT_EQ(book.chapters[0].title, "Issue 1");
}

TEST(CbzPagesTest, EmptyOrOnlyJunk) {
  EXPECT_TRUE(cbz::pages({}).pages.empty());
  EXPECT_TRUE(cbz::pages({"__MACOSX/a.jpg", "notes.txt"}).pages.empty());
}

TEST(CbzPagesTest, FindsComicInfoAndReadsTheMangaFlag) {
  EXPECT_EQ(cbz::pages({"ComicInfo.xml", "1.jpg"}).comicInfo, "ComicInfo.xml");
  EXPECT_EQ(cbz::pages({"COMICINFO.XML", "1.jpg"}).comicInfo, "COMICINFO.XML");
  EXPECT_TRUE(cbz::pages({"1.jpg"}).comicInfo.empty());
  EXPECT_TRUE(cbz::mangaRightToLeft("<ComicInfo><Manga>YesAndRightToLeft</Manga></ComicInfo>"));
  EXPECT_TRUE(cbz::mangaRightToLeft("<comicinfo><manga> yesandrighttoleft </manga></comicinfo>"));
  EXPECT_FALSE(cbz::mangaRightToLeft("<ComicInfo><Manga>Yes</Manga></ComicInfo>"));
  EXPECT_FALSE(cbz::mangaRightToLeft("<ComicInfo><Manga>No</Manga></ComicInfo>"));
  EXPECT_FALSE(cbz::mangaRightToLeft("<ComicInfo><Title>x</Title></ComicInfo>"));
  EXPECT_FALSE(cbz::mangaRightToLeft("<Manga>YesAndRightToLeft"));
}
