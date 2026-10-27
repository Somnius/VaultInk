#include "LiveSyncCrypto.h"

#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/platform_util.h>

#include <cctype>
#include <cstring>

namespace livesync {
namespace {

constexpr size_t IV_LEN = 12;
constexpr size_t HKDF_SALT_LEN = 32;
constexpr size_t PBKDF2_SALT_LEN = 32;
constexpr size_t TAG_LEN = 16;

const mbedtls_md_info_t* sha256() { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); }

// iv | hkdfSalt | ciphertext | tag, with the master key.
Result openBox(const Key master, const uint8_t* box, const size_t len, std::string& plain) {
  if (len < IV_LEN + HKDF_SALT_LEN + TAG_LEN) return Result::BadFormat;
  const uint8_t* iv = box;
  const uint8_t* hkdfSalt = box + IV_LEN;
  const uint8_t* ct = hkdfSalt + HKDF_SALT_LEN;
  const size_t ctLen = len - IV_LEN - HKDF_SALT_LEN - TAG_LEN;
  const uint8_t* tag = ct + ctLen;
  uint8_t key[32];
  if (mbedtls_hkdf(sha256(), hkdfSalt, HKDF_SALT_LEN, master, 32, nullptr, 0, key, sizeof(key)) != 0) {
    return Result::BadFormat;
  }
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  plain.assign(ctLen, '\0');
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv, IV_LEN, nullptr, 0, tag, TAG_LEN, ct,
                                  reinterpret_cast<uint8_t*>(plain.data()));
  }
  mbedtls_gcm_free(&gcm);
  mbedtls_platform_zeroize(key, sizeof(key));
  if (rc != 0) {
    plain.clear();
    return rc == MBEDTLS_ERR_GCM_AUTH_FAILED ? Result::WrongKey : Result::BadFormat;
  }
  return Result::Ok;
}

// "%=..." / "%$..." prefix check; legacy formats are refused.
Result prefixed(std::string_view s, const char second, std::string& bytes) {
  if (s.size() < 2 || s[0] != '%' || s[1] != second) {
    return (!s.empty() && (s[0] == '%' || s[0] == '[')) ? Result::Unsupported : Result::BadFormat;
  }
  return base64Decode(s.substr(2), bytes) ? Result::Ok : Result::BadFormat;
}

std::string hex(const uint8_t* p, const size_t n) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    out += digits[p[i] >> 4];
    out += digits[p[i] & 15];
  }
  return out;
}

// One URL-decoding pass (%XX and '+' as space are what encodeURIComponent can leave).
std::string urlDecode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

}  // namespace

bool base64Decode(std::string_view in, std::string& out) {
  size_t len = 0;
  const auto* src = reinterpret_cast<const unsigned char*>(in.data());
  if (mbedtls_base64_decode(nullptr, 0, &len, src, in.size()) == MBEDTLS_ERR_BASE64_INVALID_CHARACTER) return false;
  out.assign(len, '\0');
  if (mbedtls_base64_decode(reinterpret_cast<unsigned char*>(out.data()), out.size(), &len, src, in.size()) != 0) {
    return false;
  }
  out.resize(len);
  return true;
}

bool deriveMaster(std::string_view passphrase, const uint8_t* salt, const size_t saltLen, Key out) {
  return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char*>(passphrase.data()),
                                       passphrase.size(), salt, saltLen, PBKDF2_ITERATIONS, 32, out) == 0;
}

Result decrypt(const Key master, std::string_view encrypted, std::string& plain) {
  std::string box;
  const Result r = prefixed(encrypted, '=', box);
  if (r != Result::Ok) return r;
  return openBox(master, reinterpret_cast<const uint8_t*>(box.data()), box.size(), plain);
}

Result decryptWithPassphrase(std::string_view passphrase, std::string_view encrypted, std::string& plain) {
  std::string box;
  const Result r = prefixed(encrypted, '$', box);
  if (r != Result::Ok) return r;
  if (box.size() < PBKDF2_SALT_LEN) return Result::BadFormat;
  Key master;
  if (!deriveMaster(passphrase, reinterpret_cast<const uint8_t*>(box.data()), PBKDF2_SALT_LEN, master)) {
    return Result::BadFormat;
  }
  const Result out = openBox(master, reinterpret_cast<const uint8_t*>(box.data()) + PBKDF2_SALT_LEN,
                             box.size() - PBKDF2_SALT_LEN, plain);
  mbedtls_platform_zeroize(master, sizeof(master));
  return out;
}

Result decryptMetaPath(const Key master, std::string_view field, std::string& json) {
  static constexpr std::string_view PREFIX = "/\\:";
  if (field.substr(0, PREFIX.size()) != PREFIX) return Result::BadFormat;
  return decrypt(master, field.substr(PREFIX.size()), json);
}

Result decodeSetupUri(std::string_view uri, std::string_view passphrase, const int64_t weekWindow, std::string& json) {
  static constexpr std::string_view PREFIX = "obsidian://setuplivesync?settings=";
  if (uri.substr(0, PREFIX.size()) == PREFIX) uri.remove_prefix(PREFIX.size());
  while (!uri.empty() && (uri.back() == ' ' || uri.back() == '\n' || uri.back() == '\r')) uri.remove_suffix(1);
  const std::string blob = urlDecode(uri);
  // Time-bound form first: hex(HMAC-SHA256(SHA256(pass), '["livesync/setup-uri","tb1","ephemeral",<week>]')).
  Result last = Result::WrongKey;
  if (weekWindow >= 0) {
    uint8_t passHash[32], mac[32];
    mbedtls_md(sha256(), reinterpret_cast<const unsigned char*>(passphrase.data()), passphrase.size(), passHash);
    const std::string msg = "[\"livesync/setup-uri\",\"tb1\",\"ephemeral\"," + std::to_string(weekWindow) + "]";
    mbedtls_md_hmac(sha256(), passHash, sizeof(passHash), reinterpret_cast<const unsigned char*>(msg.data()),
                    msg.size(), mac);
    last = decryptWithPassphrase(hex(mac, sizeof(mac)), blob, json);
    mbedtls_platform_zeroize(passHash, sizeof(passHash));
    if (last == Result::Ok) return last;
  }
  const Result persistent = decryptWithPassphrase(passphrase, blob, json);
  return persistent == Result::WrongKey && last != Result::WrongKey ? last : persistent;
}

}  // namespace livesync
