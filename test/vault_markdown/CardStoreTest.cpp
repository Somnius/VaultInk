#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>

#include "CardStore.h"

using namespace cardstore;
using cardsched::CardState;

namespace {

struct MemFiles {
  std::map<std::string, std::string> files;
  std::string pendingPath, pending;

  StoreFs fs() {
    StoreFs out;
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
    out.appendTo = [this](const std::string& p, std::string_view t) {
      files[p].append(t);
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
};

const std::string PATH = "/.vaultink/demo/cards.tsv";

CardState state(uint32_t due, uint32_t last, uint16_t interval = 5) {
  CardState s;
  s.due = due;
  s.last = last;
  s.interval = interval;
  s.ease = 240;
  s.reps = 2;
  s.lapses = 1;
  return s;
}
}  // namespace

TEST(CardStore, LineRoundTrip) {
  const CardState s = state(20010, 20000, 10);
  const std::string line = toLine(0xdeadbeef12345678ULL, s);
  EXPECT_EQ(line, "deadbeef12345678\t20010\t10\t240\t2\t1\t20000");
  Entry e;
  ASSERT_TRUE(fromLine(line, e));
  EXPECT_EQ(e.id, 0xdeadbeef12345678ULL);
  EXPECT_EQ(e.state.due, 20010u);
  EXPECT_EQ(e.state.last, 20000u);
  EXPECT_EQ(e.state.interval, 10);
  EXPECT_EQ(e.state.lapses, 1);
}

TEST(CardStore, BadLinesAreRejected) {
  Entry e;
  EXPECT_FALSE(fromLine("", e));
  EXPECT_FALSE(fromLine("zz\t1\t1\t1\t1\t1\t1", e));
  EXPECT_FALSE(fromLine("deadbeef12345678\t1\t1\t1\t1\t1", e));         // a field short
  EXPECT_FALSE(fromLine("deadbeef12345678\t1\t1\t1\t1\t1\t1\t1", e));   // a field over
  EXPECT_FALSE(fromLine("deadbeef12345678\t1\t99999\t1\t1\t1\t1", e));  // interval over 16 bits
  EXPECT_FALSE(fromLine("deadbeef12345678\t1\t1\t1\t300\t1\t1", e));    // reps over 8 bits
}

TEST(CardStore, MissingFileIsEmptyAndSaveCreatesIt) {
  MemFiles m;
  Store store(m.fs(), PATH);
  ASSERT_TRUE(store.load());
  EXPECT_EQ(store.size(), 0u);
  ASSERT_TRUE(store.save(7, state(20001, 20000)));
  EXPECT_EQ(m.files[PATH].rfind("vaultink-cards 1\n", 0), 0u);
  ASSERT_NE(store.find(7), nullptr);
  EXPECT_EQ(store.find(7)->due, 20001u);
  EXPECT_EQ(store.find(8), nullptr);
}

TEST(CardStore, NewestLineWinsAndSurvivesReload) {
  MemFiles m;
  {
    Store store(m.fs(), PATH);
    store.load();
    store.save(1, state(20001, 20000));
    store.save(2, state(20005, 20000));
    store.save(1, state(20009, 20001));
  }
  Store again(m.fs(), PATH);
  ASSERT_TRUE(again.load());
  EXPECT_EQ(again.size(), 2u);
  EXPECT_EQ(again.find(1)->due, 20009u);
  EXPECT_EQ(again.find(2)->due, 20005u);
}

TEST(CardStore, FilteredLoadKeepsOnlyWantedCards) {
  MemFiles m;
  {
    Store store(m.fs(), PATH);
    store.load();
    for (uint64_t id = 1; id <= 10; ++id) store.save(id, state(20001, 20000));
  }
  Store again(m.fs(), PATH);
  again.load([](uint64_t id) { return id % 2 == 0; });
  EXPECT_EQ(again.size(), 5u);
  EXPECT_EQ(again.find(3), nullptr);
  EXPECT_NE(again.find(4), nullptr);
}

TEST(CardStore, OtherVersionStartsEmptyAndIsRewrittenOnSave) {
  MemFiles m;
  m.files[PATH] = "vaultink-cards 9\n0000000000000001\t1\t1\t250\t1\t0\t1\n";
  Store store(m.fs(), PATH);
  store.load();
  EXPECT_EQ(store.size(), 0u);
  ASSERT_TRUE(store.save(5, state(20001, 20000)));
  EXPECT_EQ(m.files[PATH], std::string("vaultink-cards 1\n") + toLine(5, state(20001, 20000)) + "\n");
}

TEST(CardStore, TornAndJunkLinesAreSkipped) {
  MemFiles m;
  m.files[PATH] = std::string("vaultink-cards 1\n") + toLine(1, state(20001, 20000)) + "\nnot a line\n" +
                  toLine(2, state(20002, 20000)).substr(0, 20);  // power cut mid-line
  Store store(m.fs(), PATH);
  store.load();
  EXPECT_EQ(store.size(), 1u);
  EXPECT_NE(store.find(1), nullptr);
}

TEST(CardStore, CompactionKeepsNewestAndDropsOldGoneCards) {
  MemFiles m;
  const uint32_t today = 20200;
  {
    Store store(m.fs(), PATH);
    store.load();
    store.save(1, state(20001, 20000));  // seen 200 days ago, still in a note
    store.save(1, state(20010, 20001));
    store.save(2, state(20001, 20000));      // seen 200 days ago, note is gone
    store.save(3, state(today + 1, today));  // recent
    store.save(3, state(today + 4, today));
  }
  Store store(m.fs(), PATH);
  store.load();
  ASSERT_TRUE(store.compact(today, [](uint64_t id) { return id == 1; }));
  EXPECT_EQ(store.size(), 2u);
  EXPECT_NE(store.find(1), nullptr);
  EXPECT_EQ(store.find(2), nullptr);
  EXPECT_EQ(store.find(3)->due, today + 4);
  Store again(m.fs(), PATH);
  again.load();
  EXPECT_EQ(again.size(), 2u);
  EXPECT_EQ(again.find(1)->due, 20010u);
  EXPECT_EQ(std::count(m.files[PATH].begin(), m.files[PATH].end(), '\n'), 3);  // header + 2
}

TEST(CardStore, BigJournalAsksForCompaction) {
  MemFiles m;
  std::string big = std::string(HEADER) + "\n";
  while (big.size() <= COMPACT_BYTES) big += toLine(1, state(20001, 20000)) + "\n";
  m.files[PATH] = big;
  Store store(m.fs(), PATH);
  store.load();
  EXPECT_TRUE(store.needsCompaction());
  EXPECT_EQ(store.size(), 1u);
  store.compact(20001, nullptr);
  EXPECT_FALSE(store.needsCompaction());
  EXPECT_LT(m.files[PATH].size(), 200u);
}
