#include "VaultLink.h"

#include <algorithm>
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
  // An extension is 1-10 ASCII letters or digits after the last dot ("pic.png"); "Dr. Smith" and
  // "Wait…" or "sir)." are note names.
  const size_t dot = base.find_last_of('.');  // a leading dot (".profile") is part of the name
  const std::string_view ext = dot == std::string_view::npos ? std::string_view() : base.substr(dot + 1);
  const bool hasExt = dot != 0 && !ext.empty() && ext.size() <= 10 && std::all_of(ext.begin(), ext.end(), [](char c) {
                        return std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 0x80;
                      });
  return hasExt ? std::string(note) : std::string(note) + ".md";
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
  // The nearest .obsidian wins; without one, a folder right under /Vaults is the vault (an
  // imported ZIP, a copy without Obsidian's settings).
  std::string vaultsChild;
  for (std::string dir = noteDir;; dir = parentOf(dir)) {
    if (fs.exists(join(dir, ".obsidian"))) return dir;
    if (vaultsChild.empty() && parentOf(dir) == "/Vaults") vaultsChild = dir;
    if (dir == "/") break;
  }
  return vaultsChild.empty() ? noteDir : vaultsChild;
}

namespace {
std::string resolveFile(const std::string& root, const std::string& currentDir, const std::string& file, const Fs& fs,
                        size_t maxEntries);
}

std::string resolve(const std::string& root, const std::string& currentDir, const Target& target, const Fs& fs,
                    const size_t maxEntries) {
  const std::string file = fileNameFor(target.note);
  std::string path = resolveFile(root, currentDir, file, fs, maxEntries);
  // "Section 2.1" or "Plan v1.2" look like a file with an extension; when no such file exists,
  // they are notes.
  const bool kept = file.size() == target.note.size();
  if (path.empty() && kept && !(file.size() > 3 && sameName(std::string_view(file).substr(file.size() - 3), ".md"))) {
    path = resolveFile(root, currentDir, file + ".md", fs, maxEntries);
  }
  return path;
}

namespace {
std::string resolveFile(const std::string& root, const std::string& currentDir, const std::string& file, const Fs& fs,
                        const size_t maxEntries) {
  // A path ("People/Anna") is tried from the vault root and the note's folder, then, like Obsidian,
  // matched as the end of any path in the vault; a bare name is tried in the note's folder first.
  const bool hasPath = file.find('/') != std::string::npos;
  if (hasPath) {
    for (const std::string& base : {root, currentDir}) {
      const std::string path = join(base, file);
      if (fs.exists(path)) return path;
    }
  } else {
    const std::string local = join(currentDir, file);
    if (fs.exists(local)) return local;
  }
  const auto endsWithFile = [&file](std::string_view path) {
    return path.size() > file.size() && path[path.size() - file.size() - 1] == '/' &&
           sameName(path.substr(path.size() - file.size()), file);
  };

  if (fs.known) {
    // The list is in scan order, so the first name found at the fewest folders deep wins.
    std::string best;
    long bestDepth = 0;
    fs.known(root, [&](std::string_view path) {
      if (!endsWithFile(path)) return;
      const long depth = std::count(path.begin(), path.end(), '/');
      if (best.empty() || depth < bestDepth) {
        best = std::string(path);
        bestDepth = depth;
      }
    });
    if (!best.empty() && fs.exists(best)) return best;
  }

  // Breadth-first, so the first match is the one with the shortest path.
  const std::string name = hasPath ? file.substr(file.find_last_of('/') + 1) : file;
  std::deque<std::string> dirs{root};
  size_t seen = 0;
  while (!dirs.empty() && seen < maxEntries) {
    const std::string dir = dirs.front();
    dirs.pop_front();
    std::string found;
    fs.list(dir, [&](const std::string& entry, const bool isDir) {
      ++seen;
      if (!found.empty() || entry.empty() || entry[0] == '.') return;
      if (isDir) {
        dirs.push_back(join(dir, entry));
      } else if (sameName(entry, name) && endsWithFile(join(dir, entry))) {
        found = join(dir, entry);
      }
    });
    if (!found.empty()) return found;
  }
  return {};
}

}  // namespace

}  // namespace vaultlink
