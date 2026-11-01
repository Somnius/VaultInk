#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "LzString.h"
#include "LzStringVectors.inc"

namespace {

uint32_t fnv1a(const std::string& s) {
  uint32_t h = 0x811C9DC5u;
  for (const char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 0x01000193u;
  }
  return h;
}

std::string decode(const std::string& b64) {
  std::string out;
  EXPECT_TRUE(lzstring::decompressFromBase64(b64, out));
  return out;
}

}  // namespace

TEST(LzString, KnownVectors) {
  EXPECT_EQ(decode(LZ_HELLO_B64), LZ_HELLO_PLAIN);
  EXPECT_EQ(decode(LZ_GREEK_B64), LZ_GREEK_PLAIN);  // Greek, an emoji (surrogate pair), é
  EXPECT_EQ(decode(LZ_EMPTY_OBJ_B64), LZ_EMPTY_OBJ_PLAIN);
  EXPECT_EQ(decode(LZ_REPEAT_B64), LZ_REPEAT_PLAIN);  // 500 × 'a': dictionary growth and KwKwK entries
}

TEST(LzString, LinesAndWhitespaceAreIgnored) {
  EXPECT_EQ(decode(LZ_GREEK_DOC_B64_LINES), LZ_GREEK_DOC_PLAIN);
  std::string spaced;
  for (const char* p = LZ_HELLO_B64; *p; ++p) {
    spaced += *p;
    spaced += " \r\n\t";
  }
  EXPECT_EQ(decode("\n  " + spaced), LZ_HELLO_PLAIN);
}

TEST(LzString, LargeDocument) {
  const std::string json = decode(LZ_BIG_DOC_B64_LINES);
  EXPECT_EQ(json.size(), LZ_BIG_DOC_PLAIN_BYTES);
  EXPECT_EQ(fnv1a(json), LZ_BIG_DOC_PLAIN_FNV1A);
  EXPECT_EQ(json.substr(0, 20), R"({"type":"excalidraw")");
}

TEST(LzString, EmptyAndGarbageDoNotCrash) {
  std::string out = "stale";
  EXPECT_FALSE(lzstring::decompressFromBase64("", out));  // lz-string gives null for an empty input
  EXPECT_TRUE(out.empty());
  out = "stale";
  EXPECT_FALSE(lzstring::decompressFromBase64("not base64 at all!", out));  // '!' is outside the alphabet
  EXPECT_TRUE(out.empty());
  // A cut real stream: lz-string (and its Python port) return what was decoded before the cut.
  EXPECT_TRUE(lzstring::decompressFromBase64("N4KAkARALgngDgUwgLgAQBQjtBECGAzgLYCWANgIYBWAvgHQD2AVgA==", out));
  EXPECT_EQ(out, std::string("{\n\t\"type\": \x14"));
  for (const char* garbage : {"A", "AAAA", "////////", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz", "+/+/+/+/+/+/+/+/", "="}) {
    lzstring::decompressFromBase64(garbage, out);  // either answer, no crash
  }
  // Every truncation of a valid stream stays within bounds.
  const std::string whole = LZ_GREEK_DOC_B64_LINES;
  for (size_t n = 0; n < whole.size(); n += 7) lzstring::decompressFromBase64(whole.substr(0, n), out);
  // Every byte of a valid stream flipped.
  std::string flipped = LZ_GREEK_B64;
  for (size_t i = 0; i < flipped.size(); ++i) {
    const char keep = flipped[i];
    flipped[i] = keep == 'A' ? 'B' : 'A';
    lzstring::decompressFromBase64(flipped, out);
    flipped[i] = keep;
  }
}

TEST(LzString, InputLimit) {
  std::string out;
  const std::string tooLong(lzstring::MAX_INPUT + 1, 'A');
  EXPECT_FALSE(lzstring::decompressFromBase64(tooLong, out));
  EXPECT_TRUE(out.empty());
}
