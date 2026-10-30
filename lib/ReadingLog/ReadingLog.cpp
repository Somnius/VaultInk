#include "ReadingLog.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace readinglog {
namespace {

bool endsWithNoCase(std::string_view s, std::string_view suffix) {
  if (s.size() < suffix.size()) return false;
  for (size_t i = 0; i < suffix.size(); ++i) {
    char c = s[s.size() - suffix.size() + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm), to compare days.
int64_t dayNumber(int y, const int m, const int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

}  // namespace

Day utcDay(const int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm tmv{};
#if defined(_WIN32)
  gmtime_s(&tmv, &t);
#else
  gmtime_r(&t, &tmv);
#endif
  return Day{tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, (tmv.tm_wday + 6) % 7};
}

int daysIn(const int year, const int month) {
  static constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 30;
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) return 29;
  return DAYS[month - 1];
}

bool isBook(const std::string_view path) {
  return !(endsWithNoCase(path, ".md") || endsWithNoCase(path, ".txt") || endsWithNoCase(path, ".markdown"));
}

std::string toLine(const Session& s) {
  std::string path = s.path;
  std::replace(path.begin(), path.end(), '\t', ' ');
  std::replace(path.begin(), path.end(), '\n', ' ');
  char head[64];
  snprintf(head, sizeof(head), "%lld\t%lu\t%u\t", static_cast<long long>(s.start),
           static_cast<unsigned long>(s.seconds), static_cast<unsigned>(s.progressBp));
  return head + path;
}

bool fromLine(const std::string_view line, Session& out) {
  size_t tabs[3];
  size_t from = 0;
  for (size_t& t : tabs) {
    t = line.find('\t', from);
    if (t == std::string_view::npos) return false;
    from = t + 1;
  }
  const std::string start(line.substr(0, tabs[0]));
  const std::string secs(line.substr(tabs[0] + 1, tabs[1] - tabs[0] - 1));
  const std::string bp(line.substr(tabs[1] + 1, tabs[2] - tabs[1] - 1));
  char* end = nullptr;
  out.start = std::strtoll(start.c_str(), &end, 10);
  if (end == start.c_str() || out.start <= 0) return false;
  out.seconds = static_cast<uint32_t>(std::strtoul(secs.c_str(), nullptr, 10));
  out.progressBp = static_cast<uint16_t>(std::min<unsigned long>(10000, std::strtoul(bp.c_str(), nullptr, 10)));
  out.path.assign(line.substr(tabs[2] + 1));
  while (!out.path.empty() && (out.path.back() == '\r' || out.path.back() == ' ')) out.path.pop_back();
  return !out.path.empty();
}

Stats compute(const std::function<void(const std::function<bool(std::string_view)>&)>& lines, const int64_t now,
              const LocalDay& localDay, std::string calendarBook, const size_t recentCount) {
  Stats st;
  const Day today = localDay(now);
  st.monthYear = today.year;
  st.month = today.month;
  st.today = today.day;
  st.daysInMonth = daysIn(today.year, today.month);
  st.firstWeekday = static_cast<int>(((dayNumber(today.year, today.month, 1) % 7) + 7 + 3) % 7);  // 1970-01-01 = Thu
  const int64_t todayNum = dayNumber(today.year, today.month, today.day);
  const int64_t weekStart = todayNum - today.weekday;

  // Pass 1: totals and the recent books (newest session per book).
  std::vector<BookProgress> books;
  int64_t newestStart = 0;
  std::string newestBook;
  lines([&](const std::string_view line) {
    Session s;
    if (!fromLine(line, s) || s.start > now + 60) return true;
    const Day d = localDay(s.start);
    const int64_t num = dayNumber(d.year, d.month, d.day);
    st.totalSeconds += s.seconds;
    if (d.year == today.year && d.month == today.month) st.monthSeconds += s.seconds;
    if (num >= weekStart && num <= todayNum) st.weekSeconds += s.seconds;
    if (num == todayNum) st.todaySeconds += s.seconds;
    auto it = std::find_if(books.begin(), books.end(), [&](const BookProgress& b) { return b.path == s.path; });
    if (it == books.end()) {
      books.push_back({s.path, s.progressBp, s.start});
    } else if (s.start >= it->lastRead) {
      it->progressBp = s.progressBp;
      it->lastRead = s.start;
    }
    if (s.start >= newestStart) {
      newestStart = s.start;
      newestBook = s.path;
    }
    return true;
  });
  std::sort(books.begin(), books.end(),
            [](const BookProgress& a, const BookProgress& b) { return a.lastRead > b.lastRead; });
  if (books.size() > recentCount) books.resize(recentCount);
  st.recent = std::move(books);

  // Pass 2: the calendar book's progress at the end of each day this month.
  st.calendarBook = calendarBook.empty() ? newestBook : std::move(calendarBook);
  st.dayProgressBp.assign(st.daysInMonth, -1);
  if (st.calendarBook.empty()) return st;
  std::vector<int64_t> lastAt(st.daysInMonth, 0);
  int before = -1;
  int64_t beforeAt = 0;
  const int64_t monthStart = dayNumber(today.year, today.month, 1);
  lines([&](const std::string_view line) {
    Session s;
    if (!fromLine(line, s) || s.path != st.calendarBook || s.start > now + 60) return true;
    const Day d = localDay(s.start);
    const int64_t num = dayNumber(d.year, d.month, d.day);
    if (num < monthStart) {
      if (s.start >= beforeAt) {
        before = s.progressBp;
        beforeAt = s.start;
      }
    } else if (d.year == today.year && d.month == today.month && s.start >= lastAt[d.day - 1]) {
      st.dayProgressBp[d.day - 1] = s.progressBp;
      lastAt[d.day - 1] = s.start;
    }
    return true;
  });
  // Days without reading keep the last progress; days after today stay empty.
  int carry = before;
  for (int i = 0; i < st.daysInMonth; ++i) {
    if (i + 1 > st.today) {
      st.dayProgressBp[i] = -1;
      continue;
    }
    if (st.dayProgressBp[i] < 0) {
      st.dayProgressBp[i] = carry;
    } else {
      carry = st.dayProgressBp[i];
    }
  }
  return st;
}

}  // namespace readinglog
