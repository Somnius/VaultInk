#include "SshKnownHosts.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace sshkh {

namespace {
constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(const char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// A host name or address as typed: letters, digits and . - _ : (IPv6), nothing that could start
// another field or line.
bool hostIsPlain(const std::string_view host) {
  if (host.empty() || host.size() > 253) return false;
  return std::all_of(host.begin(), host.end(), [](const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
           c == '_' || c == ':';
  });
}

struct Line {
  std::string_view host;
  std::string_view type;
  std::string_view key;
};

// One line into its three fields. False for a blank line, a comment, a marker line ("@revoked")
// or a hashed host ("|1|..."): those are kept in the file but never match.
bool parseLine(std::string_view line, Line& out) {
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  if (line.empty() || line.front() == '#' || line.front() == '@' || line.front() == '|') return false;
  std::string_view fields[3];
  size_t count = 0;
  while (!line.empty() && count < 3) {
    size_t end = 0;
    while (end < line.size() && line[end] != ' ' && line[end] != '\t') ++end;
    fields[count++] = line.substr(0, end);
    line.remove_prefix(end);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  }
  if (count < 3) return false;
  out.host = fields[0];
  out.type = fields[1];
  out.key = fields[2];
  return true;
}

template <typename Fn>
void forEachLine(const std::string_view text, Fn&& fn) {
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    fn(start, end);
    start = end + 1;
  }
}

bool sameHost(const std::string_view field, const std::string& wanted) {
  if (field.size() != wanted.size()) return false;
  for (size_t i = 0; i < field.size(); ++i) {
    if (lower(field[i]) != wanted[i]) return false;
  }
  return true;
}
}  // namespace

std::string base64Encode(const uint8_t* data, const size_t len, const bool padding) {
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t b0 = data[i];
    const uint32_t b1 = i + 1 < len ? data[i + 1] : 0;
    const uint32_t b2 = i + 2 < len ? data[i + 2] : 0;
    const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
    out += ALPHABET[(triple >> 18) & 0x3F];
    out += ALPHABET[(triple >> 12) & 0x3F];
    if (i + 1 < len) {
      out += ALPHABET[(triple >> 6) & 0x3F];
    } else if (padding) {
      out += '=';
    }
    if (i + 2 < len) {
      out += ALPHABET[triple & 0x3F];
    } else if (padding) {
      out += '=';
    }
  }
  return out;
}

bool base64Decode(std::string_view text, std::vector<uint8_t>& out) {
  out.clear();
  size_t pad = 0;
  while (!text.empty() && text.back() == '=' && pad < 2) {
    text.remove_suffix(1);
    ++pad;
  }
  if ((text.size() + pad) % 4 != 0 || text.size() % 4 == 1) return false;
  out.reserve(text.size() * 3 / 4);
  uint32_t acc = 0;
  int bits = 0;
  for (const char c : text) {
    const int v = base64Value(c);
    if (v < 0) return false;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
    }
  }
  // The bits left over must be zero, or two texts would decode to the same key.
  return (acc & ((1u << bits) - 1)) == 0;
}

std::string hostField(const std::string_view host, const uint16_t port) {
  std::string name;
  name.reserve(host.size() + 10);
  for (const char c : host) name += lower(c);
  if (port == 22 || port == 0) return name;
  return "[" + name + "]:" + std::to_string(port);
}

std::string keyType(const uint8_t* keyBlob, const size_t keyBlobLen) {
  if (!keyBlob || keyBlobLen < 4 || keyBlobLen > MAX_KEY_BLOB_BYTES) return {};
  const uint32_t len = (static_cast<uint32_t>(keyBlob[0]) << 24) | (static_cast<uint32_t>(keyBlob[1]) << 16) |
                       (static_cast<uint32_t>(keyBlob[2]) << 8) | keyBlob[3];
  if (len == 0 || len > 64 || len > keyBlobLen - 4 || keyBlobLen - 4 - len == 0) return {};
  std::string type(reinterpret_cast<const char*>(keyBlob + 4), len);
  for (const char c : type) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '@' || c == '.';
    if (!ok) return {};
  }
  return type;
}

Verdict check(const std::string_view knownHosts, const std::string_view host, const uint16_t port,
              const uint8_t* keyBlob, const size_t keyBlobLen) {
  const std::string wanted = hostField(host, port);
  const std::string type = keyType(keyBlob, keyBlobLen);
  const std::string key = type.empty() ? std::string() : base64Encode(keyBlob, keyBlobLen);
  bool hostSeen = false;
  bool match = false;
  forEachLine(knownHosts, [&](const size_t start, const size_t end) {
    Line line;
    if (!parseLine(knownHosts.substr(start, end - start), line) || !sameHost(line.host, wanted)) return;
    hostSeen = true;
    if (!type.empty() && line.type == type && line.key == key) match = true;
  });
  // A blob that is not a key never matches; with a pinned line it is a mismatch, without one it is
  // unknown and add() refuses it.
  if (match) return Verdict::Match;
  return hostSeen ? Verdict::Mismatch : Verdict::Unknown;
}

bool add(std::string& knownHosts, const std::string_view host, const uint16_t port, const uint8_t* keyBlob,
         const size_t keyBlobLen) {
  const std::string type = keyType(keyBlob, keyBlobLen);
  if (type.empty() || !hostIsPlain(host)) return false;
  size_t lines = 0;
  forEachLine(knownHosts, [&](const size_t start, const size_t end) {
    Line line;
    if (parseLine(std::string_view(knownHosts).substr(start, end - start), line)) ++lines;
  });
  std::string entry = hostField(host, port) + " " + type + " " + base64Encode(keyBlob, keyBlobLen) + "\n";
  if (lines >= MAX_ENTRIES || knownHosts.size() + entry.size() + 1 > MAX_TEXT_BYTES) return false;
  if (!knownHosts.empty() && knownHosts.back() != '\n') knownHosts += '\n';
  knownHosts += entry;
  return true;
}

size_t remove(std::string& knownHosts, const std::string_view host, const uint16_t port) {
  const std::string wanted = hostField(host, port);
  std::string kept;
  kept.reserve(knownHosts.size());
  size_t removed = 0;
  const std::string_view text(knownHosts);
  forEachLine(text, [&](const size_t start, const size_t end) {
    const std::string_view raw = text.substr(start, end - start);
    Line line;
    if (parseLine(raw, line) && sameHost(line.host, wanted)) {
      ++removed;
      return;
    }
    kept.append(raw);
    kept += '\n';
  });
  if (removed > 0) knownHosts.swap(kept);
  return removed;
}

std::string canonical(const std::string_view knownHosts) {
  std::string out;
  forEachLine(knownHosts, [&](const size_t start, const size_t end) {
    Line line;
    if (!parseLine(knownHosts.substr(start, end - start), line)) return;
    for (const char c : line.host) out += lower(c);
    out += ' ';
    out.append(line.type);
    out += ' ';
    out.append(line.key);
    out += '\n';
  });
  return out;
}

namespace {
constexpr size_t MAC_PREFIX_LEN = sizeof(MAC_PREFIX) - 1;

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool isMacLine(const std::string_view line) { return line.substr(0, MAC_PREFIX_LEN) == MAC_PREFIX; }

std::string_view trimmedLine(std::string_view line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
  return line;
}

// Differences are OR-ed over the whole length, so how long it takes says nothing about where.
bool sameBytes(const uint8_t* a, const uint8_t* b, const size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
  return diff == 0;
}
}  // namespace

FileState verify(const bool present, const bool readOk, const std::string_view text, const Mac& mac) {
  if (!present) return FileState::Missing;
  if (!readOk || text.size() > MAX_FILE_BYTES) return FileState::Unreadable;
  for (const char c : text) {
    const auto u = static_cast<unsigned char>(c);
    if (u == 0 || (u < 0x20 && c != '\n' && c != '\r' && c != '\t') || u == 0x7F) return FileState::Unreadable;
  }
  // Exactly one MAC line, and nothing but blank lines after it.
  size_t macLines = 0;
  bool macIsLast = false;
  uint8_t stored[32] = {};
  bool wellFormed = false;
  forEachLine(text, [&](const size_t start, const size_t end) {
    const std::string_view line = trimmedLine(text.substr(start, end - start));
    if (line.empty()) return;
    if (!isMacLine(line)) {
      macIsLast = false;
      return;
    }
    ++macLines;
    macIsLast = true;
    const std::string_view hex = line.substr(MAC_PREFIX_LEN);
    wellFormed = hex.size() == 64;
    for (size_t i = 0; wellFormed && i < 32; ++i) {
      const int hi = hexValue(hex[i * 2]);
      const int lo = hexValue(hex[i * 2 + 1]);
      if (hi < 0 || lo < 0) {
        wellFormed = false;
      } else {
        stored[i] = static_cast<uint8_t>((hi << 4) | lo);
      }
    }
  });
  if (macLines != 1 || !macIsLast || !wellFormed) return FileState::Changed;
  uint8_t wanted[32] = {};
  if (!mac.compute || !mac.compute(mac.ctx, canonical(text), wanted)) return FileState::Unreadable;
  return sameBytes(stored, wanted, sizeof(wanted)) ? FileState::Good : FileState::Changed;
}

bool seal(std::string& text, const Mac& mac) {
  std::string body;
  body.reserve(text.size() + 96);
  const std::string_view view(text);
  forEachLine(view, [&](const size_t start, const size_t end) {
    const std::string_view raw = view.substr(start, end - start);
    if (isMacLine(trimmedLine(raw))) return;
    body.append(raw);
    body += '\n';
  });
  uint8_t value[32] = {};
  if (!mac.compute || !mac.compute(mac.ctx, canonical(body), value)) return false;
  static constexpr char HEX[] = "0123456789abcdef";
  body += MAC_PREFIX;
  for (const uint8_t b : value) {
    body += HEX[b >> 4];
    body += HEX[b & 0x0F];
  }
  body += '\n';
  text.swap(body);
  return true;
}

void KeyGate::begin(std::string knownHosts, std::string host, const uint16_t port) {
  knownHosts_ = std::move(knownHosts);
  host_ = std::move(host);
  port_ = port;
  accepted_.clear();
  signedIn_ = false;
  last_ = Verdict::Unknown;
}

void KeyGate::reset() { begin(std::string(), std::string(), 22); }

bool KeyGate::accept(const uint8_t* keyBlob, const size_t keyBlobLen) {
  if (!keyBlob || keyBlobLen == 0 || keyBlobLen > MAX_KEY_BLOB_BYTES) {
    last_ = Verdict::Mismatch;
    return false;
  }
  if (signedIn_) {
    // A later key exchange: only the key this session started with.
    const bool same =
        !accepted_.empty() && accepted_.size() == keyBlobLen && sameBytes(accepted_.data(), keyBlob, keyBlobLen);
    last_ = same ? Verdict::Match : Verdict::Mismatch;
    return same;
  }
  last_ = check(knownHosts_, host_, port_, keyBlob, keyBlobLen);
  if (last_ != Verdict::Match) return false;
  accepted_.assign(keyBlob, keyBlob + keyBlobLen);
  return true;
}

bool KeyGate::signedIn() {
  signedIn_ = true;
  knownHosts_.clear();  // not needed again: the accepted key is the rule from here on
  return !accepted_.empty();
}

std::string fingerprint(const uint8_t sha256[32]) { return "SHA256:" + base64Encode(sha256, 32, false); }

}  // namespace sshkh
