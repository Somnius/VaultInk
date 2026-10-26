#include "VaultIndex.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <set>

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

}  // namespace

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

  std::set<std::string> seen;
  auto addTerms = [&](std::string_view s) {
    for (auto& w : textfold::words(s)) {
      if (note.terms.size() >= MAX_TERMS_PER_NOTE) return;
      if (seen.insert(w).second) note.terms.push_back(std::move(w));
    }
  };
  addTerms(note.title);
  for (const auto& alias : note.aliases) addTerms(alias);
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
  for (int f = 0; f < 7; ++f) {
    const size_t tab = f < 6 ? line.find(FIELD, start) : std::string_view::npos;
    if (f < 6 && tab == std::string_view::npos) return false;
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
  splitInto(fields[6], ' ', note.terms);
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
  // Pass 1: which notes the old index has, at which size.
  std::map<std::string, uint64_t> indexed;
  bool header = true;
  bool valid = false;
  fs.lines(indexPath, [&](std::string_view line) {
    if (header) {
      header = false;
      valid = line == INDEX_VERSION;
      return valid;
    }
    const size_t tab1 = line.find(FIELD);
    const size_t tab2 = tab1 == std::string_view::npos ? tab1 : line.find(FIELD, tab1 + 1);
    if (tab2 != std::string_view::npos) {
      indexed[std::string(line.substr(0, tab1))] =
          std::strtoull(std::string(line.substr(tab1 + 1, tab2 - tab1 - 1)).c_str(), nullptr, 10);
    }
    return true;
  });
  if (!valid) indexed.clear();

  const std::vector<std::string> notes = listNotes(root, fs);
  std::map<std::string, uint64_t> stamps;
  for (size_t i = 0; i < notes.size(); ++i) {
    stamps[notes[i]] = fs.stamp(notes[i]);
    if (progress && i % 32 == 31) progress(0, 0);  // lets a device yield on big vaults
  }

  if (!fs.begin(indexPath)) return false;
  bool ok = fs.append(std::string(INDEX_VERSION) + "\n");
  std::set<std::string> kept;
  // Pass 2: unchanged notes keep their old line.
  if (!indexed.empty()) {
    header = true;
    fs.lines(indexPath, [&](std::string_view line) {
      if (header) {
        header = false;
        return true;
      }
      const std::string path(line.substr(0, line.find(FIELD)));
      const auto old = indexed.find(path);
      const auto now = stamps.find(path);
      if (old != indexed.end() && now != stamps.end() && now->second != 0 && now->second == old->second &&
          kept.insert(path).second) {
        ok = ok && fs.append(std::string(line) + "\n");
      }
      return ok;
    });
  }
  // New and changed notes are read again.
  size_t done = kept.size();
  size_t rescanned = 0;
  for (const auto& path : notes) {
    if (!ok) break;
    if (kept.count(path)) continue;
    std::string text;
    if (!fs.read(path, MAX_NOTE_BYTES, text)) continue;
    Note note = scanNote(path, text);
    note.stamp = stamps[path];
    ok = fs.append(toLine(note) + "\n");
    ++rescanned;
    if (progress) progress(++done, notes.size());
  }
  if (!fs.commit(ok)) return false;
  if (stats) {
    stats->notes = notes.size();
    stats->rescanned = rescanned;
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
      for (const std::string& base : {root, dir}) {
        if (normalize(base + "/" + link, resolved) && key(resolved) == target) return true;
      }
      return false;
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
    int score = 0;
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
    hits.push_back({note.path, note.title, score});
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
