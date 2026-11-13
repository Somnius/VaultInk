#include <ChmBook.h>
#include <DocToHtml.h>
#include <Fb2ToHtml.h>
#include <HtmlBook.h>
#include <RtfToHtml.h>
#include <gtest/gtest.h>

#include <random>

#include "ChmFixture.h"
#include "DocFixture.h"
#include "TestIo.h"

// Files made to cost time, card space or memory, and failures that are not the file's fault. What must hold:
// a book is kept (Ok, Truncated) only when everything that could be read was read.

using chmfixture::buildChm;
using chmfixture::buildHhc;
using chmfixture::ChmFile;
using chmfixture::ChmOptions;
using docfixture::buildDoc;
using docfixture::MemSource;
using docfixture::Piece;

namespace {

struct CountingSink {
  size_t bytes = 0;
  static bool write(void* ctx, const char*, size_t size) {
    static_cast<CountingSink*>(ctx)->bytes += size;
    return true;
  }
};

std::string letters(const size_t size, const uint32_t seed) {
  std::mt19937 rng(seed);
  std::string text;
  text.reserve(size);
  while (text.size() < size) text += rng() % 7 == 0 ? ' ' : static_cast<char>('a' + rng() % 26);
  return text;
}

int pauses = 0;
int allocations = 0;
int failAllocation = -1;
void* countingAlloc(size_t size) { return allocations++ == failAllocation ? nullptr : malloc(size); }

// 3000 small pages in one reset span: read the simple way, every second page would unpack the section again
// from its start.
std::string descendingPages(const uint32_t resetFrames) {
  std::vector<ChmFile> files;
  std::vector<std::pair<std::string, std::string>> toc;
  for (int i = 0; i < 3000; i++) {
    const std::string name = "p" + std::to_string(10000 + i) + ".htm";
    files.push_back({"/" + name, "<p>" + letters(300, static_cast<uint32_t>(i)) + "</p>", true});
  }
  // The contents list jumps between the back half and the front half: every second page lies far before the
  // one just read.
  for (int k = 0; k < 1500; k++) {
    toc.push_back({"T", "p" + std::to_string(10000 + 2999 - k) + ".htm"});
    toc.push_back({"T", "p" + std::to_string(10000 + 1499 - k) + ".htm"});
  }
  files.push_back({"/toc.hhc", buildHhc(toc), false});
  ChmOptions options;
  options.lzx.resetFrames = resetFrames;
  return buildChm(files, options);
}

}  // namespace

TEST(HostileChm, PagesInDescendingOrderWithOneHugeResetSpanAreRefused) {
  MemSource mem{descendingPages(64), 1 << 20};  // a frame comes in one read
  CountingSink sink;
  chm::Memory memory;
  memory.pause = [] { pauses++; };
  pauses = 0;
  const blocksource::Source source = mem.source();
  EXPECT_EQ(chm::streamHtml(source, "T", CountingSink::write, &sink, memory), chm::Status::TooComplex);
  // About 1 MB of pages is 32 frames: the budget is 4 * 32 + 2 * 3000 + 256 frames, three reads each (two table
  // entries and the frame), plus the directory and the pages read before it ran out.
  EXPECT_LT(mem.reads, 3u * (4 * 40 + 2 * 3000 + 256) + 20000u);
  EXPECT_GT(pauses, 100);
}

TEST(HostileChm, TheSamePagesWithTheUsualResetsOpen) {
  // A reset every 2 frames, as Microsoft's compiler writes: going back costs 2 frames at most, inside the budget.
  MemSource mem{descendingPages(2)};
  CountingSink sink;
  EXPECT_EQ(chm::streamHtml(mem.source(), "T", CountingSink::write, &sink), chm::Status::Ok);
  EXPECT_GT(sink.bytes, 3000u * 300u);
}

TEST(HostileChm, ASectionThatClaimsToUnpackToFarMoreThanItHoldsIsRefused) {
  const std::vector<ChmFile> files = {{"/a.htm", "<p>" + letters(5000, 1) + "</p>", true}};
  chm::Info info;
  for (const uint64_t claim : {uint64_t{300} << 20, uint64_t{100} << 20, uint64_t{0xFFFFFFFF}, uint64_t{1} << 40}) {
    ChmOptions options;
    options.claimedPlainSize = claim;
    if (claim > (uint64_t{400} << 20)) {
      // The table can't be that long in a test: the size alone must refuse it.
      options.claimedPlainSize = 0;
      std::string file = buildChm(files, options);
      const size_t at = file.find(std::string("\x02\x00\x00\x00\x01\x00\x00\x00\x08\x00\x00\x00\x28\x00\x00\x00", 16));
      ASSERT_NE(at, std::string::npos);
      for (int i = 0; i < 8; i++) file[at + 16 + i] = static_cast<char>((claim >> (8 * i)) & 0xFF);
      MemSource mem{file};
      EXPECT_NE(chm::readInfo(mem.source(), info), chm::Status::Ok) << claim;
      continue;
    }
    MemSource mem{buildChm(files, options)};
    EXPECT_EQ(chm::readInfo(mem.source(), info), chm::Status::TooBig) << claim;
    CountingSink sink;
    EXPECT_EQ(chm::streamHtml(mem.source(), "T", CountingSink::write, &sink), chm::Status::TooBig) << claim;
    EXPECT_EQ(sink.bytes, 0u);
  }
}

TEST(HostileChm, ADirectoryNumberLongerThanNineBytesEndsTheListing) {
  const std::vector<ChmFile> files = {{"/a.htm", "<p>a</p>", false}};
  const std::string name = std::string(1, '\x06') + "/x.htm";
  ChmOptions options;
  options.withCompression = false;
  // Ten bytes that all say "another byte follows": no number. Nothing after it in the chunk is trusted.
  options.rawFirstEntry = name + std::string(1, '\0') + std::string(10, '\xFF') + std::string(1, '\x01');
  chm::Info info;
  MemSource bad{buildChm(files, options)};
  EXPECT_EQ(chm::readInfo(bad.source(), info), chm::Status::NoPages);
  // Nine bytes is a number (63 bits), too big for a file: that entry is left out and the next one is read.
  options.rawFirstEntry =
      name + std::string(1, '\0') + std::string(8, '\xFF') + std::string(1, '\x7F') + std::string(1, '\x01');
  MemSource big{buildChm(files, options)};
  StringSink sink;
  EXPECT_EQ(chm::streamHtml(big.source(), "T", StringSink::write, &sink), chm::Status::Ok);
  EXPECT_EQ(bodyOf(sink.text), "<p>a</p>");
}

TEST(HostileChm, AReadThatFailsIsNeverACutBook) {
  std::vector<ChmFile> files = {{"/#SYSTEM", chmfixture::buildSystem("Title", "toc.hhc"), false},
                                {"/toc.hhc", buildHhc({{"B", "b.htm"}, {"A", "a.htm"}}), true}};
  files.push_back({"/a.htm", "<p>" + letters(50000, 2) + "</p>", true});
  files.push_back({"/b.htm", "<p>" + letters(50000, 3) + "</p>", false});
  const std::string file = buildChm(files);
  MemSource whole{file, 4096};
  CountingSink all;
  ASSERT_EQ(chm::streamHtml(whole.source(), "T", CountingSink::write, &all), chm::Status::Ok);
  ASSERT_GT(whole.reads, 50u);
  size_t failures = 0;
  for (size_t failAfter = 0; failAfter < whole.reads; failAfter++) {
    MemSource mem{file, 4096};
    mem.failAfter = failAfter;
    const blocksource::Source source = mem.source();
    CountingSink sink;
    const chm::Status status = chm::streamHtml(source, "T", CountingSink::write, &sink);
    EXPECT_TRUE(source.ioFailed) << failAfter;
    EXPECT_EQ(status, chm::Status::IoError) << failAfter;
    failures += status == chm::Status::IoError;
    MemSource again{file, 4096};
    again.failAfter = failAfter;
    const blocksource::Source infoSource = again.source();
    chm::Info info;
    const chm::Status infoStatus = chm::readInfo(infoSource, info);
    EXPECT_EQ(infoStatus == chm::Status::Ok, !infoSource.ioFailed) << failAfter;
  }
  EXPECT_EQ(failures, whole.reads);
}

TEST(HostileChm, NoMemoryPartWayIsNeverACutBook) {
  std::vector<ChmFile> files = {{"/toc.hhc", buildHhc({{"B", "b.htm"}, {"A", "a.htm"}}), true},
                                {"/a.htm", "<p>" + letters(40000, 4) + "</p>", true},
                                {"/b.htm", "<p>" + letters(40000, 5) + "</p>", true}};
  const std::string file = buildChm(files);
  chm::Memory memory;
  memory.alloc = countingAlloc;
  memory.release = free;
  allocations = 0;
  failAllocation = -1;
  {
    MemSource mem{file};
    CountingSink sink;
    ASSERT_EQ(chm::streamHtml(mem.source(), "T", CountingSink::write, &sink, memory), chm::Status::Ok);
  }
  const int total = allocations;
  ASSERT_GE(total, 5);  // the page table, the contents order and titles, the frame input, the window
  for (int fail = 0; fail < total; fail++) {
    allocations = 0;
    failAllocation = fail;
    MemSource mem{file};
    CountingSink sink;
    EXPECT_EQ(chm::streamHtml(mem.source(), "T", CountingSink::write, &sink, memory), chm::Status::Oom) << fail;
  }
  failAllocation = -1;
}

TEST(HostileDoc, AReadThatFailsIsNeverACutBook) {
  docfixture::DocOptions options;
  options.fragment = true;
  options.summary = docfixture::buildSummary(1252, "Title", "Author");
  const std::string file = buildDoc({Piece::narrow(letters(9000, 6) + "\r"), Piece::utf16(u"αβ\r")}, options);
  MemSource whole{file, 512};
  CountingSink all;
  ASSERT_EQ(doc::streamHtml(whole.source(), "T", CountingSink::write, &all), doc::Status::Ok);
  ASSERT_GT(whole.reads, 20u);
  for (size_t failAfter = 0; failAfter < whole.reads; failAfter++) {
    MemSource mem{file, 512};
    mem.failAfter = failAfter;
    const blocksource::Source source = mem.source();
    CountingSink sink;
    EXPECT_EQ(doc::streamHtml(source, "T", CountingSink::write, &sink), doc::Status::IoError) << failAfter;
    MemSource again{file, 512};
    again.failAfter = failAfter;
    const blocksource::Source infoSource = again.source();
    doc::Info info;
    const doc::Status infoStatus = doc::readInfo(infoSource, info);
    EXPECT_EQ(infoStatus == doc::Status::Ok, !infoSource.ioFailed) << failAfter;
    if (infoSource.ioFailed) {
      EXPECT_EQ(infoStatus, doc::Status::IoError) << failAfter;
    }
  }
  // The same file cut short is the file's own damage: what is there opens.
  MemSource cut{file.substr(0, file.size() - 600), 512};
  const blocksource::Source cutSource = cut.source();
  CountingSink sink;
  const doc::Status status = doc::streamHtml(cutSource, "T", CountingSink::write, &sink);
  EXPECT_FALSE(cutSource.ioFailed);
  EXPECT_NE(status, doc::Status::IoError);
}

TEST(HostileDoc, ADocumentWithNoTextIsRefused) {
  doc::Info info;
  for (const char* text : {"\r", "\x01\r\r\x0C", "  \t \r", "\x13 PAGE \x15\r", "\x07\x07\r"}) {
    MemSource mem{buildDoc({Piece::narrow(text)})};
    EXPECT_EQ(doc::readInfo(mem.source(), info), doc::Status::NoText);
    StringSink sink;
    EXPECT_EQ(doc::streamHtml(mem.source(), "T", StringSink::write, &sink), doc::Status::NoText);
  }
  MemSource empty{buildDoc({})};
  EXPECT_NE(doc::readInfo(empty.source(), info), doc::Status::Ok);
  // Text only in a footnote is text.
  docfixture::DocOptions notes;
  notes.ccpFtn = 3;
  MemSource footnote{buildDoc({Piece::narrow("\rn\r\r")}, notes)};
  EXPECT_EQ(doc::readInfo(footnote.source(), info), doc::Status::Ok);
  // One letter after a megabyte of marks is found.
  MemSource late{buildDoc({Piece::narrow(std::string(1 << 20, '\x01') + "x\r")})};
  EXPECT_EQ(doc::readInfo(late.source(), info), doc::Status::Ok);
}

TEST(HostileDoc, AnOldPlainTextFileNamedDocIsNotAWordDocument) {
  doc::Info info;
  for (const std::string& text :
       {std::string("This is the documentation of the program.\n\nChapter 1\n") + letters(3000, 7), std::string("x"),
        std::string("\xEF\xBB\xBFUTF-8 text with a mark"),
        std::string("{\\rtf1 an RTF saved as .doc}") + letters(900, 8),
        std::string("\x31\xBE\x00\x00 Word for DOS") + letters(900, 9),
        std::string("\xDB\xA5\x2D\x00 Word 2") + letters(900, 10)}) {
    MemSource mem{text};
    EXPECT_EQ(doc::readInfo(mem.source(), info), doc::Status::NotDoc);
    StringSink sink;
    EXPECT_EQ(doc::streamHtml(mem.source(), "T", StringSink::write, &sink), doc::Status::NotDoc);
    EXPECT_TRUE(sink.text.empty());
  }
}

// One page is xhtml::MAX_PAGE_BYTES at most, whatever the format: past it the converter fails with TooBig.
TEST(PageSizeCap, EveryConverterStopsAtTheCap) {
  const size_t big = xhtml::MAX_PAGE_BYTES + 500000;
  const std::string text = letters(big, 11);
  {
    MemSource mem{buildDoc({Piece::narrow(text + "\r")})};
    CountingSink sink;
    EXPECT_EQ(doc::streamHtml(mem.source(), "T", CountingSink::write, &sink), doc::Status::TooBig);
    EXPECT_LE(sink.bytes, xhtml::MAX_PAGE_BYTES);
  }
  {
    std::vector<ChmFile> files;
    for (int i = 0; i < 18; i++)
      files.push_back({"/p" + std::to_string(10 + i) + ".htm", "<p>" + text.substr(0, 1 << 20) + "</p>", false});
    MemSource mem{buildChm(files)};
    CountingSink sink;
    EXPECT_EQ(chm::streamHtml(mem.source(), "T", CountingSink::write, &sink), chm::Status::TooBig);
    EXPECT_LE(sink.bytes, xhtml::MAX_PAGE_BYTES);
  }
  {
    const std::string page = "<html><body><p>" + text + "</p></body></html>";
    StringReader reader{page};
    CountingSink sink;
    EXPECT_EQ(htmlbook::streamHtml(&reader, StringReader::read, "T", CountingSink::write, &sink),
              htmlbook::Status::TooBig);
    EXPECT_LE(sink.bytes, xhtml::MAX_PAGE_BYTES);
  }
  {
    const std::string page = "{\\rtf1\\ansi " + text + "\\par}";
    StringReader reader{page};
    CountingSink sink;
    EXPECT_EQ(rtf::streamHtml(&reader, StringReader::read, "T", CountingSink::write, &sink), rtf::Status::TooBig);
    EXPECT_LE(sink.bytes, xhtml::MAX_PAGE_BYTES);
  }
  {
    const std::string page = "<?xml version=\"1.0\" encoding=\"utf-8\"?><FictionBook><body><section><p>" + text +
                             "</p></section></body></FictionBook>";
    StringReader reader{page};
    CountingSink sink;
    EXPECT_EQ(fb2::streamHtml(&reader, StringReader::read, "T", CountingSink::write, &sink), fb2::Status::TooBig);
    EXPECT_LE(sink.bytes, xhtml::MAX_PAGE_BYTES);
  }
  // Just under the cap opens.
  {
    const std::string page = "<html><body><p>" + text.substr(0, xhtml::MAX_PAGE_BYTES - 4096) + "</p></body></html>";
    StringReader reader{page};
    CountingSink sink;
    EXPECT_EQ(htmlbook::streamHtml(&reader, StringReader::read, "T", CountingSink::write, &sink), htmlbook::Status::Ok);
  }
}
