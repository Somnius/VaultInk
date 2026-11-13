#include "SshKnownHosts.h"

#include <cstring>

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
  for (const char c : host) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                    c == '-' || c == '_' || c == ':';
    if (!ok) return false;
  }
  return true;
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

std::string fingerprint(const uint8_t sha256[32]) { return "SHA256:" + base64Encode(sha256, 32, false); }

}  // namespace sshkh
