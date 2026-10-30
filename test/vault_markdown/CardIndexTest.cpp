#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "CardIndex.h"

using namespace cardindex;

namespace {

struct MemFs {
  std::map<std::string, std::string> files;
  std::map<std::string, int> edits;
  std::string pendingPath, pending;
  int reads = 0;

  vaultindex::IndexFs fs() {
    vaultindex::IndexFs out;
    out.stamp = [this](const std::string& p) -> uint64_t {
      return files.count(p) ? (static_cast<uint64_t>(edits[p]) << 32) | (files[p].size() + 1) : 0;
    };
    out.read = [this](const std::string& p, size_t cap, std::string& o) {
      if (!files.count(p)) return false;
      ++reads;
      o = files[p].substr(0, cap);
      return true;
    };
    out.lines = [this](const std::string& p, const std::function<bool(std::string_view)>& line) {
      if (!files.count(p)) return false;
      const std::string text = files[p];
      size_t start = 0;
      while (start < text.size()) {
        const size_t nl = text.find('\n', start);
        if (!line(std::string_view(text).substr(start, nl - start))) break;
        if (nl == std::string::npos) break;
        start = nl + 1;
      }
      return true;
    };
    out.begin = [this](const std::string& p) {
      pendingPath = p;
      pending.clear();
      return true;
    };
    out.append = [this](std::string_view t) {
      pending.append(t);
      return true;
    };
    out.commit = [this](bool keep) {
      if (keep) files[pendingPath] = pending;
      return true;
    };
    return out;
  }

  // Writes a note and its search index line (the index is up to date, as the caller ensures).
  void addNote(const std::string& root, const std::string& rel, const std::string& text, const std::string& tags) {
    const std::string path = root + "/" + rel;
    files[path] = text;
    ++edits[path];
    index = vaultindex::indexPathFor(root);
    vaultindex::Note n;
    n.path = path;
    n.stamp = fs().stamp(path);
    n.title = rel;
    size_t s = 0;
    while (s < tags.size()) {
      size_t c = tags.find(',', s);
      if (c == std::string::npos) c = tags.size();
      if (c > s) n.tags.push_back(tags.substr(s, c - s));
      s = c + 1;
    }
    indexLines[path] = vaultindex::toLine(n);
    std::string all = "vaultink-index 5\n";
    for (const auto& [p, l] : indexLines) all += l + "\n";
    files[index] = all;
  }
  std::string index;
  std::map<std::string, std::string> indexLines;
};

const std::string ROOT = "/Vaults/demo";
const std::string TAG = "flashcards";

cardsched::CardState known(uint32_t due) {
  cardsched::CardState s;
  s.last = due > 3 ? due - 3 : 1;
  s.due = due;
  s.interval = 3;
  return s;
}
}  // namespace

TEST(CardIndex, DecksFromTags) {
  EXPECT_TRUE(decksOf({"todo", "flashcardsx"}, TAG).empty());
  EXPECT_EQ(decksOf({"flashcards"}, TAG), std::vector<std::string>{""});
  const auto d = decksOf({"flashcards/greek/verbs", "x", "flashcards/greek/verbs", "flashcards"}, TAG);
  ASSERT_EQ(d.size(), 2u);
  EXPECT_EQ(d[0], "");
  EXPECT_EQ(d[1], "greek/verbs");
}

TEST(CardIndex, PathsSitBesideTheSearchIndex) {
  EXPECT_EQ(cardIndexPathFor(ROOT).substr(cardIndexPathFor(ROOT).size() - 9), "cards.idx");
  EXPECT_EQ(storePathFor(ROOT).substr(storePathFor(ROOT).size() - 9), "cards.tsv");
  EXPECT_EQ(cardIndexPathFor(ROOT).substr(0, cardIndexPathFor(ROOT).find_last_of('/')),
            vaultindex::indexPathFor(ROOT).substr(0, vaultindex::indexPathFor(ROOT).find_last_of('/')));
}

TEST(CardIndex, BuildReadsOnlyTaggedNotes) {
  MemFs m;
  m.addNote(ROOT, "A.md", "q1::a1\nq2:::a2\n", "flashcards");
  m.addNote(ROOT, "B.md", "key::value\n", "todo");
  m.addNote(ROOT, "Sub/C.md", "{{x}} marks\n", "flashcards/greek");
  const auto fs = m.fs();
  BuildStats st;
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs, &st));
  EXPECT_EQ(st.notes, 2u);
  EXPECT_EQ(st.rescanned, 2u);
  EXPECT_EQ(st.cards, 4u);  // 1 + 2 (reversible) + 1
  EXPECT_EQ(m.reads, 2);
}

TEST(CardIndex, SecondBuildKeepsUnchangedBlocks) {
  MemFs m;
  m.addNote(ROOT, "A.md", "q1::a1\n", "flashcards");
  m.addNote(ROOT, "C.md", "q3::a3\n", "flashcards");
  const auto fs = m.fs();
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs));
  m.reads = 0;
  BuildStats st;
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs, &st));
  EXPECT_EQ(st.rescanned, 0u);
  EXPECT_EQ(m.reads, 0);
  EXPECT_EQ(st.cards, 2u);
  m.addNote(ROOT, "A.md", "q1::a1\nq9::a9\n", "flashcards");  // changed
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs, &st));
  EXPECT_EQ(st.rescanned, 1u);
  EXPECT_EQ(m.reads, 1);
  EXPECT_EQ(st.cards, 3u);
}

TEST(CardIndex, AnUntaggedNoteLeavesTheIndex) {
  MemFs m;
  m.addNote(ROOT, "A.md", "q1::a1\n", "flashcards");
  const auto fs = m.fs();
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs));
  m.addNote(ROOT, "A.md", "q1::a1\n", "other");  // tag removed
  BuildStats st;
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs, &st));
  EXPECT_EQ(st.cards, 0u);
  EXPECT_EQ(m.files[cardIndexPathFor(ROOT)], std::string(VERSION) + "\n");
}

TEST(CardIndex, TextWithTabsAndNewlinesSurvives) {
  MemFs m;
  m.addNote(ROOT, "A.md", "first\nline?\n?\nan\\swer\tx\n", "flashcards");
  const auto fs = m.fs();
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs));
  cardstore::Store store({}, "x");
  const auto session = collectSession(cardIndexPathFor(ROOT), fs, store, 20000, {});
  ASSERT_EQ(session.size(), 1u);
  EXPECT_EQ(session[0].front, "first\nline?");
  EXPECT_EQ(session[0].back, "an\\swer\tx");
  EXPECT_EQ(session[0].notePath, ROOT + "/A.md");
  EXPECT_EQ(session[0].line, 1u);
}

TEST(CardIndex, DeckListJoinsTheStore) {
  MemFs m;
  m.addNote(ROOT, "A.md", "q1::a1\nq2::a2\nq3::a3\n", "flashcards/greek");
  m.addNote(ROOT, "B.md", "q4::a4\n", "flashcards");
  const auto fs = m.fs();
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs));
  const uint32_t today = 20000;
  const auto all = collectSession(cardIndexPathFor(ROOT), fs, cardstore::Store({}, "x"), today, {});
  ASSERT_EQ(all.size(), 4u);
  // q1 known and due, q2 known but not due, the rest new.
  cardstore::StoreFs sfs;
  sfs.appendTo = [&](const std::string& p, std::string_view t) {
    m.files[p].append(t);
    return true;
  };
  sfs.lines = fs.lines;
  sfs.begin = fs.begin;
  sfs.append = fs.append;
  sfs.commit = fs.commit;
  cardstore::Store store(sfs, storePathFor(ROOT));
  store.load();
  store.save(all[0].id, known(today));
  store.save(all[1].id, known(today + 5));
  const auto decks = listDecks(cardIndexPathFor(ROOT), fs, store, today);
  ASSERT_EQ(decks.size(), 2u);
  EXPECT_EQ(decks[0].name, "");
  EXPECT_EQ(decks[0].cards, 1u);
  EXPECT_EQ(decks[0].fresh, 1u);
  EXPECT_EQ(decks[1].name, "greek");
  EXPECT_EQ(decks[1].cards, 3u);
  EXPECT_EQ(decks[1].due, 1u);
  EXPECT_EQ(decks[1].fresh, 1u);
}

TEST(CardIndex, SessionOrdersLimitsAndFilters) {
  MemFs m;
  m.addNote(ROOT, "A.md", "q1::a1\nq2::a2\nq3::a3\nq4::a4\n", "flashcards/greek/verbs");
  m.addNote(ROOT, "B.md", "q5::a5\n", "flashcards/latin");
  const auto fs = m.fs();
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs));
  const uint32_t today = 20000;
  const auto first = collectSession(cardIndexPathFor(ROOT), fs, cardstore::Store({}, "x"), today, {});
  ASSERT_EQ(first.size(), 5u);

  cardstore::StoreFs sfs;
  sfs.appendTo = [&](const std::string& p, std::string_view t) {
    m.files[p].append(t);
    return true;
  };
  sfs.lines = fs.lines;
  sfs.begin = fs.begin;
  sfs.append = fs.append;
  sfs.commit = fs.commit;
  cardstore::Store store(sfs, storePathFor(ROOT));
  store.load();
  store.save(first[0].id, known(today - 1));  // overdue by a day
  store.save(first[1].id, known(today - 9));  // overdue by nine
  store.save(first[2].id, known(today + 2));  // not due

  SessionSpec spec;
  auto s = collectSession(cardIndexPathFor(ROOT), fs, store, today, spec);
  ASSERT_EQ(s.size(), 4u);  // q2 (most overdue), q1, then new q4 and q5
  EXPECT_EQ(s[0].front, "q2");
  EXPECT_EQ(s[1].front, "q1");
  EXPECT_EQ(s[2].front, "q4");
  EXPECT_EQ(s[3].front, "q5");

  spec.newLimit = 1;
  spec.reviewLimit = 1;
  s = collectSession(cardIndexPathFor(ROOT), fs, store, today, spec);
  ASSERT_EQ(s.size(), 2u);
  EXPECT_EQ(s[0].front, "q2");
  EXPECT_EQ(s[1].front, "q4");

  SessionSpec deck;
  deck.all = false;
  deck.deck = "greek";  // includes greek/verbs
  EXPECT_EQ(collectSession(cardIndexPathFor(ROOT), fs, store, today, deck).size(), 3u);
  deck.deck = "greek/verbs";
  EXPECT_EQ(collectSession(cardIndexPathFor(ROOT), fs, store, today, deck).size(), 3u);
  deck.deck = "gre";  // a prefix is not a parent deck
  EXPECT_TRUE(collectSession(cardIndexPathFor(ROOT), fs, store, today, deck).empty());

  SessionSpec note;
  note.onlyNote = ROOT + "/B.md";
  s = collectSession(cardIndexPathFor(ROOT), fs, store, today, note);
  ASSERT_EQ(s.size(), 1u);
  EXPECT_EQ(s[0].front, "q5");
}

TEST(CardIndex, AllIdsFeedsCompaction) {
  MemFs m;
  m.addNote(ROOT, "A.md", "q1::a1\nq2:::a2\n", "flashcards");
  const auto fs = m.fs();
  ASSERT_TRUE(build(ROOT, vaultindex::indexPathFor(ROOT), cardIndexPathFor(ROOT), TAG, fs));
  std::set<uint64_t> ids;
  ASSERT_TRUE(allIds(cardIndexPathFor(ROOT), fs, [&](uint64_t id) { ids.insert(id); }));
  EXPECT_EQ(ids.size(), 3u);
}
