#include "CardIndex.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <map>
#include <unordered_set>

namespace cardindex {
namespace {

constexpr char FIELD = '\t';

std::string escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        break;
      default:
        out += c;
    }
  }
  return out;
}

std::string unescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      ++i;
      out += s[i] == 'n' ? '\n' : s[i] == 't' ? '\t' : s[i];
    } else {
      out += s[i];
    }
  }
  return out;
}

std::vector<std::string_view> split(std::string_view line, char sep, size_t maxFields) {
  std::vector<std::string_view> out;
  while (out.size() + 1 < maxFields) {
    const size_t at = line.find(sep);
    if (at == std::string_view::npos) break;
    out.push_back(line.substr(0, at));
    line.remove_prefix(at + 1);
  }
  out.push_back(line);
  return out;
}

bool parseHex(std::string_view s, uint64_t& out) {
  if (s.size() != 16) return false;
  uint64_t v = 0;
  for (const char c : s) {
    v <<= 4;
    if (c >= '0' && c <= '9') {
      v |= static_cast<uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      v |= static_cast<uint64_t>(c - 'a' + 10);
    } else {
      return false;
    }
  }
  out = v;
  return true;
}

std::string relativePath(const std::string& root, const std::string& path) {
  const std::string prefix = root.empty() || root.back() == '/' ? root : root + "/";
  return path.rfind(prefix, 0) == 0 ? path.substr(prefix.size()) : path;
}

std::string joinDecks(const std::vector<std::string>& decks) {
  std::string out;
  for (size_t i = 0; i < decks.size(); ++i) {
    if (i) out += ',';
    out += decks[i];
  }
  return out;
}

bool deckMatches(std::string_view noteDecks, const SessionSpec& spec) {
  if (spec.all) return true;
  size_t start = 0;
  while (start <= noteDecks.size()) {
    size_t comma = noteDecks.find(',', start);
    if (comma == std::string_view::npos) comma = noteDecks.size();
    const std::string_view deck = noteDecks.substr(start, comma - start);
    if (deck == spec.deck ||
        (deck.size() > spec.deck.size() && deck.substr(0, spec.deck.size()) == spec.deck &&
         deck[spec.deck.size()] == '/') ||
        (spec.deck.empty() && deck.empty())) {
      return true;
    }
    start = comma + 1;
  }
  return false;
}

}  // namespace

std::string cardIndexPathFor(const std::string& root) {
  const std::string path = vaultindex::indexPathFor(root);
  return path.substr(0, path.find_last_of('/')) + "/cards.idx";
}

std::string storePathFor(const std::string& root) {
  const std::string path = vaultindex::indexPathFor(root);
  return path.substr(0, path.find_last_of('/')) + "/cards.tsv";
}

std::vector<std::string> decksOf(const std::vector<std::string>& tags, const std::string_view tag) {
  std::vector<std::string> decks;
  for (const std::string& t : tags) {
    if (t == tag) {
      decks.emplace_back();
    } else if (t.size() > tag.size() + 1 && t.compare(0, tag.size(), tag) == 0 && t[tag.size()] == '/') {
      decks.push_back(t.substr(tag.size() + 1));
    }
  }
  std::sort(decks.begin(), decks.end());
  decks.erase(std::unique(decks.begin(), decks.end()), decks.end());
  return decks;
}

bool build(const std::string& root, const std::string& indexPath, const std::string& cardsPath,
           const std::string_view tag, const vaultindex::IndexFs& fs, BuildStats* stats) {
  struct Wanted {
    uint64_t stamp;
    std::string decks;
  };
  std::map<std::string, Wanted> wanted;  // notes with the flashcards tag, by path
  const bool haveIndex = fs.lines(indexPath, [&](std::string_view line) {
    if (line.rfind("vaultink-index", 0) == 0) return true;  // the header
    vaultindex::Note note;
    if (!vaultindex::fromLine(line, note)) return true;
    const auto decks = decksOf(note.tags, tag);
    if (!decks.empty()) wanted[note.path] = {note.stamp, joinDecks(decks)};
    return true;
  });
  if (!haveIndex) return false;

  if (!fs.begin(cardsPath)) return false;
  bool ok = fs.append(std::string(VERSION) + "\n");
  BuildStats local;
  std::map<std::string, bool> kept;  // notes whose block was copied from the old file

  // Copy the blocks of notes that are unchanged.
  if (ok) {
    bool copying = false;
    fs.lines(cardsPath, [&](std::string_view line) {
      if (line.rfind("N\t", 0) == 0) {
        copying = false;
        const auto f = split(line, FIELD, 4);
        if (f.size() == 4) {
          const std::string path(f[1]);
          const auto it = wanted.find(path);
          if (it != wanted.end() && std::to_string(it->second.stamp) == f[2] && it->second.decks == f[3] &&
              !kept.count(path)) {
            copying = true;
            kept[path] = true;
          }
        }
      }
      if (copying) ok = ok && fs.append(std::string(line) + "\n");
      if (copying && line.rfind("C\t", 0) == 0) ++local.cards;
      return ok;
    });
  }
  // Read and parse the new and changed ones.
  for (const auto& [path, info] : wanted) {
    if (!ok) break;
    ++local.notes;
    if (kept.count(path)) continue;
    ++local.rescanned;
    std::string text;
    if (!fs.read(path, vaultindex::MAX_NOTE_BYTES, text)) continue;
    const auto cards = flashcards::parseCards(text);
    const std::string rel = relativePath(root, path);
    ok = fs.append("N\t" + path + "\t" + std::to_string(info.stamp) + "\t" + info.decks + "\n");
    for (const auto& card : cards) {
      if (!ok) break;
      ok = fs.append("C\t" + flashcards::idToHex(flashcards::cardId(rel, card)) + "\t" + std::to_string(card.line) +
                     "\t" + escape(card.front) + "\t" + escape(card.back) + "\n");
      ++local.cards;
    }
  }
  if (!fs.commit(ok)) return false;
  if (stats) *stats = local;
  return ok;
}

namespace {

// Streams the cards of cards.idx with the note's path and decks.
bool eachCard(const std::string& cardsPath, const vaultindex::IndexFs& fs,
              const std::function<void(const std::string& path, std::string_view decks, uint64_t id, uint32_t line,
                                       std::string_view front, std::string_view back)>& card) {
  std::string path;
  std::string decks;
  return fs.lines(cardsPath, [&](std::string_view line) {
    if (line.rfind("N\t", 0) == 0) {
      const auto f = split(line, FIELD, 4);
      path = f.size() > 1 ? std::string(f[1]) : std::string();
      decks = f.size() > 3 ? std::string(f[3]) : std::string();
    } else if (line.rfind("C\t", 0) == 0) {
      const auto f = split(line, FIELD, 5);
      uint64_t id;
      if (f.size() == 5 && parseHex(f[1], id)) {
        card(path, decks, id, static_cast<uint32_t>(std::strtoul(std::string(f[2]).c_str(), nullptr, 10)), f[3], f[4]);
      }
    }
    return true;
  });
}

}  // namespace

std::vector<DeckInfo> listDecks(const std::string& cardsPath, const vaultindex::IndexFs& fs,
                                const cardstore::Store& store, const uint32_t today) {
  std::map<std::string, DeckInfo> byName;
  eachCard(cardsPath, fs,
           [&](const std::string&, std::string_view decks, uint64_t id, uint32_t, std::string_view, std::string_view) {
             const auto* state = store.find(id);
             size_t start = 0;
             while (start <= decks.size()) {
               size_t comma = decks.find(',', start);
               if (comma == std::string_view::npos) comma = decks.size();
               const std::string name(decks.substr(start, comma - start));
               DeckInfo& d = byName[name];
               d.name = name;
               ++d.cards;
               if (!state || state->isNew()) {
                 ++d.fresh;
               } else if (state->due <= today) {
                 ++d.due;
               }
               start = comma + 1;
             }
           });
  std::vector<DeckInfo> out;
  for (auto& [name, d] : byName) out.push_back(std::move(d));
  return out;
}

std::vector<SessionCard> collectSession(const std::string& cardsPath, const vaultindex::IndexFs& fs,
                                        const cardstore::Store& store, const uint32_t today, const SessionSpec& spec) {
  std::vector<SessionCard> reviews;
  std::vector<SessionCard> fresh;
  std::unordered_set<uint64_t> seen;  // the same card under two decks or tags is studied once
  eachCard(cardsPath, fs,
           [&](const std::string& path, std::string_view decks, uint64_t id, uint32_t line, std::string_view front,
               std::string_view back) {
             if (!spec.onlyNote.empty() && path != spec.onlyNote) return;
             if (!deckMatches(decks, spec)) return;
             const auto* state = store.find(id);
             const bool isNew = !state || state->isNew();
             if (!isNew && state->due > today) return;
             if (isNew && fresh.size() >= spec.newLimit) return;
             if (!seen.insert(id).second) return;
             SessionCard c;
             c.id = id;
             c.front = unescape(front);
             c.back = unescape(back);
             c.line = line;
             c.notePath = path;
             if (state) c.state = *state;
             if (isNew) {
               fresh.push_back(std::move(c));
             } else {
               reviews.push_back(std::move(c));
             }
           });
  std::stable_sort(reviews.begin(), reviews.end(),
                   [](const SessionCard& a, const SessionCard& b) { return a.state.due < b.state.due; });
  if (reviews.size() > spec.reviewLimit) reviews.resize(spec.reviewLimit);
  reviews.insert(reviews.end(), std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
  return reviews;
}

bool allIds(const std::string& cardsPath, const vaultindex::IndexFs& fs, const std::function<void(uint64_t)>& id) {
  return eachCard(cardsPath, fs,
                  [&](const std::string&, std::string_view, uint64_t cardId, uint32_t, std::string_view,
                      std::string_view) { id(cardId); });
}

}  // namespace cardindex
