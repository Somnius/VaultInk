#include <ChmBook.h>
#include <Lzx.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <random>

#include "ChmFixture.h"
#include "TestIo.h"

using chmfixture::BlockKind;
using chmfixture::buildChm;
using chmfixture::buildHhc;
using chmfixture::buildSystem;
using chmfixture::ChmFile;
using chmfixture::ChmOptions;
using chmfixture::LzxEncoder;
using chmfixture::LzxOptions;
using chmfixture::LzxStream;
using docfixture::MemSource;

namespace {

// Text that compresses: words from a small list, with now and then a run of random bytes.
std::string sampleData(const size_t size, const uint32_t seed) {
  static const char* const words[] = {"the ",  "reader ", "page ", "<p>", "</p>\n",
                                      "vault", "ink ",    "and ",  "of ", "\xCE\xB1"};
  std::mt19937 rng(seed);
  std::string data;
  while (data.size() < size) {
    if (rng() % 40 == 0) {
      for (int i = 0; i < 30; i++) data += static_cast<char>(rng());
    } else {
      data += words[rng() % 10];
    }
  }
  data.resize(size);
  return data;
}

// Decodes a whole stream the way lib/Chm does: frame after frame, a reset every `resetFrames`.
lzx::Result decodeAll(const LzxStream& stream, const LzxOptions& options, const size_t size, std::string& out) {
  lzx::Decoder decoder;
  if (!decoder.init(options.windowBits)) return lzx::Result::Corrupt;
  for (size_t f = 0; f < stream.frameStart.size(); f++) {
    if (f % options.resetFrames == 0) decoder.reset();
    const size_t from = stream.frameStart[f];
    const size_t to = f + 1 < stream.frameStart.size() ? stream.frameStart[f + 1] : stream.bytes.size();
    const uint32_t want = static_cast<uint32_t>(std::min<size_t>(lzx::FRAME, size - f * lzx::FRAME));
    const uint8_t* data = nullptr;
    const lzx::Result result =
        decoder.decodeFrame(reinterpret_cast<const uint8_t*>(stream.bytes.data()) + from, to - from, want, data);
    if (result != lzx::Result::Ok) return result;
    out.append(reinterpret_cast<const char*>(data), want);
  }
  return lzx::Result::Ok;
}

void roundTrip(const std::string& data, const LzxOptions& options) {
  const LzxStream stream = LzxEncoder(options).encode(data);
  ASSERT_EQ(stream.frameStart.size(), (data.size() + lzx::FRAME - 1) / lzx::FRAME);
  std::string out;
  ASSERT_EQ(decodeAll(stream, options, data.size(), out), lzx::Result::Ok);
  EXPECT_TRUE(out == data);
}

struct Converted {
  chm::Status status = chm::Status::Ok;
  std::string page;
  std::string body;
};

Converted convert(const std::string& file, const size_t step = 4096) {
  MemSource mem{file, step};
  StringSink sink;
  Converted result;
  result.status = chm::streamHtml(mem.source(), "", StringSink::write, &sink);
  result.page = sink.text;
  result.body = bodyOf(sink.text);
  return result;
}

chm::Status infoOf(const std::string& file, chm::Info& info) {
  MemSource mem{file};
  return chm::readInfo(mem.source(), info);
}

std::string pageHtml(const std::string& text) {
  return "<html><head><title>x</title></head><body><p>" + text + "</p></body></html>";
}

std::vector<ChmFile> threePages(const bool compressed) {
  return {
      {"/#SYSTEM", buildSystem("The Manual", "toc.hhc", 0x0409), false},
      {"/toc.hhc", buildHhc({{"Second &amp; last", "b.htm"}, {"First", "sub/A.htm#top"}, {"First again", "sub/a.htm"}}),
       compressed},
      {"/b.htm", pageHtml("page b"), compressed},
      {"/c.html", pageHtml("page c"), compressed},
      {"/sub/a.htm", pageHtml("page a"), compressed},
      {"/style.css", "p { color: red }", compressed},
      {"/pic.gif", std::string(300, '\x01'), compressed}};
}

const char* const THREE_PAGES_BODY =
    "<h2>Second &amp; last</h2><p>page b</p><h2>First</h2><p>page a</p><hr/><p>page c</p>";

}  // namespace

TEST(Lzx, VerbatimBlocks) {
  LzxOptions options;
  roundTrip(sampleData(100000, 1), options);
  roundTrip(sampleData(1, 2), options);
  roundTrip(sampleData(32768, 3), options);
  roundTrip(sampleData(32769, 4), options);
  roundTrip(std::string(70000, 'a'), options);  // one long run: the repeated offset over and over
}

TEST(Lzx, ALastFrameFilledWithZeros) {
  // What Microsoft's compiler writes: the last frame is compressed as a whole frame, the size says less.
  LzxOptions options;
  options.fillLastFrame = true;
  options.blocks = {{BlockKind::Aligned, 32768}};
  for (const size_t size : {size_t{6881}, size_t{40000}, size_t{65536}, size_t{65537}}) {
    const std::string data = sampleData(size, 70);
    const LzxStream stream = LzxEncoder(options).encode(data);
    std::string out;
    ASSERT_EQ(decodeAll(stream, options, data.size(), out), lzx::Result::Ok);
    EXPECT_TRUE(out == data);
  }
}

TEST(Lzx, AlignedOffsetBlocks) {
  LzxOptions options;
  options.blocks = {{BlockKind::Aligned, 20000}};
  roundTrip(sampleData(150000, 5), options);
}

TEST(Lzx, UncompressedBlocksOddAndAcrossFrames) {
  LzxOptions options;
  options.blocks = {{BlockKind::Uncompressed, 777}, {BlockKind::Uncompressed, 40000}, {BlockKind::Uncompressed, 1}};
  roundTrip(sampleData(150000, 6), options);
  options.blocks = {{BlockKind::Uncompressed, 32767}, {BlockKind::Uncompressed, 1}, {BlockKind::Uncompressed, 32769}};
  roundTrip(sampleData(140000, 7), options);
}

TEST(Lzx, MixedBlocksOfEverySizeAcrossFrames) {
  LzxOptions options;
  options.blocks = {{BlockKind::Verbatim, 5000},  {BlockKind::Uncompressed, 333}, {BlockKind::Aligned, 50001},
                    {BlockKind::Verbatim, 1},     {BlockKind::Uncompressed, 2},   {BlockKind::Aligned, 3},
                    {BlockKind::Verbatim, 70000}, {BlockKind::Uncompressed, 4095}};
  for (const uint32_t resetFrames : {1u, 2u, 3u, 8u}) {
    options.resetFrames = resetFrames;
    roundTrip(sampleData(300000, 8 + resetFrames), options);
  }
}

TEST(Lzx, EveryWindowSize) {
  for (unsigned bits = lzx::MIN_WINDOW_BITS; bits <= lzx::MAX_WINDOW_BITS; bits++) {
    LzxOptions options;
    options.windowBits = bits;
    options.resetFrames = (1u << bits) / lzx::FRAME;
    options.blocks = {{BlockKind::Verbatim, 60000}, {BlockKind::Aligned, 60000}};
    roundTrip(sampleData(200000, 20 + bits), options);
  }
}

TEST(Lzx, FarMatchesInATwoMegabyteWindow) {
  // The same 4000 random bytes at the start and 1.9 MB later: offsets that need the top position slots.
  std::mt19937 rng(99);
  std::string marker(4000, '\0');
  for (char& c : marker) c = static_cast<char>(rng());
  std::string data = marker + sampleData(1900000, 30) + marker + sampleData(50000, 31);
  LzxOptions options;
  options.windowBits = 21;
  options.resetFrames = 64;
  options.blocks = {{BlockKind::Verbatim, 100000}, {BlockKind::Aligned, 100000}};
  const LzxStream stream = LzxEncoder(options).encode(data);
  EXPECT_LT(stream.bytes.size(), data.size() / 2);  // the second marker was found, among the rest
  std::string out;
  ASSERT_EQ(decodeAll(stream, options, data.size(), out), lzx::Result::Ok);
  EXPECT_TRUE(out == data);
}

TEST(Lzx, RefusesWindowsOutOfRange) {
  lzx::Decoder decoder;
  EXPECT_FALSE(decoder.init(14));
  EXPECT_FALSE(decoder.init(22));
  EXPECT_FALSE(decoder.init(25));
  const auto noMemory = [](size_t) -> void* { return nullptr; };
  EXPECT_FALSE(decoder.init(16, noMemory, [](void*) {}));
  const uint8_t* out = nullptr;
  const uint8_t in[4] = {};
  EXPECT_EQ(decoder.decodeFrame(in, 4, 100, out), lzx::Result::Corrupt);
}

TEST(Lzx, TheE8HeaderIsUnsupported) {
  LzxOptions options;
  options.e8Header = true;
  const LzxStream stream = LzxEncoder(options).encode(sampleData(1000, 40));
  std::string out;
  EXPECT_EQ(decodeAll(stream, options, 1000, out), lzx::Result::Unsupported);
}

TEST(Lzx, AMatchBeforeTheResetIsCorrupt) {
  // Two frames encoded as one span, decoded with a reset between them: the second frame's matches reach back.
  LzxOptions options;
  const std::string half = sampleData(lzx::FRAME, 41);
  const LzxStream stream = LzxEncoder(options).encode(half + half);
  options.resetFrames = 1;
  std::string out;
  EXPECT_EQ(decodeAll(stream, options, 2 * lzx::FRAME, out), lzx::Result::Corrupt);
}

TEST(Lzx, DamagedStreamsDoNotCrash) {
  LzxOptions options;
  options.blocks = {{BlockKind::Verbatim, 9000}, {BlockKind::Uncompressed, 501}, {BlockKind::Aligned, 30000}};
  const std::string data = sampleData(90000, 42);
  const LzxStream good = LzxEncoder(options).encode(data);
  std::mt19937 rng(20261113);
  size_t decoded = 0;
  for (int round = 0; round < 3000; round++) {
    LzxStream stream = good;
    const int edits = 1 + static_cast<int>(rng() % 4);
    for (int e = 0; e < edits; e++) stream.bytes[rng() % stream.bytes.size()] = static_cast<char>(rng());
    if (round % 5 == 0) stream.bytes.resize(rng() % stream.bytes.size());
    for (uint32_t& start : stream.frameStart)
      start = std::min<uint32_t>(start, static_cast<uint32_t>(stream.bytes.size()));
    std::string out;
    if (decodeAll(stream, options, data.size(), out) == lzx::Result::Ok) decoded++;
  }
  EXPECT_LT(decoded, 3000u);
  // Bytes that were never LZX.
  lzx::Decoder decoder;
  ASSERT_TRUE(decoder.init(16));
  for (int round = 0; round < 3000; round++) {
    std::string junk(rng() % 600, '\0');
    for (char& c : junk) c = static_cast<char>(rng());
    decoder.reset();
    const uint8_t* out = nullptr;
    decoder.decodeFrame(reinterpret_cast<const uint8_t*>(junk.data()), junk.size(), 1 + rng() % lzx::FRAME, out);
  }
}

TEST(ChmBook, PagesInContentsOrderThenTheRest) {
  for (const bool compressed : {false, true}) {
    const std::string file = buildChm(threePages(compressed));
    chm::Info info;
    ASSERT_EQ(infoOf(file, info), chm::Status::Ok);
    EXPECT_EQ(info.title, "The Manual");
    EXPECT_EQ(info.language, "en");
    const Converted c = convert(file, compressed ? 333 : 4096);
    EXPECT_EQ(c.status, chm::Status::Ok);
    EXPECT_EQ(c.body, THREE_PAGES_BODY);
    EXPECT_TRUE(wellFormed(c.page));
    EXPECT_NE(c.page.find(chm::CACHE_VERSION_TAG), std::string::npos);
    EXPECT_NE(c.page.find("<title>The Manual</title>"), std::string::npos);
  }
}

TEST(ChmBook, LastFrameFilledWithZeros) {
  ChmOptions options;
  options.lzx.fillLastFrame = true;
  options.lzx.blocks = {{BlockKind::Aligned, 32768}};
  std::vector<ChmFile> files = threePages(true);
  std::mt19937 rng(71);
  std::string letters;
  while (letters.size() < 50000) letters += static_cast<char>('a' + rng() % 26);
  files.push_back({"/zz.htm", pageHtml(letters), true});
  const Converted c = convert(buildChm(files, options));
  EXPECT_EQ(c.status, chm::Status::Ok);
  EXPECT_EQ(c.body, std::string(THREE_PAGES_BODY) + "<hr/><p>" + letters + "</p>");
}

TEST(ChmBook, NoContentsFileMeansDirectoryOrder) {
  std::vector<ChmFile> files = {
      {"/b.htm", pageHtml("b"), true}, {"/A.HTM", pageHtml("a"), true}, {"/c.html", pageHtml("c"), false}};
  const Converted c = convert(buildChm(files));
  EXPECT_EQ(c.status, chm::Status::Ok);
  EXPECT_EQ(c.body, "<p>a</p><hr/><p>b</p><hr/><p>c</p>");
  chm::Info info;
  EXPECT_EQ(infoOf(buildChm(files), info), chm::Status::Ok);
  EXPECT_TRUE(info.title.empty());
}

TEST(ChmBook, VersionTwoHeaderAndSmallChunks) {
  ChmOptions options;
  options.version = 2;
  options.chunkSize = 256;  // the directory takes several chunks
  const Converted c = convert(buildChm(threePages(true), options));
  EXPECT_EQ(c.status, chm::Status::Ok);
  EXPECT_EQ(c.body, THREE_PAGES_BODY);
}

TEST(ChmBook, ManyPagesAcrossFramesAndResets) {
  // 60 pages of about 4 KB: 240 KB of text, 8 frames, a reset every 2; the contents file lists them backwards,
  // so the decoder starts again from a reset for every page.
  std::vector<ChmFile> files;
  std::vector<std::pair<std::string, std::string>> toc;
  std::string expected;
  for (int i = 59; i >= 0; i--) {
    const std::string name = "p" + std::to_string(100 + i) + ".htm";
    toc.push_back({"T" + std::to_string(i), name});
  }
  for (int i = 0; i < 60; i++) {
    std::string text = "page " + std::to_string(i) + " ";
    std::mt19937 rng(i);
    while (text.size() < 4000) text += static_cast<char>('a' + rng() % 26);
    files.push_back({"/p" + std::to_string(100 + i) + ".htm", pageHtml(text), true});
  }
  for (int i = 59; i >= 0; i--) {
    const std::string& page = files[i].data;
    expected +=
        "<h2>T" + std::to_string(i) + "</h2>" + page.substr(page.find("<p>"), page.find("</body>") - page.find("<p>"));
  }
  files.push_back({"/toc.hhc", buildHhc(toc), true});
  ChmOptions options;
  options.lzx.blocks = {{BlockKind::Verbatim, 30000}, {BlockKind::Uncompressed, 1001}, {BlockKind::Aligned, 45000}};
  const Converted c = convert(buildChm(files, options));
  EXPECT_EQ(c.status, chm::Status::Ok);
  EXPECT_TRUE(c.body == expected);
  EXPECT_TRUE(wellFormed(c.page));
}

TEST(ChmBook, GreekFromTheLanguageAndFromThePage) {
  // "Βιβλίο" in Windows-1253 and in UTF-8.
  const std::string greek1253 = "\xC2\xE9\xE2\xEB\xDF\xEF";
  const std::string greekUtf8 = "\xCE\x92\xCE\xB9\xCE\xB2\xCE\xBB\xCE\xAF\xCE\xBF";
  std::vector<ChmFile> files = {
      {"/#SYSTEM", buildSystem(greek1253, "", 0x0408), false},
      {"/toc.hhc", buildHhc({{greek1253, "a.htm"}}), true},
      {"/a.htm", "<html><body><p>" + greek1253 + "</p></body></html>", true},
      {"/b.htm", "<html><head><meta charset=\"utf-8\"></head><body><p>" + greekUtf8 + "</p></body></html>", true}};
  ChmOptions options;
  options.lcid = 0x0409;  // the header says English, #SYSTEM says Greek: #SYSTEM is the one used
  const std::string file = buildChm(files, options);
  chm::Info info;
  ASSERT_EQ(infoOf(file, info), chm::Status::Ok);
  EXPECT_EQ(info.title, greekUtf8);
  EXPECT_EQ(info.language, "el");
  const Converted c = convert(file);
  EXPECT_EQ(c.body, "<h2>" + greekUtf8 + "</h2><p>" + greekUtf8 + "</p><hr/><p>" + greekUtf8 + "</p>");
  EXPECT_TRUE(wellFormed(c.page));
}

TEST(ChmBook, ContentsFileOddities) {
  const std::string hhc =
      "<html><!-- a comment with a quote ' and > in it --><body><ul>"
      "<li><object type='text/sitemap'><PARAM NAME=Name VALUE='Bare > quoted'><param name=\"Local\" "
      "value=\"ms-its:book.chm::/One%20Two.htm\"></object>"
      "<li><object><param name=\"Name\" value=\"No page\"></object>"
      "<li><object><param name=\"Name\" value=\"Missing\"><param name=\"Local\" value=\"gone.htm\"></object>"
      "<li><object><param name=\"Name\" value=\"" +
      std::string(400, 'L') +
      "\"><param name=\"Local\" value=\".\\dir\\b.htm\"></object>"
      "<li><object><param name=\"Local\" value=\"/c.htm\"></object>"
      "<li><object><param name=\"Name\" value=\"" +
      std::string(900, 'x') +
      "\"><param name=\"Local\" value=\"d.htm\"></object>"
      "</ul></body></html>";
  std::vector<ChmFile> files = {{"/toc.hhc", hhc, true},
                                {"/c.htm", pageHtml("c"), true},
                                {"/d.htm", pageHtml("d"), true},
                                {"/dir/b.htm", pageHtml("b"), true},
                                {"/one two.htm", pageHtml("one"), true}};
  const Converted c = convert(buildChm(files));
  EXPECT_EQ(c.status, chm::Status::Ok);
  // The 900-letter title is in a tag longer than is read, so its page comes last, with the pages not listed.
  EXPECT_EQ(c.body, "<h2>Bare &gt; quoted</h2><p>one</p><h2>" + std::string(chm::MAX_TOC_TITLE, 'L') +
                        "</h2><p>b</p><hr/><p>c</p><hr/><p>d</p>");
  EXPECT_TRUE(wellFormed(c.page));
}

TEST(ChmBook, RefusesWhatItCannotRead) {
  chm::Info info;
  EXPECT_EQ(infoOf("", info), chm::Status::NotChm);
  EXPECT_EQ(infoOf(std::string(5000, 'x'), info), chm::Status::NotChm);
  EXPECT_EQ(infoOf("ITSF" + std::string(20, '\0'), info), chm::Status::NotChm);

  EXPECT_EQ(infoOf(buildChm({{"/style.css", "x", true}, {"/#SYSTEM", buildSystem("T"), false}}), info),
            chm::Status::NoPages);
  EXPECT_TRUE(convert(buildChm({{"/style.css", "x", true}})).page.empty());

  ChmOptions version;
  version.version = 4;
  EXPECT_EQ(infoOf(buildChm(threePages(true), version), info), chm::Status::Unsupported);

  ChmOptions window;
  window.controlWindow = 128;  // 4 MB
  EXPECT_EQ(infoOf(buildChm(threePages(true), window), info), chm::Status::Unsupported);
  window.controlWindow = 3;  // not a power of two
  EXPECT_EQ(infoOf(buildChm(threePages(true), window), info), chm::Status::Unsupported);

  ChmOptions control;
  control.controlVersion = 3;
  EXPECT_EQ(infoOf(buildChm(threePages(true), control), info), chm::Status::Unsupported);

  ChmOptions noSection;
  noSection.withCompression = false;
  noSection.extraEntries = {{"/z.htm", std::string(100, 'z'), true}};  // a compressed page, nothing to read it with
  EXPECT_EQ(infoOf(buildChm({{"/a.htm", pageHtml("a"), false}}, noSection), info), chm::Status::Corrupt);
  // The same pages when all are stored: no compressed section is needed.
  noSection.extraEntries.clear();
  EXPECT_EQ(convert(buildChm({{"/a.htm", pageHtml("a"), false}}, noSection)).body, "<p>a</p>");

  ChmOptions e8;
  e8.lzx.e8Header = true;
  const Converted c = convert(buildChm(threePages(true), e8));
  EXPECT_EQ(c.status, chm::Status::Unsupported);
}

TEST(ChmBook, WindowsAndResetIntervals) {
  std::vector<ChmFile> files = threePages(true);
  std::mt19937 rng(72);
  std::string letters;
  while (letters.size() < 300000) letters += static_cast<char>('a' + rng() % 26);
  files.push_back({"/zz.htm", pageHtml(letters), true});
  for (const unsigned bits : {15u, 16u, 18u, 21u}) {
    for (const uint32_t resetFrames : {1u, 4u, 64u}) {
      ChmOptions options;
      options.lzx.windowBits = bits;
      options.lzx.resetFrames = resetFrames;
      options.controlCache = 7;  // the third number of ControlData is not used
      const Converted c = convert(buildChm(files, options));
      EXPECT_EQ(c.status, chm::Status::Ok) << bits << " " << resetFrames;
      EXPECT_TRUE(c.body == std::string(THREE_PAGES_BODY) + "<hr/><p>" + letters + "</p>")
          << bits << " " << resetFrames;
    }
  }
}

TEST(ChmBook, ControlDataInBytes) {
  ChmOptions options;
  options.controlVersion = 1;
  EXPECT_EQ(convert(buildChm(threePages(true), options)).body, THREE_PAGES_BODY);
}

TEST(ChmBook, MorePagesThanTheCapAreLeftOut) {
  std::vector<ChmFile> files;
  for (size_t i = 0; i < chm::MAX_TOPICS + 50; i++)
    files.push_back({"/p" + std::to_string(100000 + i) + ".htm", "<p>x</p>", true});
  const Converted c = convert(buildChm(files));
  EXPECT_EQ(c.status, chm::Status::Truncated);
  EXPECT_TRUE(wellFormed(c.page));
  size_t pages = 0;
  for (size_t at = c.page.find("<p>x</p>"); at != std::string::npos; at = c.page.find("<p>x</p>", at + 1)) pages++;
  EXPECT_EQ(pages, chm::MAX_TOPICS);
}

TEST(ChmBook, EntriesThatOverlapStopAtTheFileSize) {
  // 3000 entries that all point at the same 60 KB page: 180 MB if each were written.
  ChmOptions options;
  options.overlapCount = 3000;
  std::string big = "<p>";
  big += sampleData(60000, 50);
  const Converted c = convert(buildChm({{"/a.htm", big, true}}, options));
  EXPECT_EQ(c.status, chm::Status::Truncated);
  EXPECT_TRUE(wellFormed(c.page));
  EXPECT_LT(c.page.size(), 2u * 1024 * 1024);
}

TEST(ChmBook, SinkThatRefusesIsAWriteError) {
  MemSource mem{buildChm(threePages(true))};
  StringSink sink;
  sink.failAfter = 100;
  EXPECT_EQ(chm::streamHtml(mem.source(), "T", StringSink::write, &sink), chm::Status::WriteError);
}

TEST(ChmBook, NoMemoryIsReportedNotCrashed) {
  chm::Memory none;
  none.alloc = [](size_t) -> void* { return nullptr; };
  none.release = [](void*) {};
  MemSource mem{buildChm(threePages(true))};
  chm::Info info;
  EXPECT_EQ(chm::readInfo(mem.source(), info, none), chm::Status::Oom);
  StringSink sink;
  EXPECT_EQ(chm::streamHtml(mem.source(), "T", StringSink::write, &sink, none), chm::Status::Oom);
}

TEST(ChmBook, EveryShorterFileIsRefusedOrReadWithoutACrash) {
  const std::string file = buildChm(threePages(true));
  for (size_t len = 0; len < file.size(); len += (len < 600 ? 1 : 29)) {
    const Converted c = convert(file.substr(0, len));
    if (!c.page.empty()) {
      EXPECT_TRUE(wellFormed(c.page)) << len;
    }
    chm::Info info;
    infoOf(file.substr(0, len), info);
  }
}

TEST(ChmBook, DamagedFilesDoNotCrash) {
  ChmOptions options;
  options.lzx.blocks = {{BlockKind::Verbatim, 3000}, {BlockKind::Uncompressed, 201}, {BlockKind::Aligned, 5000}};
  std::vector<ChmFile> files = threePages(true);
  files.push_back({"/long.htm", pageHtml(sampleData(70000, 60)), true});
  const std::string compressed = buildChm(files, options);
  const std::string stored = buildChm(threePages(false));
  std::mt19937 rng(20261113);
  size_t opened = 0;
  for (int round = 0; round < 4000; round++) {
    std::string file = round % 2 == 0 ? compressed : stored;
    const int edits = 1 + static_cast<int>(rng() % 6);
    for (int e = 0; e < edits; e++) {
      // Half of the damage goes to the headers and the directory, where the numbers are.
      const size_t at = rng() % 2 ? rng() % file.size() : rng() % 0x600;
      switch (rng() % 4) {
        case 0:
          file[at] = static_cast<char>(rng());
          break;
        case 1:
          file[at] = static_cast<char>(0xFF);
          break;
        case 2:
          file[at] = 0;
          break;
        default:
          if (at + 4 <= file.size()) docfixture::put32(file, at, rng() % 2 ? rng() : rng() % 64);
          break;
      }
    }
    const Converted c = convert(file, 1 + rng() % 5000);
    if (!c.page.empty()) {
      opened++;
      EXPECT_TRUE(wellFormed(c.page)) << round;
      EXPECT_LT(c.page.size(), 4u << 20) << round;
    }
    chm::Info info;
    infoOf(file, info);
  }
  EXPECT_GT(opened, 100u);
}

// Not a check by itself: with MARKUP_WRITE_FIXTURES set to a folder, the files the tests build are written there,
// so another program (7-Zip reads .chm) can be asked whether it reads them the same way.
TEST(ChmBook, WritesItsFixturesWhenAsked) {
  const char* dir = getenv("MARKUP_WRITE_FIXTURES");
  if (!dir) return;
  std::mt19937 rng(5);
  std::vector<ChmFile> files = threePages(true);
  for (int i = 0; i < 40; i++) {
    std::string text;
    while (text.size() < 9000) text += i % 3 == 0 ? static_cast<char>(rng()) : static_cast<char>('a' + rng() % 6);
    files.push_back({"/extra" + std::to_string(i) + ".htm", text, true});
  }
  const auto save = [&](const char* name, const ChmOptions& options) {
    std::ofstream(std::string(dir) + "/" + name, std::ios::binary) << buildChm(files, options);
  };
  ChmOptions options;
  save("verbatim.chm", options);
  options.lzx.blocks = {{BlockKind::Aligned, 20000}};
  save("aligned.chm", options);
  options.lzx.blocks = {{BlockKind::Uncompressed, 777}, {BlockKind::Uncompressed, 40000}, {BlockKind::Uncompressed, 2}};
  save("uncompressed.chm", options);
  options.lzx.blocks = {{BlockKind::Verbatim, 5000},  {BlockKind::Uncompressed, 333}, {BlockKind::Aligned, 50001},
                        {BlockKind::Verbatim, 1},     {BlockKind::Uncompressed, 2},   {BlockKind::Aligned, 3},
                        {BlockKind::Verbatim, 70000}, {BlockKind::Uncompressed, 4095}};
  save("mixed.chm", options);
  options.lzx.fillLastFrame = true;
  save("mixed-filled.chm", options);
  options.lzx.windowBits = 21;
  options.lzx.resetFrames = 64;
  save("window-2mb.chm", options);
  options.lzx.windowBits = 17;
  options.lzx.resetFrames = 4;
  save("window-128k.chm", options);
  // Two readings of ControlData are published. Here they differ (interval 4, window 4, third number 1: a reset
  // every 4 frames, or every 2), and each file is compressed one of the two ways. 7-Zip reads the first and not
  // the second, and lib/Chm takes the numbers the same way.
  options.controlCache = 1;
  save("readings-reset-every-4.chm", options);
  options.lzx.resetFrames = 2;
  options.controlInterval = 4;
  save("readings-reset-every-2.chm", options);
  for (const ChmFile& file : files) {
    std::string name = file.name.substr(1);
    std::replace(name.begin(), name.end(), '/', '_');
    std::ofstream(std::string(dir) + "/plain_" + name, std::ios::binary) << file.data;
  }
}
