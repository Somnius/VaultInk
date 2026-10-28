#include "LiveSyncCrypto.h"

#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>

#include <cctype>
#include <cstring>

namespace livesync {
namespace {

constexpr size_t IV_LEN = 12;
constexpr size_t HKDF_SALT_LEN = 32;
constexpr size_t PBKDF2_SALT_LEN = 32;
constexpr size_t TAG_LEN = 16;

void (*defaultYield)(int, void*) = nullptr;
void* defaultYieldCtx = nullptr;

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

std::string base64Encode(const uint8_t* data, const size_t len) {
  static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t n =
        (static_cast<uint32_t>(data[i]) << 16) | (i + 1 < len ? data[i + 1] << 8 : 0) | (i + 2 < len ? data[i + 2] : 0);
    out += ALPHABET[(n >> 18) & 63];
    out += ALPHABET[(n >> 12) & 63];
    out += i + 1 < len ? ALPHABET[(n >> 6) & 63] : '=';
    out += i + 2 < len ? ALPHABET[n & 63] : '=';
  }
  return out;
}

bool base64Decode(std::string_view in, std::string& out) {
  // Standard alphabet with '=' padding (no whitespace, no URL-safe forms: LiveSync writes neither).
  const auto value = [](const char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  out.clear();
  if (in.size() % 4 != 0) return false;
  out.reserve(in.size() / 4 * 3);
  for (size_t i = 0; i < in.size(); i += 4) {
    int v[4];
    int pad = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = in[i + k];
      if (c == '=' && i + 4 == in.size() && k >= 2) {
        v[k] = 0;
        ++pad;
        continue;
      }
      if (pad || (v[k] = value(c)) < 0) return false;
    }
    const uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
    out += static_cast<char>(n >> 16);
    if (pad < 2) out += static_cast<char>((n >> 8) & 255);
    if (pad < 1) out += static_cast<char>(n & 255);
  }
  return true;
}

void setDefaultYield(void (*yield)(int, void*), void* ctx) {
  defaultYield = yield;
  defaultYieldCtx = ctx;
}

bool deriveMaster(std::string_view passphrase, const uint8_t* salt, const size_t saltLen, Key out,
                  void (*yield)(int, void*), void* ctx) {
  if (!yield) {
    yield = defaultYield;
    ctx = defaultYieldCtx;
  }
  // PBKDF2-HMAC-SHA256 for one 32-byte block (what LiveSync uses), written out so it can yield:
  // U1 = HMAC(P, salt | 00000001), Ui = HMAC(P, Ui-1), out = U1 ^ ... ^ Uc.
  mbedtls_md_context_t md;
  mbedtls_md_init(&md);
  bool ok =
      mbedtls_md_setup(&md, sha256(), 1) == 0 &&
      mbedtls_md_hmac_starts(&md, reinterpret_cast<const unsigned char*>(passphrase.data()), passphrase.size()) == 0;
  uint8_t u[32];
  static constexpr uint8_t BLOCK_1[4] = {0, 0, 0, 1};
  ok = ok && mbedtls_md_hmac_update(&md, salt, saltLen) == 0 && mbedtls_md_hmac_update(&md, BLOCK_1, 4) == 0 &&
       mbedtls_md_hmac_finish(&md, u) == 0;
  if (ok) memcpy(out, u, sizeof(u));
  for (int i = 1; ok && i < PBKDF2_ITERATIONS; ++i) {
    ok = mbedtls_md_hmac_reset(&md) == 0 && mbedtls_md_hmac_update(&md, u, sizeof(u)) == 0 &&
         mbedtls_md_hmac_finish(&md, u) == 0;
    for (size_t j = 0; j < sizeof(u); ++j) out[j] ^= u[j];
    if (yield && (i + 1) % 1000 == 0) yield(i + 1, ctx);
  }
  mbedtls_md_free(&md);
  mbedtls_platform_zeroize(u, sizeof(u));
  return ok;
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

bool encrypt(const Key master, std::string_view plain, const RandomFill& random, std::string& out) {
  if (!random) return false;
  std::string box(IV_LEN + HKDF_SALT_LEN + plain.size() + TAG_LEN, '\0');
  auto* iv = reinterpret_cast<uint8_t*>(box.data());
  uint8_t* hkdfSalt = iv + IV_LEN;
  uint8_t* ct = hkdfSalt + HKDF_SALT_LEN;
  uint8_t* tag = ct + plain.size();
  random(iv, IV_LEN);
  random(hkdfSalt, HKDF_SALT_LEN);
  uint8_t key[32];
  if (mbedtls_hkdf(sha256(), hkdfSalt, HKDF_SALT_LEN, master, 32, nullptr, 0, key, sizeof(key)) != 0) return false;
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plain.size(), iv, IV_LEN, nullptr, 0,
                                   reinterpret_cast<const uint8_t*>(plain.data()), ct, TAG_LEN, tag);
  }
  mbedtls_gcm_free(&gcm);
  mbedtls_platform_zeroize(key, sizeof(key));
  if (rc != 0) return false;
  out = "%=" + base64Encode(reinterpret_cast<const uint8_t*>(box.data()), box.size());
  return true;
}

bool encryptMetaPath(const Key master, std::string_view json, const RandomFill& random, std::string& field) {
  std::string box;
  if (!encrypt(master, json, random, box)) return false;
  field = "/\\:" + box;
  return true;
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
