#include "VaultHome.h"

#include <cctype>

#include "VaultLink.h"

namespace vaulthome {
namespace {

constexpr size_t MAX_SETTINGS = 16 * 1024;
constexpr int MAX_DEPTH = 16;

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// A small JSON reader: just enough to walk objects and pick string values by key.
struct Json {
  std::string_view s;
  size_t i = 0;
  int depth = 0;

  bool atEnd() const { return i >= s.size(); }
  char peek() const { return atEnd() ? '\0' : s[i]; }
  void space() {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  }

  bool string(std::string& out) {
    out.clear();
    if (peek() != '"') return false;
    ++i;
    while (!atEnd() && s[i] != '"') {
      char c = s[i++];
      if (c == '\\') {
        if (atEnd()) return false;
        const char e = s[i++];
        if (e == 'u') {
          // \uXXXX to UTF-8 (surrogate pairs are not joined).
          if (i + 4 > s.size()) return false;
          unsigned cp = 0;
          for (int k = 0; k < 4; ++k) {
            const int v = hexValue(s[i + k]);
            if (v < 0) return false;
            cp = cp * 16 + static_cast<unsigned>(v);
          }
          i += 4;
          if (cp < 0x80) {
            out += static_cast<char>(cp);
          } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
          } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
          }
          continue;
        }
        c = e == 'n' ? '\n' : e == 't' ? '\t' : e;  // \" \\ \/ stand for themselves
      }
      out += c;
    }
    if (atEnd()) return false;
    ++i;
    return true;
  }

  // Calls `member(key)` at each key of the object here; `member` reads or skips the value.
  template <typename F>
  bool object(F&& member) {
    space();
    if (peek() != '{' || ++depth > MAX_DEPTH) return false;
    ++i;
    space();
    if (peek() == '}') {
      ++i;
      --depth;
      return true;
    }
    std::string key;
    while (true) {
      space();
      if (!string(key)) return false;
      space();
      if (peek() != ':') return false;
      ++i;
      space();
      if (!member(key)) return false;
      space();
      if (peek() == ',') {
        ++i;
        continue;
      }
      if (peek() != '}') return false;
      ++i;
      --depth;
      return true;
    }
  }

  // Skips any value; false on broken JSON or nesting deeper than MAX_DEPTH.
  bool skip() {
    space();
    if (peek() == '"') {
      std::string ignored;
      return string(ignored);
    }
    if (peek() == '{') return object([this](const std::string&) { return skip(); });
    if (peek() == '[') {
      if (++depth > MAX_DEPTH) return false;
      ++i;
      space();
      if (peek() == ']') {
        ++i;
        --depth;
        return true;
      }
      while (true) {
        if (!skip()) return false;
        space();
        if (peek() == ',') {
          ++i;
          continue;
        }
        if (peek() != ']') return false;
        ++i;
        --depth;
        return true;
      }
    }
    const size_t start = i;
    while (!atEnd() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !std::isspace(static_cast<unsigned char>(s[i])))
      ++i;  // number, true, false, null
    return i > start;
  }

  // A string value into `out`; any other value is skipped and leaves `out` empty.
  bool stringValue(std::string& out) {
    space();
    if (peek() == '"') return string(out);
    out.clear();
    return skip();
  }
};

}  // namespace

bool safeNotePath(const std::string_view note) {
  if (note.empty() || note.front() == '/' || note.find('\\') != std::string_view::npos) return false;
  for (size_t start = 0; start <= note.size();) {
    size_t slash = note.find('/', start);
    if (slash == std::string_view::npos) slash = note.size();
    if (note.substr(start, slash - start) == "..") return false;
    start = slash + 1;
  }
  return true;
}

std::string homepageSetting(const std::string_view json) {
  Json j{json};
  std::string homepage;     // the first homepage that is a note (kind "File", or no kind)
  std::string defaultNote;  // the older format
  const bool ok = j.object([&](const std::string& key) {
    if (key == "defaultNote") return j.stringValue(defaultNote);
    j.space();
    if (key != "homepages" || j.peek() != '{') return j.skip();
    return j.object([&](const std::string&) {
      j.space();
      if (j.peek() != '{') return j.skip();
      std::string value;
      std::string kind;
      const bool read = j.object([&](const std::string& field) {
        if (field == "value") return j.stringValue(value);
        if (field == "kind") return j.stringValue(kind);
        return j.skip();
      });
      if (read && homepage.empty() && !value.empty() && (kind.empty() || kind == "File")) homepage = value;
      return read;
    });
  });
  if (!ok) return {};
  return homepage.empty() ? defaultNote : homepage;
}

std::string resolve(const std::string& root, const vaultindex::IndexFs& fs) {
  std::string json;
  if (fs.read && fs.read(root + "/.obsidian/plugins/homepage/data.json", MAX_SETTINGS, json)) {
    std::string note = homepageSetting(json);
    if (safeNotePath(note)) {
      // The path from the vault root, as the plugin stores it; ".md" added when missing. No vault-wide search:
      // Home is pressed often and a big vault takes long to walk.
      if (note.size() < 3 || !vaultlink::sameName(std::string_view(note).substr(note.size() - 3), ".md")) note += ".md";
      const std::string path = (!root.empty() && root.back() == '/' ? root : root + "/") + note;
      if (fs.exists && fs.exists(path)) return path;
    }
  }
  std::string home;
  std::string index;
  if (fs.list) {
    fs.list(root, [&](const std::string& name, const bool isDir) {
      if (isDir) return;
      if (home.empty() && vaultlink::sameName(name, "Home.md")) home = name;
      if (index.empty() && vaultlink::sameName(name, "index.md")) index = name;
    });
  }
  const std::string& found = home.empty() ? index : home;
  if (found.empty()) return {};
  return (!root.empty() && root.back() == '/' ? root : root + "/") + found;
}

}  // namespace vaulthome
