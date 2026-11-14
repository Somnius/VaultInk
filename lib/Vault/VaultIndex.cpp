#include "VaultIndex.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>

#include "TextFold.h"

namespace vaultindex {
namespace {

constexpr char FIELD = '\t';
constexpr char ITEM = '\x1f';  // separates aliases and tags inside a field

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

std::string_view unquote(std::string_view s) {
  s = trim(s);
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) s = s.substr(1, s.size() - 2);
  return s;
}

// Field text without tabs, newlines or the item separator.
std::string clean(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c == '\t' || c == '\n' || c == '\r' || c == ITEM) c = ' ';
  }
  return out;
}

void splitInto(std::string_view s, const char sep, std::vector<std::string>& out) {
  size_t start = 0;
  while (start <= s.size()) {
    const size_t end = s.find(sep, start);
    const std::string_view part = s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (!part.empty()) out.emplace_back(part);
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
}

// "[a, b]", "a, b" or "a" from a frontmatter value.
void addListValues(std::string_view value, std::vector<std::string>& out) {
  value = trim(value);
  if (!value.empty() && value.front() == '[' && value.back() == ']') value = value.substr(1, value.size() - 2);
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = value.find(',', start);
    const std::string_view item =
        unquote(value.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
    if (!item.empty()) out.emplace_back(item);
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
}

std::string titleOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
  if (name.size() > 3 && name.compare(name.size() - 3, 3, ".md") == 0) name.resize(name.size() - 3);
  return name;
}

bool isMarkdown(const std::string& name) {
  return name.size() > 3 &&
         (name.compare(name.size() - 3, 3, ".md") == 0 || name.compare(name.size() - 3, 3, ".MD") == 0);
}

std::string join(const std::string& dir, const std::string& name) {
  return (dir == "/" ? std::string() : dir) + "/" + name;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// Every file under `root` (dot folders skipped), breadth-first, for vaultlink's `known` lookup.
bool writeFileList(const std::string& root, const IndexFs& fs, const std::function<void(size_t, size_t)>& progress,
                   size_t& listed) {
  listed = 0;
  if (!fs.begin(fileListPathFor(root))) return false;
  bool ok = true;
  std::deque<std::string> dirs{root};
  while (ok && !dirs.empty() && listed < MAX_LISTED_FILES) {
    const std::string dir = dirs.front();
    dirs.pop_front();
    fs.list(dir, [&](const std::string& name, const bool isDir) {
      if (!ok || name.empty() || name[0] == '.' || listed >= MAX_LISTED_FILES) return;
      if (isDir) {
        dirs.push_back(join(dir, name));
      } else {
        ok = fs.append(join(dir, name)) && fs.append("\n");
        ++listed;
        if (progress && listed % 64 == 0) progress(0, 0);
      }
    });
  }
  return fs.commit(ok) && ok;
}

// The vault's folders as build() sees them: the notes with their stamps, and a fingerprint of
// every file path plus the notes' stamps. The paths sit in one block of memory, not one
// allocation each, so a big vault doesn't use up the small-allocation heap.
struct Listing {
  struct Entry {
    uint32_t offset;
    uint32_t length;
    uint64_t stamp;
    bool kept;
  };
  std::string paths;
  std::vector<Entry> notes;                       // in walk order: shortest paths first
  std::vector<uint32_t> order;                    // indexes into `notes`, sorted by path
  size_t files = 0;                               // every file walked, notes and others
  uint64_t fingerprint = 1469598103934665603ull;  // FNV-1a
  bool stamped = true;                            // false when a note's stamp couldn't be read

  std::string_view path(const Entry& e) const { return std::string_view(paths).substr(e.offset, e.length); }

  void mix(std::string_view bytes) {
    for (const char c : bytes) fingerprint = (fingerprint ^ static_cast<uint8_t>(c)) * 1099511628211ull;
  }
  void mix(uint64_t value) {
    for (int i = 0; i < 8; ++i, value >>= 8) fingerprint = (fingerprint ^ (value & 0xff)) * 1099511628211ull;
  }

  Entry* find(std::string_view p) {
    const auto it = std::lower_bound(order.begin(), order.end(), p,
                                     [&](const uint32_t i, std::string_view key) { return path(notes[i]) < key; });
    return it != order.end() && path(notes[*it]) == p ? &notes[*it] : nullptr;
  }
};

Listing walk(const std::string& root, const IndexFs& fs, const std::function<void(size_t, size_t)>& progress) {
  Listing out;
  size_t& files = out.files;
  std::deque<std::string> dirs{root};
  while (!dirs.empty() && files < MAX_LISTED_FILES) {
    const std::string dir = dirs.front();
    dirs.pop_front();
    const auto entry = [&](const std::string& name, const bool isDir, const uint64_t* known) {
      if (name.empty() || name[0] == '.' || files >= MAX_LISTED_FILES) return;
      if (isDir) {
        dirs.push_back(join(dir, name));
        return;
      }
      ++files;
      if (progress && files % 64 == 0) progress(0, 0);
      const std::string path = join(dir, name);
      out.mix(path);
      out.mix(uint64_t{0xffff});  // ends the path, so "a" + "bc" differs from "ab" + "c"
      if (!isMarkdown(name)) return;
      const uint64_t stamp = known ? *known : fs.stamp(path);
      if (stamp == 0) out.stamped = false;
      out.mix(stamp);
      if (out.notes.size() >= MAX_INDEXED_NOTES) return;
      out.notes.push_back({static_cast<uint32_t>(out.paths.size()), static_cast<uint32_t>(path.size()), stamp, false});
      out.paths += path;
    };
    if (fs.listStamped) {
      fs.listStamped(
          dir, [&](const std::string& name, const bool isDir, const uint64_t stamp) { entry(name, isDir, &stamp); });
    } else {
      fs.list(dir, [&](const std::string& name, const bool isDir) { entry(name, isDir, nullptr); });
    }
  }
  out.order.resize(out.notes.size());
  for (uint32_t i = 0; i < out.order.size(); ++i) out.order[i] = i;
  std::sort(out.order.begin(), out.order.end(),
            [&](const uint32_t a, const uint32_t b) { return out.path(out.notes[a]) < out.path(out.notes[b]); });
  return out;
}

// The state line: what the vault looked like, and the stamps (size and time) of the index and the
// file list written for it, so a cut, emptied or replaced file doesn't pass as up to date.
std::string stateLine(const Listing& listing, const uint64_t indexStamp, const uint64_t listStamp) {
  return std::string(STATE_VERSION) + FIELD + std::to_string(listing.fingerprint) + FIELD +
         std::to_string(listing.notes.size()) + FIELD + std::to_string(listing.files) + FIELD +
         std::to_string(indexStamp) + FIELD + std::to_string(listStamp);
}
constexpr const char* STATE_INVALID = "invalid";

// The first line of a file; false when the file is missing or empty.
bool firstLine(const std::string& path, const IndexFs& fs, std::string& out) {
  bool got = false;
  fs.lines(path, [&](std::string_view line) {
    out.assign(line);
    got = true;
    return false;
  });
  return got;
}

// "path<TAB>stamp<TAB>..." of an index line.
bool pathAndStamp(std::string_view line, std::string_view& path, uint64_t& stamp) {
  const size_t tab1 = line.find(FIELD);
  const size_t tab2 = tab1 == std::string_view::npos ? tab1 : line.find(FIELD, tab1 + 1);
  if (tab2 == std::string_view::npos || tab2 == tab1 + 1) return false;
  path = line.substr(0, tab1);
  stamp = 0;
  for (const char c : line.substr(tab1 + 1, tab2 - tab1 - 1)) {
    if (c < '0' || c > '9') return false;
    stamp = stamp * 10 + static_cast<uint64_t>(c - '0');
  }
  return true;
}

// The note an index line is still good for: listed, same stamp, and not taken by an earlier line.
Listing::Entry* keeps(Listing& listing, std::string_view line) {
  std::string_view path;
  uint64_t stamp = 0;
  if (!pathAndStamp(line, path, stamp)) return nullptr;
  Listing::Entry* note = listing.find(path);
  return note && !note->kept && note->stamp != 0 && note->stamp == stamp ? note : nullptr;
}

}  // namespace

std::string indexPathFor(const std::string& root) {
  return "/.vaultink/" + std::to_string(std::hash<std::string>{}(root)) + "/index.tsv";
}

std::string fileListPathFor(const std::string& root) {
  std::string path = indexPathFor(root);
  return path.substr(0, path.find_last_of('/')) + "/files.txt";
}

std::string statePathFor(const std::string& root) {
  std::string path = indexPathFor(root);
  return path.substr(0, path.find_last_of('/')) + "/state.txt";
}

Note scanNote(const std::string& path, std::string_view text) {
  Note note;
  note.path = path;
  note.title = titleOf(path);

  std::string_view body = text;
  if (startsWith(text, "---\n") || startsWith(text, "---\r\n")) {
    const size_t end = text.find("\n---", 3);
    if (end != std::string_view::npos) {
      const std::string_view front = text.substr(text.find('\n') + 1, end - text.find('\n'));
      body = text.substr(std::min(text.size(), text.find('\n', end + 1) == std::string_view::npos
                                                   ? text.size()
                                                   : text.find('\n', end + 1) + 1));
      std::vector<std::string>* list = nullptr;
      size_t start = 0;
      while (start < front.size()) {
        const size_t nl = front.find('\n', start);
        const std::string_view line =
            front.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        start = nl == std::string_view::npos ? front.size() : nl + 1;
        const std::string_view t = trim(line);
        if (startsWith(t, "- ") && list != nullptr) {
          addListValues(t.substr(2), *list);
          continue;
        }
        list = nullptr;
        const size_t colon = t.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view key = trim(t.substr(0, colon));
        if (key == "aliases" || key == "alias") {
          list = &note.aliases;
        } else if (key == "tags" || key == "tag") {
          list = &note.tags;
        } else {
          continue;
        }
        addListValues(t.substr(colon + 1), *list);
      }
      for (auto& tag : note.tags) {
        if (!tag.empty() && tag.front() == '#') tag.erase(0, 1);
      }
    }
  }
  if (body.size() > MAX_NOTE_BYTES) body = body.substr(0, MAX_NOTE_BYTES);

  // Inline #tags: '#' at the start of a word, followed by a letter, not a heading marker.
  for (size_t i = 0; i < body.size(); ++i) {
    if (body[i] != '#' || (i > 0 && body[i - 1] != ' ' && body[i - 1] != '\n' && body[i - 1] != '\t')) continue;
    size_t end = i + 1;
    while (end < body.size() && body[end] != ' ' && body[end] != '\n' && body[end] != '\t' && body[end] != '#' &&
           body[end] != ',' && body[end] != '.' && body[end] != ')') {
      ++end;
    }
    if (end > i + 1 && !(body[i + 1] >= '0' && body[i + 1] <= '9')) {
      std::string tag(body.substr(i + 1, end - i - 1));
      if (std::find(note.tags.begin(), note.tags.end(), tag) == note.tags.end()) note.tags.push_back(tag);
    }
    i = end;
  }

  // Outgoing links: [[Target|alias]], ![[Target#heading]] and [text](Target.md).
  auto addLink = [&](std::string_view target) {
    target = trim(target.substr(0, target.find_first_of("|#")));
    if (target.size() > 3 && (target.substr(target.size() - 3) == ".md" || target.substr(target.size() - 3) == ".MD")) {
      target.remove_suffix(3);
    }
    std::string link;
    for (size_t k = 0; k < target.size(); ++k) {  // Markdown links write spaces as %20
      if (target.substr(k, 3) == "%20") {
        link += ' ';
        k += 2;
      } else {
        link += target[k];
      }
    }
    if (link.empty() || note.links.size() >= MAX_LINKS_PER_NOTE) return;
    if (std::find(note.links.begin(), note.links.end(), link) == note.links.end()) note.links.push_back(link);
  };
  for (size_t i = body.find("[["); i != std::string_view::npos; i = body.find("[[", i)) {
    const size_t end = body.find("]]", i + 2);
    const size_t nl = body.find('\n', i + 2);
    if (end == std::string_view::npos || (nl != std::string_view::npos && nl < end)) {
      i += 2;
      continue;
    }
    addLink(body.substr(i + 2, end - i - 2));
    i = end + 2;
  }
  for (size_t i = body.find("]("); i != std::string_view::npos; i = body.find("](", i + 2)) {
    const size_t end = body.find(')', i + 2);
    if (end == std::string_view::npos) break;
    const std::string_view target = body.substr(i + 2, end - i - 2);
    if (target.find("://") == std::string_view::npos && target.find('\n') == std::string_view::npos &&
        target.find(".md") != std::string_view::npos) {
      addLink(target);
    }
  }

  // Headings and block ids (kept as "^id"), for searches that open a note there. Code blocks are
  // skipped.
  bool inCode = false;
  size_t blocks = 0;
  for (size_t start = 0; start < body.size();) {
    const size_t nl = body.find('\n', start);
    std::string_view line = body.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
    start = nl == std::string_view::npos ? body.size() : nl + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (startsWith(line, "```") || startsWith(line, "~~~")) inCode = !inCode;
    if (inCode) continue;
    // A block id: " ^id" ending the line (or "^id" alone, under a table or quote), letters, digits
    // and dashes. Not on a heading: that one is found by its text.
    const std::string_view trimmed = trim(line);
    const bool isHeading = startsWith(line, "#") && line.find_first_not_of('#') != std::string_view::npos &&
                           line[line.find_first_not_of('#')] == ' ';
    const size_t caret = trimmed.rfind('^');
    if (caret != std::string_view::npos && caret + 1 < trimmed.size() && (caret == 0 || trimmed[caret - 1] == ' ') &&
        blocks < MAX_BLOCK_IDS_PER_NOTE && !isHeading) {
      const std::string_view id = trimmed.substr(caret + 1);
      if (std::all_of(id.begin(), id.end(), [](const char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
          })) {
        note.headings.push_back("^" + std::string(id));
        ++blocks;
        continue;
      }
    }
    if (note.headings.size() - blocks >= MAX_HEADINGS_PER_NOTE) continue;
    size_t level = 0;
    while (level < line.size() && line[level] == '#') ++level;
    if (level == 0 || level > 6 || level >= line.size() || line[level] != ' ') continue;
    std::string_view headingText = trim(line.substr(level + 1));
    while (!headingText.empty() && headingText.back() == '#') headingText.remove_suffix(1);  // "## Title ##"
    headingText = trim(headingText);
    if (!headingText.empty()) note.headings.emplace_back(headingText);
  }

  // Which terms are in already: their indexes sorted by word, in one small block (a std::set
  // of strings took a heap node per word).
  std::vector<uint16_t> seen;
  seen.reserve(MAX_TERMS_PER_NOTE);
  auto addTerms = [&](std::string_view s, const size_t minLength = 2) {
    for (auto& w : textfold::words(s, minLength)) {
      if (note.terms.size() >= MAX_TERMS_PER_NOTE) return;
      const auto at = std::lower_bound(seen.begin(), seen.end(), w,
                                       [&](const uint16_t i, const std::string& key) { return note.terms[i] < key; });
      if (at != seen.end() && note.terms[*at] == w) continue;
      seen.insert(at, static_cast<uint16_t>(note.terms.size()));
      note.terms.push_back(std::move(w));
    }
  };
  addTerms(note.title);
  for (const auto& alias : note.aliases) addTerms(alias);
  // Block ids whole, short parts too ("^table-2"), so searching for one finds it.
  for (const auto& h : note.headings) {
    if (!h.empty() && h.front() == '^') addTerms(h, 1);
  }
  addTerms(body);
  return note;
}

std::string toLine(const Note& note) {
  auto joinItems = [](const std::vector<std::string>& items, const char sep) {
    std::string out;
    for (const auto& item : items) {
      if (!out.empty()) out += sep;
      out += clean(item);
    }
    return out;
  };
  std::string line = clean(note.path) + FIELD + std::to_string(note.stamp) + FIELD + clean(note.title) + FIELD +
                     joinItems(note.aliases, ITEM) + FIELD + joinItems(note.tags, ITEM) + FIELD;
  // Links take at most half the line budget, terms fill the rest, so every line reads back whole.
  for (const auto& link : note.links) {
    const std::string item = clean(link);
    if (line.size() + item.size() + 1 > MAX_LINE_BYTES / 2) break;
    if (line.back() != FIELD) line += ITEM;
    line += item;
  }
  line += FIELD;
  for (const auto& heading : note.headings) {
    const std::string item = clean(heading);
    if (line.size() + item.size() + 1 > MAX_LINE_BYTES * 3 / 4) break;
    if (line.back() != FIELD) line += ITEM;
    line += item;
  }
  line += FIELD;
  // Terms fill what is left of the line budget, so every line reads back whole.
  for (const auto& term : note.terms) {
    if (line.size() + term.size() + 1 > MAX_LINE_BYTES) break;
    if (line.back() != FIELD) line += ' ';
    line += term;
  }
  return line;
}

bool fromLine(std::string_view line, Note& note) {
  std::vector<std::string> fields;
  size_t start = 0;
  for (int f = 0; f < 8; ++f) {
    const size_t tab = f < 7 ? line.find(FIELD, start) : std::string_view::npos;
    if (f < 7 && tab == std::string_view::npos) return false;
    fields.emplace_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
    start = tab + 1;
  }
  note = Note{};
  note.path = fields[0];
  note.stamp = std::strtoull(fields[1].c_str(), nullptr, 10);
  note.title = fields[2];
  splitInto(fields[3], ITEM, note.aliases);
  splitInto(fields[4], ITEM, note.tags);
  splitInto(fields[5], ITEM, note.links);
  splitInto(fields[6], ITEM, note.headings);
  splitInto(fields[7], ' ', note.terms);
  return !note.path.empty();
}

std::vector<std::string> listNotes(const std::string& root, const IndexFs& fs, const size_t maxNotes) {
  std::vector<std::string> notes;
  std::deque<std::string> dirs{root};
  while (!dirs.empty() && notes.size() < maxNotes) {
    const std::string dir = dirs.front();
    dirs.pop_front();
    fs.list(dir, [&](const std::string& name, const bool isDir) {
      if (name.empty() || name[0] == '.' || notes.size() >= maxNotes) return;
      if (isDir) {
        dirs.push_back(join(dir, name));
      } else if (isMarkdown(name)) {
        notes.push_back(join(dir, name));
      }
    });
  }
  return notes;
}

bool build(const std::string& root, const std::string& indexPath, const IndexFs& fs, BuildStats* stats,
           const std::function<void(size_t, size_t)>& progress) {
  if (stats) *stats = BuildStats{};
  Listing listing = walk(root, fs, progress);
  const size_t total = listing.notes.size();
  const std::string statePath = statePathFor(root);
  const std::string listPath = fileListPathFor(root);
  if (stats) stats->notes = total;

  // Nothing changed since the last complete build: the same listing, and the index and the file
  // list are the very files that build wrote (same size and time).
  std::string line;
  const bool indexValid = firstLine(indexPath, fs, line) && line == INDEX_VERSION;
  const bool hasState = firstLine(statePath, fs, line);
  // An index that isn't the file the state was written for (cut, or put there by something else)
  // is not trusted line by line: every note is read again. With no state, or an interrupted
  // build's "invalid", the index is whole (it is replaced in one step) and its lines are checked.
  bool foreign = false;
  if (indexValid && hasState && line != STATE_INVALID) {
    const uint64_t indexStamp = fs.stamp(indexPath);
    const uint64_t listStamp = fs.stamp(listPath);
    if (listing.stamped && indexStamp != 0 && listStamp != 0 && line == stateLine(listing, indexStamp, listStamp)) {
      if (stats) stats->unchanged = true;
      return true;
    }
    std::vector<std::string> fields;
    splitInto(line, FIELD, fields);
    foreign = fields.size() == 6 && fields[4] != std::to_string(indexStamp);
  }
  // From here the index and the file list may be written. The old state goes first, so a write
  // that fails or is cut by a power loss can't leave a fingerprint that vouches for other files.
  if (hasState && line != STATE_INVALID) {
    if (!fs.begin(statePath)) return false;
    if (!fs.commit(fs.append(STATE_INVALID) && fs.append("\n")) || !firstLine(statePath, fs, line) ||
        line != STATE_INVALID) {
      return false;
    }
  }

  // Which notes the old index has with the same stamp. A line for a note that is gone or changed,
  // or a second line for the same note, means the index must be written again.
  size_t kept = 0;
  bool stale = !indexValid || foreign;
  if (!stale) {
    bool header = true;
    size_t seen = 0;
    fs.lines(indexPath, [&](std::string_view l) {
      if (header) {
        header = false;
        return true;
      }
      if (progress && ++seen % 32 == 0) progress(0, 0);
      if (Listing::Entry* note = keeps(listing, l)) {
        note->kept = true;
        ++kept;
      } else {
        stale = true;
      }
      return true;
    });
  }

  bool ok = true;
  bool allRead = true;
  size_t rescanned = 0;
  if (stale || kept != total) {
    if (progress && total > 0) progress(kept, total);
    if (!fs.begin(indexPath)) return false;
    ok = fs.append(INDEX_VERSION) && fs.append("\n");
    // Unchanged notes keep their old line.
    if (kept > 0) {
      for (auto& note : listing.notes) note.kept = false;
      bool header = true;
      fs.lines(indexPath, [&](std::string_view l) {
        if (header) {
          header = false;
          return true;
        }
        if (Listing::Entry* note = keeps(listing, l)) {
          note->kept = true;
          ok = ok && fs.append(l) && fs.append("\n");
        }
        return ok;
      });
    }
    // New and changed notes are read again.
    size_t done = kept;
    std::string text;
    for (const auto& entry : listing.notes) {
      if (!ok) break;
      if (entry.kept) continue;
      const std::string path(listing.path(entry));
      if (!fs.read(path, MAX_NOTE_BYTES, text)) {
        allRead = false;  // tried again by the next build
        continue;
      }
      Note note = scanNote(path, text);
      note.stamp = entry.stamp;
      ok = fs.append(toLine(note)) && fs.append("\n");
      ++rescanned;
      if (progress) progress(++done, total);
    }
    if (!fs.commit(ok)) return false;
    if (stats) {
      stats->rewritten = ok;
      stats->rescanned = rescanned;
    }
  }
  // A list that can't be written only costs speed: links fall back to scanning the folders.
  size_t listedFiles = 0;
  const bool listed = writeFileList(root, fs, progress, listedFiles) && listedFiles == listing.files;
  // The state is written last and only when all of the above is complete: a build that was cut
  // short, or a note that couldn't be read, is tried again next time.
  if (ok && listed && allRead && listing.stamped) {
    const uint64_t indexStamp = fs.stamp(indexPath);
    const uint64_t listStamp = fs.stamp(listPath);
    if (indexStamp != 0 && listStamp != 0 && fs.begin(statePath)) {
      fs.commit(fs.append(stateLine(listing, indexStamp, listStamp)) && fs.append("\n"));
    }
  }
  return ok;
}

std::vector<Hit> backlinks(const std::string& indexPath, const std::string& root, const std::string& notePath,
                           const IndexFs& fs, const size_t maxResults) {
  // Compared folded (case and accents, Greek too), with ".md" dropped in any case.
  auto key = [](std::string_view path) {
    std::string k = textfold::fold(path);
    if (k.size() > 3 && k.compare(k.size() - 3, 3, ".md") == 0) k.resize(k.size() - 3);
    return k;
  };
  // "a/./b/../c" -> "a/c"; false when ".." climbs above the start.
  auto normalize = [](const std::string& path, std::string& out) {
    std::vector<std::string> parts;
    std::vector<std::string> pieces;
    splitInto(path, '/', pieces);
    for (const auto& p : pieces) {
      if (p.empty() || p == ".") continue;
      if (p == "..") {
        if (parts.empty()) return false;
        parts.pop_back();
      } else {
        parts.push_back(p);
      }
    }
    out.clear();
    for (const auto& p : parts) out += "/" + p;
    return true;
  };
  const std::string title = key(titleOf(notePath));
  const std::string target = key(notePath);
  std::vector<Hit> hits;
  bool header = true;
  fs.lines(indexPath, [&](std::string_view line) {
    if (header) {
      header = false;
      return line == INDEX_VERSION;
    }
    Note note;
    if (!fromLine(line, note) || note.path == notePath) return true;
    const std::string dir = note.path.substr(0, note.path.find_last_of('/'));
    // A bare name matches by title; a path is relative to the vault root or the linking note, as
    // vaultlink::resolve does.
    auto pointsHere = [&](const std::string& link) {
      if (link.find('/') == std::string::npos) return key(link) == title;
      std::string resolved;
      bool foundElsewhere = false;
      for (const std::string& base : {root, dir}) {
        if (!normalize(base + "/" + link, resolved)) continue;
        if (key(resolved) == target) return true;
        foundElsewhere = foundElsewhere || (fs.exists && fs.exists(vaultlink::fileNameFor(resolved)));
      }
      // Like vaultlink::resolve: when neither path exists, the link names the end of a path
      // ("People/Anna" is "Notes/People/Anna.md").
      if (foundElsewhere || link.find("..") != std::string::npos) return false;
      const std::string tail = "/" + key(link[0] == '/' ? link.substr(1) : link);
      return target.size() > tail.size() && target.compare(target.size() - tail.size(), tail.size(), tail) == 0;
    };
    if (std::any_of(note.links.begin(), note.links.end(), pointsHere)) hits.push_back({note.path, note.title, 0});
    return true;
  });
  std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.title < b.title; });
  if (hits.size() > maxResults) hits.resize(maxResults);
  return hits;
}

std::vector<Hit> search(const std::string& indexPath, std::string_view query, const IndexFs& fs,
                        const size_t maxResults) {
  const std::vector<std::string> words = textfold::words(query, 1);
  std::vector<Hit> hits;
  if (words.empty()) return hits;
  // 2 = the exact word, 1 = a word starting with it, 0 = no match.
  auto matchIn = [](const std::vector<std::string>& list, const std::string& word) {
    int best = 0;
    for (const auto& item : list) {
      if (item == word) return 2;
      if (startsWith(item, word)) best = 1;
    }
    return best;
  };
  auto better = [](const Hit& a, const Hit& b) { return a.score != b.score ? a.score > b.score : a.title < b.title; };
  bool header = true;
  fs.lines(indexPath, [&](std::string_view line) {
    if (header) {
      header = false;
      return line == INDEX_VERSION;
    }
    Note note;
    if (!fromLine(line, note)) return true;
    const auto titleWords = textfold::words(note.title, 1);
    std::vector<std::string> aliasWords, tagWords;
    for (const auto& a : note.aliases) {
      for (auto& w : textfold::words(a, 1)) aliasWords.push_back(std::move(w));
    }
    for (const auto& t : note.tags) {
      for (auto& w : textfold::words(t, 1)) tagWords.push_back(std::move(w));
    }
    // The first heading holding every word of the query (prefix match, like the rest), unless the
    // title already does: then the note opens at its start.
    std::string heading;
    const bool titleHasAll =
        std::all_of(words.begin(), words.end(), [&](const std::string& w) { return matchIn(titleWords, w) > 0; });
    for (const auto& h : note.headings) {
      if (titleHasAll) break;
      const auto headingWords = textfold::words(h, 1);
      if (std::all_of(words.begin(), words.end(), [&](const std::string& w) { return matchIn(headingWords, w) > 0; })) {
        heading = h;
        break;
      }
    }
    int score = heading.empty() ? 0 : 3;
    for (const auto& word : words) {
      // Title > alias > tag > body; an exact word scores a bit more than a prefix.
      int best = 0;
      if (const int m = matchIn(titleWords, word))
        best = 8 + 2 * m;
      else if (const int m = matchIn(aliasWords, word))
        best = 6 + 2 * m;
      else if (const int m = matchIn(tagWords, word))
        best = 4 + 2 * m;
      else if (const int m = matchIn(note.terms, word))
        best = m;
      if (best == 0) return true;  // every word must match
      score += best;
    }
    hits.push_back({note.path, note.title, score, heading});
    if (hits.size() > 4 * maxResults) {
      std::sort(hits.begin(), hits.end(), better);
      hits.resize(maxResults);
    }
    return true;
  });
  std::sort(hits.begin(), hits.end(), better);
  if (hits.size() > maxResults) hits.resize(maxResults);
  return hits;
}

}  // namespace vaultindex
