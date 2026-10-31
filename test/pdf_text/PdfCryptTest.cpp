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

TEST(PdfCrypt, Sha2KnownValues) {
  auto h256 = [](const std::string& s) {
    uint8_t out[32];
    pdf::crypt::sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out);
    return hex(std::string(reinterpret_cast<char*>(out), 32));
  };
  auto h384 = [](const std::string& s) {
    uint8_t out[48];
    pdf::crypt::sha384(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out);
    return hex(std::string(reinterpret_cast<char*>(out), 48));
  };
  auto h512 = [](const std::string& s) {
    uint8_t out[64];
    pdf::crypt::sha512(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out);
    return hex(std::string(reinterpret_cast<char*>(out), 64));
  };
  EXPECT_EQ(h256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(h256(std::string(200, 'a')), "c2a908d98f5df987ade41b5fce213067efbcc21ef2240212a41e54b5e7c28ae5");
  EXPECT_EQ(h384("abc"),
            "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
  EXPECT_EQ(h384(std::string(200, 'a')),
            "0691b6e978614b67d60557b2a2cddd53406508522efa21c624dbbfa8ab6e726d5c586b489c7c09f24109a64c10211d48");
  EXPECT_EQ(h512("abc"),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d442364"
            "3ce80e2a9ac94fa54ca49f");
  EXPECT_EQ(h512(std::string(200, 'a')),
            "4b11459c33f52a22ee8236782714c150a3b2c60994e9acee17fe68947a3e6789f31e7668394592da7bef827cddca88c4e6f86e4df7"
            "ed1ae6cba71f3e98faee9f");
  EXPECT_EQ(h256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(PdfCrypt, Aes256AndAes128EncryptAndDecrypt) {
  const std::string key = fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
  const std::string iv = fromHex("101112131415161718191a1b1c1d1e1f");
  const std::string plain = "0123456789abcdef0123456789abcdef";
  // openssl enc -aes-256-cbc -nopad
  const std::string cipher256 = fromHex("879adb94b99e35f6f6d9680548bb71d1afefd5df6a649b8544b947361abb1e36");
  std::string out;
  ASSERT_TRUE(pdf::crypt::aes256CbcDecryptRaw(reinterpret_cast<const uint8_t*>(key.data()),
                                              reinterpret_cast<const uint8_t*>(iv.data()),
                                              reinterpret_cast<const uint8_t*>(cipher256.data()), 32, out));
  EXPECT_EQ(out, plain);
  // openssl enc -aes-128-cbc -nopad (first 16 bytes of the key)
  std::string cipher(32, '\0');
  pdf::crypt::aes128CbcEncryptRaw(
      reinterpret_cast<const uint8_t*>(key.data()), reinterpret_cast<const uint8_t*>(iv.data()),
      reinterpret_cast<const uint8_t*>(plain.data()), 32, reinterpret_cast<uint8_t*>(cipher.data()));
  EXPECT_EQ(hex(cipher), "eb9e5ba41b902db8252982aa1a23f4be916765b29ca3a2e2728e432e670f496d");
  // Bad lengths are refused.
  EXPECT_FALSE(pdf::crypt::aes256CbcDecryptRaw(reinterpret_cast<const uint8_t*>(key.data()),
                                               reinterpret_cast<const uint8_t*>(iv.data()),
                                               reinterpret_cast<const uint8_t*>(cipher256.data()), 31, out));
  EXPECT_FALSE(pdf::crypt::aes256CbcDecrypt(reinterpret_cast<const uint8_t*>(key.data()),
                                            reinterpret_cast<const uint8_t*>(cipher256.data()), 16, out));
}
