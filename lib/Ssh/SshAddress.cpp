#include "SshAddress.h"

#include <RemoteFiles.h>

namespace sshaddr {

namespace {
bool startsWithNoCase(const std::string_view text, const std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != prefix[i]) return false;
  }
  return true;
}

bool hostChar(const char c, const bool v6) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')
    return true;
  return v6 && c == ':';
}

bool parsePort(const std::string_view text, uint16_t& port) {
  if (text.empty() || text.size() > 5) return false;
  uint32_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint32_t>(c - '0');
  }
  if (value == 0 || value > 65535) return false;
  port = static_cast<uint16_t>(value);
  return true;
}
}  // namespace

bool parse(std::string_view text, Address& out) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  if (startsWithNoCase(text, "ssh://")) {
    text.remove_prefix(6);
  } else if (startsWithNoCase(text, "sftp://")) {
    text.remove_prefix(7);
  }
  Address result;
  const size_t slash = text.find('/');
  std::string_view authority = text.substr(0, slash);
  const std::string_view path = slash == std::string_view::npos ? std::string_view() : text.substr(slash);
  if (authority.empty() || authority.find('@') != std::string_view::npos) return false;

  std::string_view host;
  if (authority.front() == '[') {
    const size_t close = authority.find(']');
    if (close == std::string_view::npos) return false;
    host = authority.substr(1, close - 1);
    const std::string_view rest = authority.substr(close + 1);
    if (!rest.empty() && (rest.front() != ':' || !parsePort(rest.substr(1), result.port))) return false;
    if (host.empty()) return false;
    for (const char c : host) {
      if (!hostChar(c, true)) return false;
    }
  } else {
    const size_t colon = authority.find(':');
    host = authority.substr(0, colon);
    if (colon != std::string_view::npos && !parsePort(authority.substr(colon + 1), result.port)) return false;
    if (host.empty()) return false;
    for (const char c : host) {
      if (!hostChar(c, false)) return false;
    }
  }
  if (host.size() > 253) return false;
  result.host.assign(host);

  if (!path.empty() && path != "/") {
    for (const char c : path) {
      if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    }
    if (!remotefiles::normalizePath(path, result.path)) return false;
  } else if (path == "/") {
    result.path = "/";
  }
  out = std::move(result);
  return true;
}

std::string display(const Address& address) {
  const bool v6 = address.host.find(':') != std::string::npos;
  std::string out = v6 ? "[" + address.host + "]" : address.host;
  if (address.port != 22) out += ":" + std::to_string(address.port);
  return out;
}

bool remotePath(const std::string_view base, const std::string_view dir, std::string& out) {
  if (base.empty() || base.front() != '/') return false;
  std::string below;
  if (!remotefiles::normalizePath(dir.empty() ? std::string_view("/") : dir, below)) return false;
  std::string normalBase;
  if (!remotefiles::normalizePath(base, normalBase)) return false;
  if (below == "/") {
    out = normalBase;
  } else if (normalBase == "/") {
    out = below;
  } else {
    out = normalBase + below;
  }
  return true;
}

}  // namespace sshaddr
