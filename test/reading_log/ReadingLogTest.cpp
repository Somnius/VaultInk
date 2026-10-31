#include <ReadingLog.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using readinglog::Session;

namespace {

// 2026-10-30 12:00 UTC is a Friday.
constexpr int64_t NOW = 1793361600;
constexpr int64_t DAY = 86400;

struct Log {
  std::vector<std::string> lines;
  void add(int64_t start, uint32_t seconds, uint16_t bp, const std::string& path) {
    lines.push_back(readinglog::toLine(Session{start, seconds, bp, path}));
  }
  readinglog::Stats stats(const std::string& book = {}) const {
    return readinglog::compute(
        [this](const std::function<bool(std::string_view)>& line) {
          for (const auto& l : lines)
            if (!line(l)) return;
        },
        NOW, readinglog::utcDay, book);
  }
};

}  // namespace

TEST(ReadingLog, NowIsAFridayInOctober) {
  const auto d = readinglog::utcDay(NOW);
  EXPECT_EQ(d.year, 2026);
  EXPECT_EQ(d.month, 10);
  EXPECT_EQ(d.day, 30);
  EXPECT_EQ(d.weekday, 4);
}

TEST(ReadingLog, LineRoundTrip) {
  Session s{NOW, 754, 4321, "/books/A\tB.epub"};
  Session back;
  ASSERT_TRUE(readinglog::fromLine(readinglog::toLine(s), back));
  EXPECT_EQ(back.start, NOW);
  EXPECT_EQ(back.seconds, 754u);
  EXPECT_EQ(back.progressBp, 4321);
  EXPECT_EQ(back.path, "/books/A B.epub");
  EXPECT_FALSE(readinglog::fromLine("garbage", back));
  EXPECT_FALSE(readinglog::fromLine("0\t1\t2\t/x.epub", back));
}

TEST(ReadingLog, NotesAreNotBooks) {
  EXPECT_TRUE(readinglog::isBook("/books/a.epub"));
  EXPECT_TRUE(readinglog::isBook("/Comics/b.XTCH"));
  EXPECT_FALSE(readinglog::isBook("/Vaults/V/note.md"));
  EXPECT_FALSE(readinglog::isBook("/notes.TXT"));
}

TEST(ReadingLog, TotalsByTodayWeekMonth) {
  Log log;
  log.add(NOW - 3600, 600, 100, "/a.epub");         // today
  log.add(NOW - 2 * DAY, 300, 200, "/a.epub");      // Wednesday, this week
  log.add(NOW - 6 * DAY, 120, 300, "/b.epub");      // Saturday last week, this month
  log.add(NOW - 40 * DAY, 60, 50, "/c.epub");       // September
  log.add(NOW + 10 * DAY, 999, 0, "/future.epub");  // a wrong clock: ignored
  const auto st = log.stats();
  EXPECT_EQ(st.todaySeconds, 600u);
  EXPECT_EQ(st.weekSeconds, 900u);
  EXPECT_EQ(st.monthSeconds, 1020u);
  EXPECT_EQ(st.totalSeconds, 1080u);
}

TEST(ReadingLog, RecentBooksNewestFirstDistinct) {
  Log log;
  log.add(NOW - 5 * DAY, 60, 1000, "/a.epub");
  log.add(NOW - 4 * DAY, 60, 2000, "/b.epub");
  log.add(NOW - 3 * DAY, 60, 3000, "/c.epub");
  log.add(NOW - 2 * DAY, 60, 4000, "/d.epub");
  log.add(NOW - 1 * DAY, 60, 1500, "/a.epub");
  const auto st = log.stats();
  ASSERT_EQ(st.recent.size(), 3u);
  EXPECT_EQ(st.recent[0].path, "/a.epub");
  EXPECT_EQ(st.recent[0].progressBp, 1500);
  EXPECT_EQ(st.recent[1].path, "/d.epub");
  EXPECT_EQ(st.recent[2].path, "/c.epub");
}

TEST(ReadingLog, CalendarCarriesProgressAndStopsAtToday) {
  Log log;
  log.add(NOW - 45 * DAY, 60, 500, "/a.epub");         // September: 5% carried into October
  log.add(NOW - 27 * DAY, 60, 1000, "/a.epub");        // Oct 3
  log.add(NOW - 27 * DAY + 600, 60, 1200, "/a.epub");  // later the same day
  log.add(NOW - 20 * DAY, 60, 2500, "/a.epub");        // Oct 10
  log.add(NOW - 19 * DAY, 60, 9000, "/other.epub");
  const auto st = log.stats("/a.epub");
  EXPECT_EQ(st.daysInMonth, 31);
  EXPECT_EQ(st.firstWeekday, 3);          // 2026-10-01 is a Thursday
  EXPECT_EQ(st.dayProgressBp[0], 500);    // Oct 1, from September
  EXPECT_EQ(st.dayProgressBp[2], 1200);   // Oct 3, the last session that day
  EXPECT_EQ(st.dayProgressBp[5], 1200);   // carried
  EXPECT_EQ(st.dayProgressBp[9], 2500);   // Oct 10
  EXPECT_EQ(st.dayProgressBp[29], 2500);  // today
  EXPECT_EQ(st.dayProgressBp[30], -1);    // tomorrow
}

TEST(ReadingLog, CalendarEmptyBeforeFirstRead) {
  Log log;
  log.add(NOW - 5 * DAY, 60, 700, "/new.epub");  // Oct 25
  const auto st = log.stats();
  EXPECT_EQ(st.calendarBook, "/new.epub");
  EXPECT_EQ(st.dayProgressBp[23], -1);
  EXPECT_EQ(st.dayProgressBp[24], 700);
  EXPECT_EQ(st.dayProgressBp[29], 700);
}

TEST(ReadingLog, StreakCountsConsecutiveDaysEndingTodayOrYesterday) {
  Log log;
  log.add(NOW, 60, 100, "/a.epub");              // today
  log.add(NOW - DAY, 60, 100, "/a.epub");        // yesterday
  log.add(NOW - DAY - 600, 60, 100, "/b.epub");  // same day again
  log.add(NOW - 2 * DAY, 60, 100, "/a.epub");
  log.add(NOW - 4 * DAY, 60, 100, "/a.epub");  // gap on day 3
  log.add(NOW - 5 * DAY, 60, 100, "/a.epub");
  log.add(NOW - 6 * DAY, 60, 100, "/a.epub");
  log.add(NOW - 7 * DAY, 60, 100, "/a.epub");
  log.add(NOW - 8 * DAY, 60, 100, "/a.epub");
  auto st = log.stats();
  EXPECT_EQ(st.currentStreakDays, 3);
  EXPECT_EQ(st.longestStreakDays, 5);

  Log y;  // nothing today, but yesterday keeps it alive
  y.add(NOW - DAY, 60, 100, "/a.epub");
  y.add(NOW - 2 * DAY, 60, 100, "/a.epub");
  EXPECT_EQ(y.stats().currentStreakDays, 2);

  Log old;  // last read two days ago: the streak is over, the best remains
  old.add(NOW - 2 * DAY, 60, 100, "/a.epub");
  old.add(NOW - 3 * DAY, 60, 100, "/a.epub");
  st = old.stats();
  EXPECT_EQ(st.currentStreakDays, 0);
  EXPECT_EQ(st.longestStreakDays, 2);

  EXPECT_EQ(Log{}.stats().longestStreakDays, 0);
}

TEST(ReadingLog, FinishedBooksUseTheLatestSession) {
  Log log;
  log.add(NOW - 100 * DAY, 60, 9900, "/done-then-reread.epub");
  log.add(NOW - 50 * DAY, 60, 500, "/done-then-reread.epub");  // started over: not finished
  log.add(NOW - 20 * DAY, 60, 9800, "/f1.epub");
  log.add(NOW - 10 * DAY, 60, 9799, "/almost.epub");
  log.add(NOW - 5 * DAY, 60, 10000, "/f2.epub");
  log.add(NOW - 400 * DAY, 60, 10000, "/lastyear.epub");  // 2025
  for (int i = 0; i < 5; ++i) log.add(NOW - (500 + i) * DAY, 60, 9900, "/old" + std::to_string(i) + ".epub");
  const auto st = log.stats();
  EXPECT_EQ(st.finishedCount, 8);
  EXPECT_EQ(st.finishedThisYear, 2);
  ASSERT_EQ(st.finished.size(), 5u);
  EXPECT_EQ(st.finished[0].path, "/f2.epub");
  EXPECT_EQ(st.finished[1].path, "/f1.epub");
  EXPECT_EQ(st.finished[2].path, "/lastyear.epub");
}

TEST(ReadingLog, DayMinutesAcrossBooks) {
  Log log;
  log.add(NOW - 3600, 600, 100, "/a.epub");      // 30th: 10 min
  log.add(NOW - 7200, 90, 100, "/b.epub");       // 30th: +1.5 min -> 12 (rounded up)
  log.add(NOW - 2 * DAY, 1, 100, "/b.epub");     // 28th: 1 min
  log.add(NOW - 40 * DAY, 600, 100, "/c.epub");  // September: not counted
  const auto st = log.stats();
  ASSERT_EQ(st.dayMinutes.size(), 31u);
  EXPECT_EQ(st.dayMinutes[29], 12);
  EXPECT_EQ(st.dayMinutes[27], 1);
  EXPECT_EQ(st.dayMinutes[0], 0);
}
