#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "MobiFormat.h"
#include "MobiHtmlFilter.h"

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes& b, size_t at, uint16_t v) {
  b[at] = v >> 8;
  b[at + 1] = v & 0xFF;
}
void put32(Bytes& b, size_t at, uint32_t v) {
  b[at] = v >> 24;
  b[at + 1] = (v >> 16) & 0xFF;
  b[at + 2] = (v >> 8) & 0xFF;
  b[at + 3] = v & 0xFF;
}
void push32(Bytes& b, uint32_t v) {
  b.push_back(v >> 24);
  b.push_back((v >> 16) & 0xFF);
  b.push_back((v >> 8) & 0xFF);
  b.push_back(v & 0xFF);
}

// Reference PalmDOC compressor (as in calibre's palmdoc.py): literals, back-references of 3..10
// bytes within 2047, and the space+char pair.
Bytes palmdocCompress(const std::string& in) {
  Bytes out;
  size_t i = 0;
  const size_t n = in.size();
  while (i < n) {
    // Longest match within the last 2047 bytes, length 3..10.
    size_t bestLen = 0, bestDist = 0;
    if (i >= 1) {
      const size_t start = i > 2047 ? i - 2047 : 0;
      for (size_t j = start; j < i; j++) {
        size_t len = 0;
        while (len < 10 && i + len < n && in[j + len] == in[i + len]) len++;
        if (len >= 3 && len > bestLen) {
          bestLen = len;
          bestDist = i - j;
        }
      }
    }
    if (bestLen >= 3) {
      const uint16_t pair = static_cast<uint16_t>(0x8000 | (bestDist << 3) | (bestLen - 3));
      out.push_back(pair >> 8);
      out.push_back(pair & 0xFF);
      i += bestLen;
      continue;
    }
    const uint8_t c = static_cast<uint8_t>(in[i]);
    if (c == ' ' && i + 1 < n && static_cast<uint8_t>(in[i + 1]) >= 0x40 && static_cast<uint8_t>(in[i + 1]) <= 0x7F) {
      out.push_back(static_cast<uint8_t>(in[i + 1]) ^ 0x80);
      i += 2;
      continue;
    }
    if (c == 0 || (c >= 9 && c <= 0x7F)) {
      out.push_back(c);
      i++;
      continue;
    }
    // Literal run (1..8 bytes) for bytes outside the plain range.
    size_t run = 0;
    while (run < 8 && i + run < n) {
      const uint8_t d = static_cast<uint8_t>(in[i + run]);
      if (d == 0 || (d >= 9 && d <= 0x7F)) break;
      run++;
    }
    out.push_back(static_cast<uint8_t>(run));
    for (size_t k = 0; k < run; k++) out.push_back(static_cast<uint8_t>(in[i + k]));
    i += run;
  }
  return out;
}

struct Fixture {
  std::string html;
  uint16_t compression = mobi::COMPRESSION_PALMDOC;
  uint16_t encryption = 0;
  uint32_t encoding = mobi::ENCODING_UTF8;
  std::string title = "Fixture Title";
  std::string author = "Some Author";
  bool mobiHeader = true;
  bool kf8Boundary = false;
  uint16_t extraFlags = 0;  // when set, bit 1 appends a 2-byte trailing entry and bit 0 a multibyte byte
  size_t recordSize = 4096;
  std::vector<Bytes> huffRecords;  // HUFF then CDIC records, appended after the text (compression HUFF)
  bool huffHeaderFields = true;    // write their index/count into the MOBI header
  Bytes (*huffEncode)(const std::string&) = nullptr;
  uint32_t textLength = 0;  // record 0 text length; 0 = html.size()
};

Bytes buildMobi(const Fixture& f) {
  std::vector<Bytes> records;
  // Text records.
  for (size_t at = 0; at < f.html.size(); at += f.recordSize) {
    const std::string chunk = f.html.substr(at, f.recordSize);
    Bytes rec = f.compression == mobi::COMPRESSION_PALMDOC ? palmdocCompress(chunk)
                : f.compression == mobi::COMPRESSION_HUFF  ? f.huffEncode(chunk)
                                                           : Bytes(chunk.begin(), chunk.end());
    if (f.extraFlags & 1) rec.push_back(0x00);  // multibyte overlap entry: 1 byte, 0 chars
    if (f.extraFlags & 2) {
      rec.push_back(0x55);  // some TBS payload
      rec.push_back(0x82);  // size 2 incl. itself, high bit terminates
    }
    records.push_back(std::move(rec));
  }
  if (records.empty()) records.push_back({});
  const uint16_t textRecords = static_cast<uint16_t>(records.size());
  const uint32_t huffIndex = textRecords + 1;  // after record 0
  for (const Bytes& r : f.huffRecords) records.push_back(r);
  // Record 0.
  Bytes r0(16, 0);
  put16(r0, 0, f.compression);
  put32(r0, 4, f.textLength ? f.textLength : static_cast<uint32_t>(f.html.size()));
  put16(r0, 8, textRecords);
  put16(r0, 10, static_cast<uint16_t>(f.recordSize));
  put16(r0, 12, f.encryption);
  if (f.mobiHeader) {
    const uint32_t headerLen = 0xE8;
    Bytes m(headerLen, 0);
    memcpy(m.data(), "MOBI", 4);
    put32(m, 4, headerLen);
    put32(m, 8, 2);  // mobiType: Mobipocket book
    put32(m, 12, f.encoding);
    put32(m, 20, 6);  // file version
    put32(m, 0x5C, 0xFFFFFFFF);
    if (!f.huffRecords.empty() && f.huffHeaderFields) {
      put32(m, 0x60, huffIndex);
      put32(m, 0x64, static_cast<uint32_t>(f.huffRecords.size()));
    }
    put32(m, 0x70, 0x40);  // EXTH present
    put16(m, 0xE2, f.extraFlags);
    Bytes exth;
    uint32_t count = 0;
    const auto add = [&](uint32_t type, const std::string& v) {
      push32(exth, type);
      push32(exth, static_cast<uint32_t>(8 + v.size()));
      exth.insert(exth.end(), v.begin(), v.end());
      count++;
    };
    add(100, f.author);
    add(503, f.title);
    if (f.kf8Boundary) add(121, std::string("\0\0\0\x05", 4));
    Bytes exthHead;
    exthHead.insert(exthHead.end(), {'E', 'X', 'T', 'H'});
    push32(exthHead, static_cast<uint32_t>(12 + exth.size()));
    push32(exthHead, count);
    const std::string fullName = "Full Name " + f.title;
    const uint32_t fullNameOffset = static_cast<uint32_t>(16 + headerLen + exthHead.size() + exth.size());
    put32(m, 0x44, fullNameOffset);
    put32(m, 0x48, static_cast<uint32_t>(fullName.size()));
    r0.insert(r0.end(), m.begin(), m.end());
    r0.insert(r0.end(), exthHead.begin(), exthHead.end());
    r0.insert(r0.end(), exth.begin(), exth.end());
    r0.insert(r0.end(), fullName.begin(), fullName.end());
    r0.push_back(0);
  }
  records.insert(records.begin(), r0);
  // PDB header + record list.
  Bytes file(78, 0);
  memcpy(file.data(), "fixture", 7);
  memcpy(file.data() + 60, f.mobiHeader ? "BOOKMOBI" : "TEXtREAd", 8);
  put16(file, 76, static_cast<uint16_t>(records.size()));
  const size_t listAt = file.size();
  file.resize(listAt + records.size() * 8 + 2, 0);
  uint32_t offset = static_cast<uint32_t>(file.size());
  for (size_t i = 0; i < records.size(); i++) {
    put32(file, listAt + i * 8, offset);
    file[listAt + i * 8 + 4] = 0;
    put32(file, listAt + i * 8 + 4, static_cast<uint32_t>(i) & 0xFFFFFF);
    offset += static_cast<uint32_t>(records[i].size());
  }
  for (const Bytes& r : records) file.insert(file.end(), r.begin(), r.end());
  return file;
}

mobi::Reader readerFor(const Bytes& file) {
  mobi::Reader r;
  r.ctx = const_cast<Bytes*>(&file);
  r.fileSize = static_cast<uint32_t>(file.size());
  r.readAt = [](void* ctx, uint32_t offset, uint8_t* buf, size_t size) -> int {
    const Bytes& b = *static_cast<Bytes*>(ctx);
    if (offset >= b.size()) return -1;
    const size_t n = std::min(size, b.size() - offset);
    memcpy(buf, b.data() + offset, n);
    return static_cast<int>(n);
  };
  return r;
}

std::string streamOf(const Bytes& file, mobi::Status* status = nullptr, const mobi::ImageResolver* images = nullptr) {
  const mobi::Reader r = readerFor(file);
  mobi::Info info;
  const mobi::Status s = mobi::open(r, info);
  if (status) *status = s;
  if (s != mobi::Status::Ok) return "";
  std::string out;
  mobi::Sink sink;
  sink.ctx = &out;
  sink.write = [](void* ctx, const char* data, size_t size) {
    static_cast<std::string*>(ctx)->append(data, size);
    return true;
  };
  const mobi::Status s2 = mobi::streamHtml(r, info, info.title, sink, images);
  if (status) *status = s2;
  return out;
}

std::string body(const std::string& doc) {
  const size_t a = doc.find("<body>\n");
  const size_t b = doc.rfind("\n</body>");
  if (a == std::string::npos || b == std::string::npos) return doc;
  return doc.substr(a + 7, b - a - 7);
}

std::string filterOnly(const std::string& in, size_t chunk = 0) {
  std::string out;
  mobi::HtmlFilter f(
      [](void* ctx, const char* d, size_t n) {
        static_cast<std::string*>(ctx)->append(d, n);
        return true;
      },
      &out);
  if (chunk == 0) {
    f.feed(in);
  } else {
    for (size_t i = 0; i < in.size(); i += chunk) f.feed(std::string_view(in).substr(i, chunk));
  }
  f.finish();
  return out;
}

// --- PalmDOC ---

TEST(PalmDoc, RoundTrip) {
  std::string text;
  for (int i = 0; i < 200; i++) text += "The quick brown fox jumps over the lazy dog. Again and again, ";
  text += "\xC3\xA9\xC3\xA8 non-ascii bytes \x01\x02 and\ttabs\n";
  const Bytes packed = palmdocCompress(text);
  EXPECT_LT(packed.size(), text.size() / 2);
  std::vector<uint8_t> out(text.size() + 16);
  const size_t n = mobi::palmdocDecompress(packed.data(), packed.size(), out.data(), out.size());
  ASSERT_EQ(n, text.size());
  EXPECT_EQ(std::string(out.begin(), out.begin() + n), text);
}

TEST(PalmDoc, SpaceCharPairAndLiterals) {
  const uint8_t in[] = {'a', 0xC1 /* " A" */, 0x02, 0x80, 0x81};
  uint8_t out[16];
  const size_t n = mobi::palmdocDecompress(in, sizeof(in), out, sizeof(out));
  ASSERT_EQ(n, 5u);
  EXPECT_EQ(std::string(reinterpret_cast<char*>(out), n), std::string("a A\x80\x81", 5));
}

TEST(PalmDoc, RejectsBadBackReferenceAndOverflow) {
  const uint8_t bad[] = {0x80, 0x08};  // distance 1 with nothing before it
  uint8_t out[16];
  EXPECT_EQ(mobi::palmdocDecompress(bad, sizeof(bad), out, sizeof(out)), 0u);
  const uint8_t literalShort[] = {0x05, 'a'};  // literal run longer than input
  EXPECT_EQ(mobi::palmdocDecompress(literalShort, sizeof(literalShort), out, sizeof(out)), 0u);
  const uint8_t fine[] = {'a', 'b', 'c', 'd'};
  EXPECT_EQ(mobi::palmdocDecompress(fine, sizeof(fine), out, 3), 0u);  // output too small
}

TEST(PalmDoc, TrailingEntries) {
  const uint8_t rec[] = {'t', 'e', 'x', 't', 0x01, 0x55, 0x82};
  EXPECT_EQ(mobi::trailingSize(rec, sizeof(rec), 0), 0u);
  EXPECT_EQ(mobi::trailingSize(rec, sizeof(rec), 0x2), 2u);  // only the sized entry
  EXPECT_EQ(mobi::trailingSize(rec, sizeof(rec), 0x3), 4u);  // plus multibyte: (0x01 & 3) + 1 = 2
  // A two-byte size: 0x00 then 0x83 -> 3? no: low byte first from the end: 0x03 then 0x80|0.
  const uint8_t rec2[] = {'a', 'b', 'c', 'd', 'e', 0x81, 0x03};
  // Read from end: 0x03 (7 bits, no stop), then 0x81 (stop): value = 3 | (1 << 7) = 131 > len -> rejected
  EXPECT_EQ(mobi::trailingSize(rec2, sizeof(rec2), 0x2), sizeof(rec2));
  const uint8_t rec3[] = {'a', 'b', 0x80, 0x03};  // value 3 | 0<<7 = 3
  EXPECT_EQ(mobi::trailingSize(rec3, sizeof(rec3), 0x2), 3u);
}

TEST(Cp1252, SmartQuotes) {
  char out[3];
  EXPECT_EQ(mobi::cp1252ToUtf8(0x93, out), 3u);
  EXPECT_EQ(std::string(out, 3), "\xE2\x80\x9C");
  EXPECT_EQ(mobi::cp1252ToUtf8(0xE9, out), 2u);
  EXPECT_EQ(std::string(out, 2), "\xC3\xA9");
  EXPECT_EQ(mobi::cp1252ToUtf8('a', out), 1u);
}

// --- Headers ---

TEST(MobiOpen, ReadsMetadata) {
  Fixture f;
  f.html = "<html><body><p>Hello</p></body></html>";
  const Bytes file = buildMobi(f);
  mobi::Info info;
  ASSERT_EQ(mobi::open(readerFor(file), info), mobi::Status::Ok);
  EXPECT_EQ(info.title, "Fixture Title");
  EXPECT_EQ(info.author, "Some Author");
  EXPECT_EQ(info.textEncoding, mobi::ENCODING_UTF8);
  EXPECT_EQ(info.textRecordCount, 1);
  EXPECT_EQ(info.textLength, f.html.size());
  EXPECT_TRUE(info.hasMobiHeader);
  EXPECT_FALSE(info.hasKf8Boundary);
  EXPECT_EQ(info.compression, mobi::COMPRESSION_PALMDOC);
}

TEST(MobiOpen, RefusesDrm) {
  Fixture f;
  f.html = "<p>secret</p>";
  f.encryption = 2;
  mobi::Info info;
  EXPECT_EQ(mobi::open(readerFor(buildMobi(f)), info), mobi::Status::Drm);
  f.encryption = 1;
  EXPECT_EQ(mobi::open(readerFor(buildMobi(f)), info), mobi::Status::Drm);
  EXPECT_STREQ(mobi::statusText(mobi::Status::Drm), "DRM-protected");
}

TEST(MobiOpen, RejectsNonMobiAndBadHeaders) {
  mobi::Info info;
  Bytes empty;
  EXPECT_EQ(mobi::open(readerFor(empty), info), mobi::Status::NotMobi);
  Bytes epubish(200, 'x');
  EXPECT_EQ(mobi::open(readerFor(epubish), info), mobi::Status::NotMobi);

  Fixture f;
  f.html = "<p>x</p>";
  Bytes file = buildMobi(f);
  // Truncated record list.
  Bytes truncated(file.begin(), file.begin() + 80);
  EXPECT_EQ(mobi::open(readerFor(truncated), info), mobi::Status::BadHeader);
  // Text record count past the record table.
  Bytes badCount = file;
  const uint32_t rec0At = (badCount[78] << 24) | (badCount[79] << 16) | (badCount[80] << 8) | badCount[81];
  put16(badCount, rec0At + 8, 500);
  EXPECT_EQ(mobi::open(readerFor(badCount), info), mobi::Status::BadHeader);
  // Record size over the 4096 cap.
  Bytes badSize = file;
  put16(badSize, rec0At + 10, 8192);
  EXPECT_EQ(mobi::open(readerFor(badSize), info), mobi::Status::BadHeader);
  // MOBI header length past record 0.
  Bytes badLen = file;
  put32(badLen, rec0At + 16 + 4, 0x7FFFFFFF);
  EXPECT_EQ(mobi::open(readerFor(badLen), info), mobi::Status::BadHeader);
}

TEST(MobiOpen, HybridKf8FlagSeen) {
  Fixture f;
  f.html = "<p>x</p>";
  f.kf8Boundary = true;
  mobi::Info info;
  ASSERT_EQ(mobi::open(readerFor(buildMobi(f)), info), mobi::Status::Ok);
  EXPECT_TRUE(info.hasKf8Boundary);
}

// --- Streaming ---

TEST(MobiStream, MultiRecordBookRoundTrips) {
  Fixture f;
  std::string html = "<html><head><title>T</title><style>p{}</style></head><body>";
  for (int i = 0; i < 300; i++) {
    html += "<p>Paragraph number " + std::to_string(i) + " says &ldquo;hello&rdquo; &amp; more &#8212; done.</p>\n";
  }
  html += "<mbp:pagebreak/><h1>Chapter 2</h1><p>End</p></body></html>";
  f.html = html;
  f.extraFlags = 0x3;
  const Bytes file = buildMobi(f);
  mobi::Info info;
  ASSERT_EQ(mobi::open(readerFor(file), info), mobi::Status::Ok);
  EXPECT_GT(info.textRecordCount, 3);
  EXPECT_EQ(info.extraFlags, 3);

  mobi::Status status;
  const std::string doc = streamOf(file, &status);
  ASSERT_EQ(status, mobi::Status::Ok);
  EXPECT_NE(doc.find(mobi::CACHE_VERSION_TAG), std::string::npos);
  EXPECT_NE(doc.find("<head><title>Fixture Title</title></head>"), std::string::npos);
  const std::string b = body(doc);
  EXPECT_EQ(b.find("<style"), std::string::npos);
  EXPECT_EQ(b.find("p{}"), std::string::npos);
  EXPECT_NE(b.find("<p>Paragraph number 0 says \xE2\x80\x9Chello\xE2\x80\x9D &amp; more &#8212; done.</p>"),
            std::string::npos);
  EXPECT_NE(b.find("<p>Paragraph number 299 says"), std::string::npos);
  EXPECT_NE(b.find("<h1>Chapter 2</h1><p>End</p>"), std::string::npos);
  // Every paragraph came through.
  size_t count = 0;
  for (size_t at = b.find("<p>"); at != std::string::npos; at = b.find("<p>", at + 3)) count++;
  EXPECT_EQ(count, 301u);
}

TEST(MobiStream, UncompressedCp1252) {
  Fixture f;
  f.compression = mobi::COMPRESSION_NONE;
  f.encoding = mobi::ENCODING_CP1252;
  f.html = "<p>caf\xE9 \x93quoted\x94</p>";
  mobi::Status status;
  const std::string doc = streamOf(buildMobi(f), &status);
  ASSERT_EQ(status, mobi::Status::Ok);
  EXPECT_NE(body(doc).find("<p>caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D</p>"), std::string::npos);
}

TEST(MobiStream, PlainPalmDocText) {
  Fixture f;
  f.mobiHeader = false;
  f.html = "Line one & <two>\nLine two";
  mobi::Status status;
  const std::string doc = streamOf(buildMobi(f), &status);
  ASSERT_EQ(status, mobi::Status::Ok);
  EXPECT_EQ(body(doc), "<p>Line one &amp; &lt;two&gt;<br />\nLine two</p>");
}

TEST(MobiStream, ImagesThroughResolver) {
  Fixture f;
  f.html = "<p>Before</p><img recindex=\"00003\" align=\"baseline\"><img recindex=\"9\"><p>After</p>";
  int calls = 0;
  mobi::ImageResolver images;
  images.ctx = &calls;
  images.resolve = [](void* ctx, uint32_t recindex, std::string& path) {
    (*static_cast<int*>(ctx))++;
    if (recindex != 3) return false;
    path = "/.crosspoint/epub_1/img_3.jpg";
    return true;
  };
  mobi::Status status;
  const std::string doc = streamOf(buildMobi(f), &status, &images);
  ASSERT_EQ(status, mobi::Status::Ok);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(body(doc), "<p>Before</p><img src=\"/.crosspoint/epub_1/img_3.jpg\" /><p>After</p>");
  // Without a resolver the images are dropped.
  EXPECT_EQ(body(streamOf(buildMobi(f), &status)), "<p>Before</p><p>After</p>");
}

// --- HUFF/CDIC ---
//
// Synthetic code with three lengths, assigned top-down as Mobipocket does:
//   '1'            -> phrase 0 ("A")
//   '01'           -> phrase 1 (HUFF-coded phrase: "A" + phrase 5 + "A")
//   '00xxxxxxxx'   -> phrases 2..257 ("p<N> "), index = 0x101 - code
// 258 phrases with 8 code bits span two CDIC records (256 + 2).
namespace huff {

constexpr uint32_t PHRASES = 258;

// Phrase texts; the text to encode is space-separated tokens "A" (phrase 0), "B" (phrase 1, itself
// HUFF-coded as "A p5 A") and "p<N>" (phrase N).
std::string phraseText(uint32_t i) {
  if (i == 0) return "A ";
  if (i == 1) return "A p5 A ";  // what the coded phrase expands to
  return "p" + std::to_string(i) + " ";
}

struct BitWriter {
  Bytes out;
  uint32_t acc = 0;
  int bits = 0;
  void put(uint32_t code, int len) {
    for (int i = len - 1; i >= 0; i--) {
      acc = (acc << 1) | ((code >> i) & 1);
      if (++bits == 8) {
        out.push_back(static_cast<uint8_t>(acc));
        acc = 0;
        bits = 0;
      }
    }
  }
  Bytes finish() {
    if (bits) out.push_back(static_cast<uint8_t>(acc << (8 - bits)));
    return out;
  }
};

std::vector<uint32_t> tokens(const std::string& text) {
  std::vector<uint32_t> out;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == ' ') {
      i++;
      continue;
    }
    size_t end = text.find(' ', i);
    if (end == std::string::npos) end = text.size();
    const std::string tok = text.substr(i, end - i);
    if (tok == "A")
      out.push_back(0);
    else if (tok == "B")
      out.push_back(1);
    else
      out.push_back(static_cast<uint32_t>(std::stoul(tok.substr(1))));
    i = end;
  }
  return out;
}

Bytes encode(const std::string& text) {
  BitWriter w;
  for (const uint32_t i : tokens(text)) {
    if (i == 0)
      w.put(1, 1);
    else if (i == 1)
      w.put(1, 2);
    else
      w.put(0x101 - i, 10);
  }
  return w.finish();
}

// The decoded text for `text`.
std::string expanded(const std::string& text) {
  std::string out;
  for (const uint32_t i : tokens(text)) out += phraseText(i);
  return out;
}

Bytes huffRecord() {
  Bytes r(24 + 1024 + 256, 0);
  memcpy(r.data(), "HUFF", 4);
  put32(r, 4, 24);
  put32(r, 8, 24);
  put32(r, 12, 24 + 1024);
  for (uint32_t b = 0; b < 256; b++) {
    uint32_t v;
    if (b & 0x80)
      v = (1u << 8) | 0x80 | 1;  // '1': maxcode 1, len 1
    else if (b & 0x40)
      v = (2u << 8) | 0x80 | 2;  // '01': maxcode 2, len 2
    else
      v = 3;  // '00...': non-terminal, start at length 3 and grow via mincode
    put32(r, 24 + b * 4, v);
  }
  const size_t d2 = 24 + 1024;
  const auto pair = [&](int len, uint32_t mincode, uint32_t maxcode) {
    put32(r, d2 + (len - 1) * 8, mincode);
    put32(r, d2 + (len - 1) * 8 + 4, maxcode);
  };
  pair(1, 1, 1);
  pair(2, 1, 2);
  for (int len = 3; len <= 9; len++) pair(len, 1u << (len - 2), 0);  // '01000..' > any '00' prefix
  pair(10, 0, 0x101);
  return r;
}

std::vector<Bytes> cdicRecords(int bits = 8) {
  std::vector<Bytes> recs;
  const uint32_t per = 1u << bits;
  for (uint32_t first = 0; first < PHRASES; first += per) {
    const uint32_t n = std::min(per, PHRASES - first);
    Bytes r(16 + n * 2, 0);
    memcpy(r.data(), "CDIC", 4);
    put32(r, 4, 16);
    put32(r, 8, PHRASES);
    put32(r, 12, static_cast<uint32_t>(bits));
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t i = first + k;
      Bytes payload;
      uint16_t flag = 0x8000;
      if (i == 1) {
        payload = encode("A p5 A");
        flag = 0;
      } else {
        const std::string t = phraseText(i);
        payload.assign(t.begin(), t.end());
      }
      put16(r, 16 + k * 2, static_cast<uint16_t>(r.size() - 16));
      const uint16_t lenField = static_cast<uint16_t>(flag | payload.size());
      r.push_back(lenField >> 8);
      r.push_back(lenField & 0xFF);
      r.insert(r.end(), payload.begin(), payload.end());
    }
    recs.push_back(std::move(r));
  }
  return recs;
}

Fixture fixture(const std::string& text) {
  Fixture f;
  f.compression = mobi::COMPRESSION_HUFF;
  f.html = text;
  f.huffEncode = encode;
  f.textLength = static_cast<uint32_t>(expanded(text).size());
  f.huffRecords.push_back(huffRecord());
  for (Bytes& c : cdicRecords()) f.huffRecords.push_back(std::move(c));
  return f;
}

}  // namespace huff

TEST(HuffCdic, DecodesSyntheticDictionary) {
  const std::string text = "p2 A B p257 p100 A A p3 ";
  const Fixture f = huff::fixture(text);
  const Bytes file = buildMobi(f);
  mobi::Info info;
  ASSERT_EQ(mobi::open(readerFor(file), info), mobi::Status::Ok);
  EXPECT_EQ(info.compression, mobi::COMPRESSION_HUFF);
  EXPECT_EQ(info.huffRecordCount, 3u);

  for (size_t cacheLimit : {size_t(1) << 20, size_t(0)}) {
    const mobi::Reader r = readerFor(file);
    mobi::HuffCdic h;
    ASSERT_EQ(h.load(r, info, cacheLimit), mobi::Status::Ok) << "cache " << cacheLimit;
    EXPECT_EQ(h.cached(), cacheLimit != 0);
    EXPECT_EQ(h.phraseCount(), huff::PHRASES);
    const Bytes packed = huff::encode(text);
    uint8_t out[256];
    const size_t n = h.decompress(packed.data(), packed.size(), out, sizeof(out));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(out), n), huff::expanded(text)) << "cache " << cacheLimit;
    // Output too small: 0.
    EXPECT_EQ(h.decompress(packed.data(), packed.size(), out, 4), 0u);
  }

  mobi::Status status;
  const std::string doc = streamOf(file, &status);
  ASSERT_EQ(status, mobi::Status::Ok);
  EXPECT_EQ(body(doc), huff::expanded(text));
}

TEST(HuffCdic, RejectsDamagedTables) {
  const std::string text = "A";
  // No HUFF records at all although compression says HUFF: bad header, not a crash.
  Fixture none = huff::fixture(text);
  none.huffRecords.clear();
  mobi::Info info;
  EXPECT_EQ(mobi::open(readerFor(buildMobi(none)), info), mobi::Status::BadHeader);
  // Header fields left zero.
  Fixture noFields = huff::fixture(text);
  noFields.huffHeaderFields = false;
  EXPECT_EQ(mobi::open(readerFor(buildMobi(noFields)), info), mobi::Status::BadHeader);
  // Wrong magic in the CDIC record.
  Fixture badMagic = huff::fixture(text);
  memcpy(badMagic.huffRecords[1].data(), "XXXX", 4);
  Bytes file = buildMobi(badMagic);
  ASSERT_EQ(mobi::open(readerFor(file), info), mobi::Status::Ok);
  mobi::HuffCdic h;
  EXPECT_EQ(h.load(readerFor(file), info), mobi::Status::BadHeader);
  mobi::Status status;
  streamOf(file, &status);
  EXPECT_EQ(status, mobi::Status::BadHeader);
  // Phrase index past the dictionary: maxcode for the 10-bit codes far above the phrase count.
  Fixture badIndex = huff::fixture(text);
  put32(badIndex.huffRecords[0], 24 + 1024 + 9 * 8 + 4, 0x5000);
  file = buildMobi(badIndex);
  ASSERT_EQ(mobi::open(readerFor(file), info), mobi::Status::Ok);
  const mobi::Reader r1 = readerFor(file);
  ASSERT_EQ(h.load(r1, info), mobi::Status::Ok);
  const Bytes packed = huff::encode("p2");
  uint8_t out[16];
  EXPECT_EQ(h.decompress(packed.data(), packed.size(), out, sizeof(out)), 0u);
  // A phrase that codes itself must not recurse forever.
  Fixture loop = huff::fixture(text);
  {
    Bytes& c = loop.huffRecords[1];
    const uint16_t off = static_cast<uint16_t>((c[16 + 2] << 8) | c[16 + 3]);  // phrase 1
    const Bytes self = huff::encode("B");
    c[16 + off] = 0;
    c[16 + off + 1] = static_cast<uint8_t>(self.size());
    for (size_t k = 0; k < self.size(); k++) c[16 + off + 2 + k] = self[k];
  }
  file = buildMobi(loop);
  const mobi::Reader r2 = readerFor(file);
  ASSERT_EQ(h.load(r2, info), mobi::Status::Ok);
  const Bytes b = huff::encode("B");
  EXPECT_EQ(h.decompress(b.data(), b.size(), out, sizeof(out)), 0u);
}

TEST(MobiOpen, RefusesUnknownCompression) {
  Fixture f;
  f.html = "<p>x</p>";
  f.compression = 3;
  mobi::Info info;
  EXPECT_EQ(mobi::open(readerFor(buildMobi(f)), info), mobi::Status::UnsupportedCompression);
  EXPECT_STREQ(mobi::statusText(mobi::Status::UnsupportedCompression), "unsupported compression");
}

// A real HUFF/CDIC book, when VAULTINK_MOBI_HUFF_FILE names one (not checked in). With
// VAULTINK_MOBI_HUFF_OUT set, the streamed XHTML is written there for a look.
TEST(HuffCdic, RealBookWhenAvailable) {
  const char* path = std::getenv("VAULTINK_MOBI_HUFF_FILE");
  if (!path || !*path) GTEST_SKIP() << "VAULTINK_MOBI_HUFF_FILE not set";
  std::ifstream in(path, std::ios::binary);
  if (!in) GTEST_SKIP() << "cannot open " << path;
  const Bytes file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const mobi::Reader r = readerFor(file);
  mobi::Info info;
  ASSERT_EQ(mobi::open(r, info), mobi::Status::Ok);
  ASSERT_EQ(info.compression, mobi::COMPRESSION_HUFF);

  // Raw decompressed length must match the PalmDOC header's text length.
  mobi::HuffCdic h;
  ASSERT_EQ(h.load(r, info), mobi::Status::Ok);
  std::vector<uint8_t> raw(mobi::MAX_RECORD_SIZE + 64), text(mobi::MAX_RECORD_SIZE);
  size_t total = 0;
  std::string first;
  for (uint16_t i = 1; i <= info.textRecordCount; i++) {
    uint32_t offset, length;
    ASSERT_TRUE(mobi::recordSpan(r, i, offset, length));
    ASSERT_LE(length, raw.size());
    ASSERT_GT(r.readAt(r.ctx, offset, raw.data(), length), 0);
    length -= static_cast<uint32_t>(mobi::trailingSize(raw.data(), length, info.extraFlags));
    const size_t n = h.decompress(raw.data(), length, text.data(), text.size());
    ASSERT_GT(n, 0u) << "record " << i;
    if (first.size() < 2048) first.append(reinterpret_cast<char*>(text.data()), n);
    total += n;
  }
  EXPECT_EQ(total, info.textLength);
  EXPECT_NE(first.find("<html>"), std::string::npos);
  EXPECT_NE(first.find("<body"), std::string::npos);
  printf("HUFF/CDIC real book: %u phrases, cache %s (%zu B heap), %zu bytes of text\n", h.phraseCount(),
         h.cached() ? "yes" : "no", h.heapBytes(), total);
  printf("--- first 2 KB ---\n%.*s\n--- end ---\n", 2048, first.c_str());

  mobi::Status status;
  const std::string doc = streamOf(file, &status);
  EXPECT_EQ(status, mobi::Status::Ok);
  EXPECT_GT(body(doc).size(), total / 2);
  if (const char* outPath = std::getenv("VAULTINK_MOBI_HUFF_OUT")) {
    std::ofstream o(outPath, std::ios::binary);
    o << doc;
  }
}

// --- HTML filter ---

TEST(HtmlFilter, ClosesUnclosedParagraphsAndItems) {
  EXPECT_EQ(filterOnly("<p>one<p>two<div>three"), "<p>one</p><p>two</p><div>three</div>");
  EXPECT_EQ(filterOnly("<ul><li>a<li>b</ul>after"), "<ul><li>a</li><li>b</li></ul>after");
  EXPECT_EQ(filterOnly("<p>a<b>bold<i>both</p>plain"), "<p>a<b>bold<i>both</i></b></p>plain");
  EXPECT_EQ(filterOnly("</p></div>stray closes"), "stray closes");
}

TEST(HtmlFilter, DropsUnknownTagsAndAttributes) {
  EXPECT_EQ(filterOnly("<p class=x id=\"y\">text</p>"), "<p>text</p>");
  EXPECT_EQ(filterOnly("<guide><reference title=a/></guide><p>t</p>"), "<p>t</p>");
  EXPECT_EQ(filterOnly("<a href=\"#x\" filepos=0000012345>link</a>"), "<span>link</span>");
  EXPECT_EQ(filterOnly("<font size=3 color=red>f</font>"), "<span>f</span>");
  EXPECT_EQ(filterOnly("<center>c</center>"), "<div>c</div>");
  EXPECT_EQ(filterOnly("<p align=center>c</p><p align=\"RIGHT\">r</p>"),
            "<p style=\"text-align:center\">c</p><p>r</p>");
  EXPECT_EQ(filterOnly("<br><hr><br/>"), "<br /><hr /><br />");
  EXPECT_EQ(filterOnly("<!DOCTYPE html><?xml version=\"1.0\"?><!-- a -- comment --><p>x</p>"), "<p>x</p>");
}

TEST(HtmlFilter, EntitiesAndBareAmpersands) {
  EXPECT_EQ(filterOnly("a &amp; b &lt; c &gt; d"), "a &amp; b &lt; c &gt; d");
  EXPECT_EQ(filterOnly("Tom & Jerry"), "Tom &amp; Jerry");
  EXPECT_EQ(filterOnly("AT&T; x"), "AT&amp;T; x");
  EXPECT_EQ(filterOnly("&nbsp;&mdash;&eacute;"), "\xC2\xA0\xE2\x80\x94\xC3\xA9");
  EXPECT_EQ(filterOnly("&#169; &#x2014; &#147;"), "&#169; &#x2014; \xE2\x80\x9C");
  EXPECT_EQ(filterOnly("&#1; &#xD800; &#99999999;"), "  ");
  EXPECT_EQ(filterOnly("5 > 3"), "5 &gt; 3");
  EXPECT_EQ(filterOnly("<p>a < b <b>x</b></p>"), "<p>a &lt; b <b>x</b></p>");
  EXPECT_EQ(filterOnly("<p>1 <2 && 3<4</p>"), "<p>1 &lt;2 &amp;&amp; 3&lt;4</p>");
  EXPECT_EQ(filterOnly("trailing &"), "trailing &amp;");
}

TEST(HtmlFilter, SplitsAcrossChunksIdentically) {
  const std::string in =
      "<html><head><title>Skip me</title></head><body><p align=\"center\">Hello &ldquo;world&rdquo;<br/>"
      "<b>bold</b> text<mbp:pagebreak/><h2>Two</h2><p>para &#8212; end<img recindex=\"1\"></body></html>";
  const std::string whole = filterOnly(in);
  for (size_t chunk : {1u, 2u, 3u, 7u, 13u}) EXPECT_EQ(filterOnly(in, chunk), whole) << "chunk " << chunk;
  EXPECT_EQ(whole,
            "<p style=\"text-align:center\">Hello \xE2\x80\x9Cworld\xE2\x80\x9D<br /><b>bold</b> text</p>\n"
            "<h2>Two</h2><p>para &#8212; end</p>");
}

TEST(HtmlFilter, DeepNestingAndHugeTagsSurvive) {
  std::string in;
  for (int i = 0; i < 100; i++) in += "<div>";
  in += "deep";
  for (int i = 0; i < 100; i++) in += "</div>";
  const std::string out = filterOnly(in);
  size_t opens = 0;
  for (size_t at = out.find("<div>"); at != std::string::npos; at = out.find("<div>", at + 5)) opens++;
  size_t closes = 0;
  for (size_t at = out.find("</div>"); at != std::string::npos; at = out.find("</div>", at + 6)) closes++;
  EXPECT_EQ(opens, closes);
  EXPECT_LE(opens, 24u);
  EXPECT_NE(out.find("deep"), std::string::npos);

  const std::string huge = "<p " + std::string(1000, 'x') + ">text</p>";
  EXPECT_EQ(filterOnly(huge), "text");
}

}  // namespace
