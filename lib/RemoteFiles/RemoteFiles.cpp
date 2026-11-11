#include "RemoteFiles.h"

#include <algorithm>
#include <cstdio>

namespace remotefiles {

namespace {
char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool lessNoCase(const std::string& a, const std::string& b) {
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    const char ca = lowerAscii(a[i]);
    const char cb = lowerAscii(b[i]);
    if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb);
  }
  return a.size() < b.size();
}
}  // namespace

bool safeName(const std::string_view name) {
  if (name.empty() || name.size() > MAX_NAME_BYTES) return false;
  if (name == "." || name == "..") return false;
  if (name.back() == ' ' || name.back() == '.') return false;
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F) return false;
    switch (c) {
      case '/':
      case '\\':
      case '"':
      case '*':
      case ':':
      case '<':
      case '>':
      case '?':
      case '|':
        return false;
      default:
        break;
    }
  }
  return true;
}

bool Listing::add(Entry entry) {
  if (truncated) return false;
  if (!safeName(entry.name)) {
    ++skipped;
    return false;
  }
  if (entries.size() >= MAX_ENTRIES || nameBytes + entry.name.size() > MAX_NAME_BYTES_TOTAL) {
    truncated = true;
    return false;
  }
  nameBytes += entry.name.size();
  entries.push_back(std::move(entry));
  return true;
}

void sortEntries(std::vector<Entry>& entries) {
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    if (a.isDir != b.isDir) return a.isDir;
    return lessNoCase(a.name, b.name);
  });
}

std::string joinPath(const std::string_view dir, const std::string_view name) {
  std::string out(dir);
  if (out.empty() || out.back() != '/') out += '/';
  out.append(name);
  return out;
}

std::string parentPath(const std::string_view path) {
  std::string_view p = path;
  while (p.size() > 1 && p.back() == '/') p.remove_suffix(1);
  const size_t slash = p.rfind('/');
  if (slash == std::string_view::npos || slash == 0) return "/";
  return std::string(p.substr(0, slash));
}

bool normalizePath(const std::string_view path, std::string& out) {
  std::vector<std::string_view> parts;
  size_t pos = 0;
  while (pos <= path.size()) {
    size_t end = path.find('/', pos);
    if (end == std::string_view::npos) end = path.size();
    const std::string_view seg = path.substr(pos, end - pos);
    if (seg == "..") {
      if (parts.empty()) return false;
      parts.pop_back();
    } else if (!seg.empty() && seg != ".") {
      parts.push_back(seg);
    }
    pos = end + 1;
  }
  out.clear();
  for (const auto seg : parts) {
    out += '/';
    out.append(seg);
  }
  if (out.empty()) out = "/";
  return true;
}

int64_t epochUtc(const unsigned year, const unsigned month, const unsigned day, const unsigned hour,
                 const unsigned minute, const unsigned second) {
  if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
  if (hour > 23 || minute > 59 || second > 60) return 0;
  // Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
  int64_t y = year;
  y -= month <= 2;
  const int64_t era = y / 400;
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + static_cast<int64_t>(doe) - 719468;
  return days * 86400 + static_cast<int64_t>(hour) * 3600 + minute * 60 + second;
}

std::string sizeText(const uint64_t bytes) {
  char buf[24];
  if (bytes < 1024) {
    snprintf(buf, sizeof(buf), "%u B", static_cast<unsigned>(bytes));
  } else if (bytes < 1024ULL * 1024) {
    snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
  } else if (bytes < 1024ULL * 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  }
  return buf;
}

}  // namespace remotefiles
