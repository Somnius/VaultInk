#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "FsHelpers.h"

namespace {

using namespace std::string_view_literals;

TEST(ReflowableBookExtension, IncludesTextFormatsAndExcludesImageBooks) {
  for (const auto path : {"/Books/book.epub"sv, "book.TXT"sv, "book.mD"sv, "book.mobi"sv, "book.FB2"sv, "page.html"sv,
                          "page.HTM"sv, "page.xhtml"sv, "letter.Rtf"sv}) {
    EXPECT_TRUE(FsHelpers::hasReflowableBookExtension(path));
  }
  for (const auto path : {"book.xtc"sv, "book.XTCH"sv, "book.bmp"sv, "book.epub.bak"sv, ""sv}) {
    EXPECT_FALSE(FsHelpers::hasReflowableBookExtension(path));
  }
}

TEST(IsSafePathComponent, AcceptsNamesWithRepeatedDots) {
  EXPECT_TRUE(FsHelpers::isSafePathComponent("volume..2.epub"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent("notes...txt"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent(".hidden"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent("a.b"sv));
  EXPECT_TRUE(FsHelpers::isSafePathComponent("book.epub"sv));
}

TEST(IsSafePathComponent, RejectsEmptyAndExactDotComponents) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent(""sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("."sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent(".."sv));
}

TEST(IsSafePathComponent, RejectsPathSeparatorsAnywhereInTheComponent) {
  EXPECT_FALSE(FsHelpers::isSafePathComponent("a/b"sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("a\\b"sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("../x"sv));
  EXPECT_FALSE(FsHelpers::isSafePathComponent("x/.."sv));
}

TEST(NormalisePath, CollapsesParentReferenceWithinPath) {
  EXPECT_EQ(FsHelpers::normalisePath("/Books/../.crosspoint/x"), ".crosspoint/x");
}

TEST(NormalisePath, DropsLeadingParentReferencesPastRoot) { EXPECT_EQ(FsHelpers::normalisePath("/../../etc"), "etc"); }

// Sanitizes into a buffer of `size` bytes (at most 64, the size ScreenshotUtil uses).
std::string sanitize(const char* input, const size_t size = 64) {
  char out[64];
  FsHelpers::sanitizePathComponentForFat32(input, out, size);
  return out;
}

// Book titles from the EPUBs attached to #2103 and #2199.
constexpr char kTitle2103[] = "Богиня глюкозы. Нормализуйте уровень сахара в крови, чтобы изменить свою жизнь";
constexpr char kTitle2199[] = "Вглядываясь в солнце. Жизнь без страха смерти";

TEST(SanitizePathComponentForFat32, KeepsTitleThatFits) {
  EXPECT_EQ(sanitize("Эдем (полный перевод)"), "Эдем-(полный-перевод)");
}

// The readers copy the title into ScreenshotInfo::title (char[64]) with snprintf, which can
// end the copy partway through a Cyrillic letter.
TEST(SanitizePathComponentForFat32, DropsLetterCutOffByCaller) {
  char title[64];
  snprintf(title, sizeof(title), "%s", kTitle2103);
  EXPECT_EQ(sanitize(title), "Богиня-глюкозы.-Нормализуйте-уров");
  snprintf(title, sizeof(title), "%s", kTitle2199);
  EXPECT_EQ(sanitize(title), "Вглядываясь-в-солнце.-Жизнь-без-ст");
}

TEST(SanitizePathComponentForFat32, DoesNotSplitLetterAtBufferLimit) {
  // Each letter of "Жизнь" is 2 bytes. 7 bytes of room holds "Жиз" and half of "н".
  EXPECT_EQ(sanitize("Жизнь", 8), "Жиз");
  EXPECT_EQ(sanitize(kTitle2103), "Богиня-глюкозы.-Нормализуйте-уров");
}

TEST(MarkupBookExtension, Fb2HtmlRtf) {
  EXPECT_TRUE(FsHelpers::hasFb2Extension("a.fb2"));
  EXPECT_FALSE(FsHelpers::hasFb2Extension("a.fb2.zip"));  // the zipped kind is not read
  for (const auto path : {"a.html"sv, "a.htm"sv, "a.XHTML"sv}) EXPECT_TRUE(FsHelpers::hasHtmlExtension(path));
  EXPECT_FALSE(FsHelpers::hasHtmlExtension("a.mhtml.txt"));
  EXPECT_TRUE(FsHelpers::hasRtfExtension("a.RTF"));
  for (const auto path : {"a.fb2"sv, "a.html"sv, "a.htm"sv, "a.xhtml"sv, "a.rtf"sv}) {
    EXPECT_TRUE(FsHelpers::hasMarkupBookExtension(path));
  }
  for (const auto path : {"a.epub"sv, "a.txt"sv, "a.md"sv, "a.mobi"sv, "a.doc"sv, "rtf"sv}) {
    EXPECT_FALSE(FsHelpers::hasMarkupBookExtension(path));
  }
}

}  // namespace

TEST(RandomPageBook, PicksBookFormatsOnly) {
  for (const char* path :
       {"/Books/a.epub", "/Books/a.EPUB", "/Comics/x.cbz", "/Comics/x.xtch", "/Books/p.pdf", "/Books/novel.txt",
        "/Ελληνικά/βιβλίο.epub", "/Books/a.fb2", "/Books/a.html", "/Books/a.rtf"}) {
    EXPECT_TRUE(FsHelpers::isRandomPageBook(path)) << path;
  }
  for (const char* path : {"/Vaults/V/note.md", "/pic.png", "/a.canvas", "/a.zip", "/Sketch.excalidraw"}) {
    EXPECT_FALSE(FsHelpers::isRandomPageBook(path)) << path;
  }
}

TEST(RandomPageBook, LeavesOutLogsChangelogsAndTheReadersOwnFolders) {
  for (const char* path : {"/debug/crash-2026-11-01.txt", "/Books/Crash report.txt", "/.vaultink/whats-new.md",
                           "/.vaultink/whats-new.txt", "/Books/CHANGELOG.txt", "/Books/run.log", "/.crosspoint/x.txt",
                           "/Books/.hidden/novel.epub", "/Docs/backtrace-1.txt"}) {
    EXPECT_FALSE(FsHelpers::isRandomPageBook(path)) << path;
  }
}
