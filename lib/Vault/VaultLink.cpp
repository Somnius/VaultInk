#include "VaultLink.h"

#include <cctype>
#include <deque>

namespace vaultlink {
namespace {

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string urlDecode(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hexValue(s[i + 1]) >= 0 && hexValue(s[i + 2]) >= 0) {
      out += static_cast<char>(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string join(const std::string& dir, std::string_view name) {
  if (dir.empty() || dir == "/") return "/" + std::string(name);
  return dir + "/" + std::string(name);
}

std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return path.substr(0, slash);
}

}  // namespace

bool parse(std::string_view href, Target& out) {
  constexpr std::string_view scheme = "vault:";
  if (href.substr(0, scheme.size()) != scheme) return false;
  href.remove_prefix(scheme.size());
  const size_t hash = href.find('#');
  out.note = urlDecode(href.substr(0, hash));
  out.anchor = hash == std::string_view::npos ? std::string() : urlDecode(href.substr(hash + 1));
  while (!out.note.empty() && out.note.front() == '/') out.note.erase(0, 1);
  return !out.note.empty();
}

std::string fileNameFor(std::string_view note) {
  const size_t slash = note.find_last_of('/');
  const std::string_view base = slash == std::string_view::npos ? note : note.substr(slash + 1);
  if (base.find('.') != std::string_view::npos && base.back() != '.') return std::string(note);
  return std::string(note) + ".md";
}

bool sameName(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

std::string vaultRoot(const std::string& notePath, const Fs& fs) {
  const std::string noteDir = parentOf(notePath);
  for (std::string dir = noteDir;; dir = parentOf(dir)) {
    if (fs.exists(join(dir, ".obsidian"))) return dir;
    if (dir == "/") break;
  }
  return noteDir;
}

std::string resolve(const std::string& root, const std::string& currentDir, const Target& target, const Fs& fs,
                    const size_t maxEntries) {
  const std::string file = fileNameFor(target.note);
  if (file.find('/') != std::string::npos) {
    for (const std::string& base : {root, currentDir}) {
      const std::string path = join(base, file);
      if (fs.exists(path)) return path;
    }
    return {};
  }
  const std::string local = join(currentDir, file);
  if (fs.exists(local)) return local;

  // Breadth-first, so the first match is the one with the shortest path.
  std::deque<std::string> dirs{root};
  size_t seen = 0;
  while (!dirs.empty() && seen < maxEntries) {
    const std::string dir = dirs.front();
    dirs.pop_front();
    std::string found;
    fs.list(dir, [&](const std::string& name, const bool isDir) {
      ++seen;
      if (!found.empty() || name.empty() || name[0] == '.') return;
      if (isDir) {
        dirs.push_back(join(dir, name));
      } else if (sameName(name, file)) {
        found = join(dir, name);
      }
    });
    if (!found.empty()) return found;
  }
  return {};
}

}  // namespace vaultlink
