#include <gtest/gtest.h>

#include <string>

#include "PdfCrypt.h"

namespace {

std::string hex(const std::string& s) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (const unsigned char c : s) {
    out += digits[c >> 4];
    out += digits[c & 15];
  }
  return out;
}

std::string fromHex(const std::string& h) {
  std::string out;
  for (size_t i = 0; i + 1 < h.size(); i += 2) out += static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16));
  return out;
}

}  // namespace

TEST(PdfCrypt, Md5KnownValues) {
  EXPECT_EQ(hex(pdf::crypt::md5("")), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(hex(pdf::crypt::md5("abc")), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_EQ(hex(pdf::crypt::md5("The quick brown fox jumps over the lazy dog")), "9e107d9d372bb6826bd81d3542a419d6");
  // Two blocks of padding: 56 bytes is the first length that needs a second block for the tail.
  EXPECT_EQ(hex(pdf::crypt::md5(std::string(56, 'a'))), "3b0c8ac703f828b04c6c197006d17218");
  EXPECT_EQ(hex(pdf::crypt::md5(std::string(1000, 'a'))), "cabe45dcc9ae5b66ba86600cca6b8ba8");
}

TEST(PdfCrypt, Rc4KnownValue) {
  std::string data = "Plaintext";
  pdf::crypt::rc4(reinterpret_cast<const uint8_t*>("Key"), 3, reinterpret_cast<uint8_t*>(data.data()), data.size());
  EXPECT_EQ(hex(data), "bbf316e8d940af0ad3");
  pdf::crypt::rc4(reinterpret_cast<const uint8_t*>("Key"), 3, reinterpret_cast<uint8_t*>(data.data()), data.size());
  EXPECT_EQ(data, "Plaintext");  // the same call undoes itself
}

TEST(PdfCrypt, Aes128CbcDecrypts) {
  const std::string key = fromHex("000102030405060708090a0b0c0d0e0f");
  const std::string iv = fromHex("101112131415161718191a1b1c1d1e1f");
  // openssl enc -aes-128-cbc of "Hello PDF world!!" (17 bytes, so a second block of padding).
  const std::string cipher = fromHex("8401b1ee7a7d593855237324e24e876bc2044e055e767bc21b631fa9ce7e4e69");
  std::string out;
  ASSERT_TRUE(pdf::crypt::aes128CbcDecrypt(reinterpret_cast<const uint8_t*>(key.data()),
                                           reinterpret_cast<const uint8_t*>((iv + cipher).data()), 16 + cipher.size(),
                                           out));
  EXPECT_EQ(out, "Hello PDF world!!");
  // A wrong length or broken padding is refused.
  const std::string whole = iv + cipher;
  EXPECT_FALSE(pdf::crypt::aes128CbcDecrypt(reinterpret_cast<const uint8_t*>(key.data()),
                                            reinterpret_cast<const uint8_t*>(whole.data()), whole.size() - 1, out));
  std::string broken = whole;
  broken.back() ^= 0x55;
  EXPECT_FALSE(pdf::crypt::aes128CbcDecrypt(reinterpret_cast<const uint8_t*>(key.data()),
                                            reinterpret_cast<const uint8_t*>(broken.data()), broken.size(), out));
}
