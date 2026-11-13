#include <gtest/gtest.h>

#include <cstring>
#include <utility>
#include <vector>

#include "LibraryFolders.h"
#include "LibraryFormats.h"
#include "LibraryIndexFile.h"

namespace library {

std::string joinLibraryPath(const std::string_view folder, const std::string_view name) {
  return std::string(folder) + "/" + std::string(name);
}

}  // namespace library

namespace {

std::vector<uint8_t> makeBlob(const uint64_t pathHash, const std::initializer_list<uint8_t> fields) {
  std::vector<uint8_t> blob(sizeof(pathHash) + fields.size());
  std::memcpy(blob.data(), &pathHash, sizeof(pathHash));
  std::copy(fields.begin(), fields.end(), blob.begin() + sizeof(pathHash));
  return blob;
}

}  // namespace

TEST(LibraryIndexFile, MissingIndexDoesNotCloseAnUninitializedHandle) {
  Storage.clearFile();
  HalFile::resetInvalidCloseCount();

  {
    library::LibraryIndexFile index;
    EXPECT_FALSE(index.open("/missing.clx"));
  }

  EXPECT_EQ(HalFile::invalidCloseCount(), 0);
}

TEST(LibraryIndexFile, ReadsEveryStoredOrderInBothDirections) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  library::layoutSections(header, 0, 0);

  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t authorOrder[] = {2, 0, 1};
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), authorOrder, sizeof(authorOrder));
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));

  const auto expectOrder = [&](const library::SortOrder order, const uint16_t a, const uint16_t b, const uint16_t c) {
    EXPECT_EQ(index.ordinalForRow(order, 0), a);
    EXPECT_EQ(index.ordinalForRow(order, 1), b);
    EXPECT_EQ(index.ordinalForRow(order, 2), c);
    EXPECT_EQ(index.ordinalForRow(order, 3), 0xFFFF);
  };
  expectOrder(library::SortOrder::RecentAsc, 1, 2, 0);
  expectOrder(library::SortOrder::RecentDesc, 0, 2, 1);
  expectOrder(library::SortOrder::TitleAsc, 0, 1, 2);
  expectOrder(library::SortOrder::TitleDesc, 2, 1, 0);
  expectOrder(library::SortOrder::AuthorAsc, 2, 0, 1);
  expectOrder(library::SortOrder::AuthorDesc, 1, 0, 2);
}

TEST(LibraryIndexFile, ResolvesRecentRowsByIdentity) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 3;
  // One 8-byte path hash blob per record.
  library::layoutSections(header, 0, 3 * sizeof(uint64_t));
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));

  // Ordinals 0 and 2 share a size, so only the hash can tell them apart.
  constexpr uint64_t HASHES[] = {11, 22, 33};
  constexpr uint32_t SIZES[] = {100, 200, 100};
  for (uint16_t ordinal = 0; ordinal < 3; ordinal++) {
    library::ClixRecord record{};
    record.fileSize = SIZES[ordinal];
    record.nameOff = ordinal * sizeof(uint64_t);
    std::memcpy(bytes.data() + library::recordOffset(header, ordinal), &record, sizeof(record));
    std::memcpy(bytes.data() + header.nameStart + record.nameOff, &HASHES[ordinal], sizeof(uint64_t));
  }
  const uint16_t arrivalOrder[] = {1, 2, 0};
  std::memcpy(bytes.data() + library::arrivalOrderOffset(header, 0), arrivalOrder, sizeof(arrivalOrder));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  const library::BookIdentity books[] = {
      {HASHES[0], SIZES[0]},  // ordinal 0 -> ascending row 2
      {HASHES[2], SIZES[2]},  // same size as ordinal 0, hash picks ordinal 2 -> row 1
      {99, SIZES[0]},         // size matches, hash does not: absent
      {HASHES[1], 999},       // hash matches, size does not: absent
      {HASHES[1], 0},         // size unknown: the hash alone matches -> row 0
  };
  uint16_t rows[5] = {};
  ASSERT_TRUE(index.recentRowsFor(books, 5, rows));
  EXPECT_EQ(rows[0], 2);
  EXPECT_EQ(rows[1], 1);
  EXPECT_EQ(rows[2], 0xFFFF);
  EXPECT_EQ(rows[3], 0xFFFF);
  EXPECT_EQ(rows[4], 0);
}

TEST(LibraryIndexFile, RejectsInvalidPermutationOrdinal) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  library::layoutSections(header, 0, 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const uint16_t invalid = 1;
  std::memcpy(bytes.data() + library::authorOrderOffset(header, 0), &invalid, sizeof(invalid));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_EQ(index.ordinalForRow(library::SortOrder::AuthorAsc, 0), 0xFFFF);
}

TEST(LibraryIndexFile, ReadsPathHashAndEveryPublicBlobField) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  const uint8_t folder[] = {6, '/', 'b', 'o', 'o', 'k', 's'};
  constexpr uint64_t PATH_HASH = 0x0123456789ABCDEFULL;
  const auto blob = makeBlob(PATH_HASH, {'x', 1, 'a', 1, 't', 8, 'O', 'r', 'i', 'g', 'i', 'n', 'a', 'l'});
  header.folderCount = 1;
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  Storage.setFile("/library.clx", bytes);

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  uint64_t pathHash = 0;
  ASSERT_TRUE(index.readPathHash(record, pathHash));
  EXPECT_EQ(pathHash, PATH_HASH);
  std::string name;
  ASSERT_TRUE(index.readName(record, name));
  EXPECT_EQ(name, "x");
  std::string author;
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "a");
  std::string title;
  ASSERT_TRUE(index.readTitle(record, title));
  EXPECT_EQ(title, "t");
  ASSERT_TRUE(index.readSourceAuthor(record, author));
  EXPECT_EQ(author, "Original");
  std::string path;
  ASSERT_TRUE(index.readPath(record, path));
  EXPECT_EQ(path, "/books/x");
  index.close();

  bytes[header.nameStart + sizeof(PATH_HASH) + 5] = 255;
  Storage.setFile("/library.clx", std::move(bytes));
  ASSERT_TRUE(index.open("/library.clx"));
  EXPECT_FALSE(index.readSourceAuthor(record, author));
}

TEST(LibraryIndexFile, RejectsTruncatedAndOverflowingPathHashes) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  library::layoutSections(header, 0, sizeof(uint64_t) - 1);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  library::ClixRecord record{};
  uint64_t hash = 1;
  EXPECT_FALSE(index.readPathHash(record, hash));
  EXPECT_EQ(hash, 0u);
  EXPECT_TRUE(index.ioFailed());

  record.nameOff = UINT32_MAX;
  EXPECT_FALSE(index.readPathHash(record, hash));
}

TEST(LibraryIndexFile, RejectsFolderRecordBeyondFolderBlob) {
  library::ClixHeader header{};
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 1;
  const uint8_t folder[] = {5, '/'};
  const auto blob = makeBlob(1, {'x', 0, 0, 0});
  library::layoutSections(header, sizeof(folder), blob.size());
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folder, sizeof(folder));
  std::memcpy(bytes.data() + header.nameStart, blob.data(), blob.size());
  library::ClixRecord record{};
  record.nameLen = 1;
  Storage.setFile("/library.clx", std::move(bytes));

  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  std::string path;
  EXPECT_FALSE(index.readPath(record, path));
}

namespace {

// Three folders: /a, /bb, /c (ids 0..2), no books needed to walk the table.
std::vector<uint8_t> threeFolderIndex(library::ClixHeader& header) {
  std::memcpy(header.magic, library::CLIX_MAGIC, sizeof(header.magic));
  header.formatVersion = library::CLIX_FORMAT_VERSION;
  header.foldVersion = library::CLIX_FOLD_VERSION;
  header.bookCount = 0;
  header.folderCount = 3;
  const uint8_t folders[] = {2, '/', 'a', 3, '/', 'b', 'b', 2, '/', 'c'};
  library::layoutSections(header, sizeof(folders), 0);
  std::vector<uint8_t> bytes(header.selfSize, 0);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + header.folderStart, folders, sizeof(folders));
  return bytes;
}

}  // namespace

TEST(LibraryIndexFile, ReadsFolderPathsAndWalksTheTableOnce) {
  library::ClixHeader header{};
  Storage.setFile("/library.clx", threeFolderIndex(header));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));

  std::string path;
  ASSERT_TRUE(index.readFolderPath(1, path));
  EXPECT_EQ(path, "/bb");
  ASSERT_TRUE(index.readFolderPath(2, path));
  EXPECT_EQ(path, "/c");
  EXPECT_FALSE(index.readFolderPath(3, path));
  EXPECT_TRUE(path.empty());

  std::vector<std::string> seen;
  auto collect = [](uint16_t id, const std::string& dir, void* user) {
    auto& out = *static_cast<std::vector<std::string>*>(user);
    EXPECT_EQ(id, out.size());
    out.push_back(dir);
    return true;
  };
  ASSERT_TRUE(index.forEachFolder(collect, &seen));
  EXPECT_EQ(seen, (std::vector<std::string>{"/a", "/bb", "/c"}));

  size_t visits = 0;
  auto stopAtSecond = [](uint16_t, const std::string&, void* user) { return ++*static_cast<size_t*>(user) < 2; };
  EXPECT_TRUE(index.forEachFolder(stopAtSecond, &visits));
  EXPECT_EQ(visits, 2u);
}

TEST(LibraryIndexFile, ForEachFolderRejectsATruncatedTable) {
  library::ClixHeader header{};
  auto bytes = threeFolderIndex(header);
  bytes[header.folderStart + 3] = 200;  // the second record claims more bytes than the table holds
  Storage.setFile("/library.clx", std::move(bytes));
  library::LibraryIndexFile index;
  ASSERT_TRUE(index.open("/library.clx"));
  size_t visits = 0;
  auto count = [](uint16_t, const std::string&, void* user) {
    ++*static_cast<size_t*>(user);
    return true;
  };
  EXPECT_FALSE(index.forEachFolder(count, &visits));
  EXPECT_EQ(visits, 1u);
  std::string path;
  EXPECT_FALSE(index.readFolderPath(2, path));
}

TEST(LibraryFolders, BucketsBooksByFolderKeepingOrder) {
  // Title order of six books living in folders 2,0,2,1,0,2.
  const uint16_t ids[] = {2, 0, 2, 1, 0, 2};
  uint16_t counts[4], rows[6], starts[4], folder[4];
  const uint16_t groups = library::bucketByFolder(ids, 6, 4, counts, rows, starts, folder);
  ASSERT_EQ(groups, 3);  // folder 3 is empty and gets no group
  EXPECT_EQ(folder[0], 0);
  EXPECT_EQ(folder[1], 1);
  EXPECT_EQ(folder[2], 2);
  EXPECT_EQ(starts[0], 0);
  EXPECT_EQ(starts[1], 2);
  EXPECT_EQ(starts[2], 3);
  const uint16_t expected[] = {1, 4, 3, 0, 2, 5};
  for (int i = 0; i < 6; i++) EXPECT_EQ(rows[i], expected[i]) << i;
}

TEST(LibraryFolders, RejectsEmptyInputAndOutOfRangeFolders) {
  uint16_t counts[2], rows[2], starts[2], folder[2];
  const uint16_t good[] = {0, 1};
  const uint16_t bad[] = {0, 2};
  EXPECT_EQ(library::bucketByFolder(good, 0, 2, counts, rows, starts, folder), 0);
  EXPECT_EQ(library::bucketByFolder(good, 2, 0, counts, rows, starts, folder), 0);
  EXPECT_EQ(library::bucketByFolder(bad, 2, 2, counts, rows, starts, folder), 0);
  EXPECT_EQ(library::bucketByFolder(good, 2, 2, counts, rows, starts, folder), 2);
}

TEST(LibraryFormats, BitsAndMask) {
  using namespace library;
  EXPECT_EQ(formatBitOf("/a/B.EPUB"), FMT_EPUB);
  EXPECT_EQ(formatBitOf("x.xtch"), FMT_XTC);
  EXPECT_EQ(formatBitOf("x.pdf"), FMT_PDF);
  EXPECT_EQ(formatBitOf("x.canvas"), 0);
  EXPECT_TRUE(formatAllowed(FMT_ALL, "x.cbz"));
  EXPECT_FALSE(formatAllowed(FMT_EPUB | FMT_PDF, "x.txt"));
  EXPECT_TRUE(formatAllowed(FMT_EPUB, "x.canvas"));  // not covered by the filter
  // Browse Files switches: book formats and pictures, drawings and zips; unknown types always show.
  EXPECT_FALSE(browseAllowed(BROWSE_ALL & ~BROWSE_PICTURES, "a.PNG"));
  EXPECT_TRUE(browseAllowed(BROWSE_ALL & ~BROWSE_PICTURES, "a.epub"));
  EXPECT_FALSE(browseAllowed(BROWSE_ALL & ~BROWSE_DRAWINGS, "board.canvas"));
  EXPECT_FALSE(browseAllowed(BROWSE_ALL & ~BROWSE_DRAWINGS, "sketch.excalidraw.md"));
  EXPECT_FALSE(browseAllowed(BROWSE_ALL & ~BROWSE_ZIP, "files.zip"));
  EXPECT_FALSE(browseAllowed(BROWSE_ALL & ~static_cast<uint16_t>(FMT_MOBI), "a.azw3"));
  EXPECT_TRUE(browseAllowed(0, "firmware.bin"));
  // The home Books count: books only, and only the ticked formats.
  EXPECT_TRUE(countsAsBook(FMT_ALL, "a.epub"));
  EXPECT_TRUE(countsAsBook(FMT_ALL, "a.PDF"));
  EXPECT_TRUE(countsAsBook(FMT_ALL, "a.azw3"));
  // FB2, HTML, RTF, DOC and CHM share the MOBI / AZW bit: every stored mask stays valid (no new bit, no
  // migration).
  EXPECT_EQ(formatBitOf("a.docx"), 0);
  for (const char* name : {"a.fb2", "b.HTML", "c.htm", "d.xhtml", "e.Rtf", "f.doc", "g.CHM"}) {
    EXPECT_EQ(formatBitOf(name), FMT_MOBI) << name;
    EXPECT_TRUE(formatAllowed(FMT_ALL, name)) << name;
    EXPECT_FALSE(formatAllowed(FMT_ALL & ~FMT_MOBI, name)) << name;
    EXPECT_FALSE(browseAllowed(BROWSE_ALL & ~static_cast<uint16_t>(FMT_MOBI), name)) << name;
    EXPECT_TRUE(countsAsBook(FMT_ALL, name)) << name;
  }
  EXPECT_EQ(FMT_ALL, 127);
  EXPECT_EQ(BROWSE_ALL, 1023);
  EXPECT_FALSE(countsAsBook(FMT_ALL, "notes.md"));
  EXPECT_FALSE(countsAsBook(FMT_ALL, "readme.txt"));
  EXPECT_FALSE(countsAsBook(FMT_ALL, "board.canvas"));
  EXPECT_FALSE(countsAsBook(FMT_ALL & ~FMT_PDF, "a.pdf"));
}
