#include <gtest/gtest.h>

#include <string>

#include "CodePage.h"

using codepage::Decoder;
using codepage::Id;

namespace {

std::string decode(const Id id, const std::string& bytes, const size_t step = 4096) {
  Decoder decoder(id);
  std::string out;
  for (size_t at = 0; at < bytes.size(); at += step) {
    const size_t n = std::min(step, bytes.size() - at);
    std::string piece(n * Decoder::MAX_OUT_PER_BYTE + Decoder::MAX_PENDING_OUT, '\0');
    piece.resize(decoder.feed(reinterpret_cast<const uint8_t*>(bytes.data()) + at, n, piece.data()));
    out += piece;
  }
  char tail[Decoder::MAX_PENDING_OUT];
  out.append(tail, decoder.finish(tail));
  return out;
}

}  // namespace

TEST(CodePage, NamesAndNumbers) {
  Id id = Id::Utf8;
  EXPECT_TRUE(codepage::fromName("Windows-1253", id));
  EXPECT_EQ(id, Id::Cp1253);
  EXPECT_TRUE(codepage::fromName("ISO-8859-7", id));
  EXPECT_EQ(id, Id::Iso8859_7);
  EXPECT_TRUE(codepage::fromName("UTF-8", id));
  EXPECT_EQ(id, Id::Utf8);
  EXPECT_TRUE(codepage::fromName("windows-1251", id));
  EXPECT_EQ(id, Id::Cp1251);
  EXPECT_FALSE(codepage::fromName("shift_jis", id));
  EXPECT_FALSE(codepage::fromName("a-name-far-longer-than-any-known-here", id));
  EXPECT_TRUE(codepage::fromWindowsNumber(1253, id));
  EXPECT_EQ(id, Id::Cp1253);
  EXPECT_FALSE(codepage::fromWindowsNumber(932, id));
  EXPECT_TRUE(codepage::fromRtfCharset(161, id));
  EXPECT_EQ(id, Id::Cp1253);
  EXPECT_FALSE(codepage::fromRtfCharset(0, id));
}

TEST(CodePage, GreekPages) {
  // "Καλημέρα" in Windows-1253; the accented Ά differs between the two Greek pages.
  EXPECT_EQ(decode(Id::Cp1253, "\xCA\xE1\xEB\xE7\xEC\xDD\xF1\xE1"), "Καλημέρα");
  EXPECT_EQ(decode(Id::Iso8859_7, "\xCA\xE1\xEB\xE7\xEC\xDD\xF1\xE1"), "Καλημέρα");
  EXPECT_EQ(decode(Id::Cp1253, "\xA2"), "Ά");
  EXPECT_EQ(decode(Id::Iso8859_7, "\xB6"), "Ά");
  EXPECT_EQ(decode(Id::Iso8859_7, "\xA4"), "€");
  EXPECT_EQ(decode(Id::Cp1253, "\x80"), "€");
  EXPECT_EQ(decode(Id::Cp1253, "a\xD2z"), "az");  // 0xD2 is undefined: dropped
}

TEST(CodePage, WesternAndCyrillic) {
  EXPECT_EQ(decode(Id::Cp1252, "caf\xE9 \x93q\x94 \x97"), "café “q” —");
  EXPECT_EQ(decode(Id::Iso8859_1, "caf\xE9\x93"), "café");  // 0x93 is a control character in Latin-1
  EXPECT_EQ(decode(Id::Cp1251, "\xCF\xF0\xE8\xE2\xE5\xF2 \xB8"), "Привет ё");
}

TEST(CodePage, Utf8PassesAndFallsBackToWindows1252) {
  EXPECT_EQ(decode(Id::Utf8, "Καλημέρα 😀"), "Καλημέρα 😀");
  EXPECT_EQ(decode(Id::Utf8, "caf\xE9 bar"), "café bar");       // a lone Latin-1 byte
  EXPECT_EQ(decode(Id::Utf8, "\x93quote\x94"), "“quote”");      // Windows-1252 quotes
  EXPECT_EQ(decode(Id::Utf8, "a\xC3"), "aÃ");                   // cut off at the end
  EXPECT_EQ(decode(Id::Utf8, "\xC0\xAFz"), "À¯z");              // overlong: not UTF-8
  EXPECT_EQ(decode(Id::Utf8, "\xED\xA0\x80z"), "í\xC2\xA0€z");  // a surrogate: not UTF-8
}

TEST(CodePage, ControlCharactersAreDropped) {
  EXPECT_EQ(decode(Id::Utf8, std::string("a\0b\x01\x1F\tc\nd", 9)), "ab\tc\nd");
  EXPECT_EQ(decode(Id::Utf8, "x\xEF\xBF\xBEy"), "xy");  // U+FFFE
}

TEST(CodePage, EveryChunkingGivesTheSameText) {
  const std::string text = "Ελληνικά κείμενο — “εισαγωγικά” 😀 caf\xE9 τέλος";
  const std::string whole = decode(Id::Utf8, text);
  for (size_t step = 1; step <= 7; step++) EXPECT_EQ(decode(Id::Utf8, text, step), whole) << "step " << step;
}

TEST(CodePage, EncodeUtf8) {
  char out[4];
  EXPECT_EQ(codepage::encodeUtf8(0x41, out), 1u);
  EXPECT_EQ(codepage::encodeUtf8(0x3B1, out), 2u);
  EXPECT_EQ(std::string(out, 2), "α");
  EXPECT_EQ(codepage::encodeUtf8(0x20AC, out), 3u);
  EXPECT_EQ(codepage::encodeUtf8(0x1F600, out), 4u);
  EXPECT_EQ(std::string(out, 4), "😀");
  EXPECT_EQ(codepage::encodeUtf8(0xD800, out), 0u);
  EXPECT_EQ(codepage::encodeUtf8(0x110000, out), 0u);
}
