#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Reading time, kept for the statistics sleep screen (kos25k #23). One text line per reading
// session in /.vaultink/reading-log.tsv:
//   <start epoch>\t<seconds>\t<progress at the end, 0-10000>\t<book path>
// Only books are logged (no notes), only sessions the trusted clock could date. Stats are read by
// streaming the file, so memory stays small however long the log gets.
namespace readinglog {

constexpr const char* LOG_PATH = "/.vaultink/reading-log.tsv";

struct Session {
  int64_t start = 0;  // UTC epoch seconds
  uint32_t seconds = 0;
  uint16_t progressBp = 0;  // 0..10000 at the end of the session
  std::string path;
};

// A local calendar day: year, month 1-12, day 1-31, weekday 0 = Monday .. 6 = Sunday.
struct Day {
  int year = 0;
  int month = 0;
  int day = 0;
  int weekday = 0;
};
using LocalDay = std::function<Day(int64_t epoch)>;
// UTC, for tests and when no timezone is known.
Day utcDay(int64_t epoch);

std::string toLine(const Session& s);
bool fromLine(std::string_view line, Session& out);
// True for files whose reading counts as a book (not Markdown notes or plain text).
bool isBook(std::string_view path);

struct BookProgress {
  std::string path;
  uint16_t progressBp = 0;
  int64_t lastRead = 0;
};

struct Stats {
  uint32_t totalSeconds = 0;
  uint32_t monthSeconds = 0;
  uint32_t weekSeconds = 0;  // Monday to now
  uint32_t todaySeconds = 0;
  std::vector<BookProgress> recent;  // newest first, distinct books, up to `recentCount`
  // For this month's calendar: the progress of `calendarBook` at the end of each day (index 0 =
  // day 1), carried over to days without reading; -1 before the book was first read this month.
  std::string calendarBook;
  std::vector<int> dayProgressBp;
  int monthYear = 0;
  int month = 0;
  int daysInMonth = 0;
  int firstWeekday = 0;  // weekday of day 1 (0 = Monday)
  int today = 0;         // day of the month
};

// `lines` streams the log (one line per call, return false to stop). `calendarBook` is the book
// open now (empty: the newest in the log).
Stats compute(const std::function<void(const std::function<bool(std::string_view)>&)>& lines, int64_t now,
              const LocalDay& localDay, std::string calendarBook = {}, size_t recentCount = 3);

int daysIn(int year, int month);

}  // namespace readinglog
