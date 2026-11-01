#include "RideLog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "Alarms.h"

namespace euc {

// Howard Hinnant's days_from_civil / civil_from_days (public domain algorithms).
int64_t toEpochSeconds(const DateTime& dt) {
  int64_t y = dt.year;
  const int64_t m = dt.month;
  const int64_t d = dt.day;
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + doe - 719468;
  return days * 86400 + dt.hour * 3600LL + dt.minute * 60LL + dt.second;
}

DateTime fromEpochSeconds(const int64_t seconds) {
  DateTime dt;
  int64_t days = seconds / 86400;
  int64_t rem = seconds % 86400;
  if (rem < 0) {
    rem += 86400;
    --days;
  }
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const int64_t m = mp + (mp < 10 ? 3 : -9);
  dt.year = static_cast<uint16_t>(y + (m <= 2 ? 1 : 0));
  dt.month = static_cast<uint8_t>(m);
  dt.day = static_cast<uint8_t>(d);
  dt.hour = static_cast<uint8_t>(rem / 3600);
  dt.minute = static_cast<uint8_t>((rem / 60) % 60);
  dt.second = static_cast<uint8_t>(rem % 60);
  return dt;
}

namespace ridelog {

namespace {
// Appends one float field (or nothing when absent) and the separator. Returns false when the buffer is full.
bool put(char* buf, const size_t cap, size_t& pos, const char* fmt, const double v) {
  if (pos >= cap) {
    return false;
  }
  const int n = snprintf(buf + pos, cap - pos, fmt, v);
  if (n < 0 || static_cast<size_t>(n) >= cap - pos) {
    pos = cap;
    return false;
  }
  pos += static_cast<size_t>(n);
  return true;
}

bool putRaw(char* buf, const size_t cap, size_t& pos, const char* s) {
  if (pos >= cap) {
    return false;
  }
  const int n = snprintf(buf + pos, cap - pos, "%s", s);
  if (n < 0 || static_cast<size_t>(n) >= cap - pos) {
    pos = cap;
    return false;
  }
  pos += static_cast<size_t>(n);
  return true;
}

bool fatSafe(const char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  if (u < 0x20 || u >= 0x7F) {
    return false;
  }
  return std::strchr("\\/:*?\"<>|", c) == nullptr;
}
}  // namespace

int formatHeader(char* buf, const size_t cap) { return snprintf(buf, cap, "%s\n", kHeader); }

int formatLine(char* buf, const size_t cap, const Telemetry& t, const DateTime& when) {
  size_t pos = 0;
  const int n = snprintf(buf, cap, "%04u-%02u-%02u %02u:%02u:%02u.%03u,", when.year, when.month, when.day, when.hour,
                         when.minute, when.second, when.millis);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    return n;
  }
  pos = static_cast<size_t>(n);
  float temp = 0.0f;
  const bool hasTemp = Alarms::maxTemp(t, temp);
  const std::array<std::pair<bool, double>, 10> fields = {{
      {t.has(F_SPEED), t.speedKmh},
      {t.has(F_VOLTAGE), t.voltage},
      {t.has(F_PWM), t.pwm},
      {t.has(F_CURRENT), t.current},
      {t.has(F_POWER), t.power},
      {t.has(F_BATTERY), t.batteryPct},
      {t.has(F_TOTAL), t.totalKm},
      {hasTemp, temp},
      {t.has(F_PITCH), t.pitch},
      {t.has(F_ROLL), t.roll},
  }};
  const std::array<const char*, 10> fmts = {"%.1f,", "%.2f,", "%.1f,", "%.2f,", "%.1f,",
                                            "%.1f,", "%.3f,", "%.1f,", "%.1f,", "%.1f,"};
  for (size_t i = 0; i < fields.size(); ++i) {
    const bool ok = fields[i].first ? put(buf, cap, pos, fmts[i], fields[i].second) : putRaw(buf, cap, pos, ",");
    if (!ok) {
      return static_cast<int>(cap);
    }
  }
  const bool ok = t.has(F_TRIP) ? put(buf, cap, pos, "%.3f\n", t.tripKm) : putRaw(buf, cap, pos, "\n");
  return ok ? static_cast<int>(pos) : static_cast<int>(cap);
}

int fileName(char* buf, const size_t cap, const char* nickname, const DateTime& when) {
  char nick[32] = {};
  size_t n = 0;
  for (const char* p = nickname; p != nullptr && *p != '\0' && n < sizeof(nick) - 1; ++p) {
    nick[n++] = fatSafe(*p) ? *p : '_';
  }
  // Trim trailing dots/spaces (FAT drops them) and underscores from the replacements.
  while (n > 0 && (nick[n - 1] == '.' || nick[n - 1] == ' ' || nick[n - 1] == '_')) {
    nick[--n] = '\0';
  }
  if (n == 0) {
    snprintf(nick, sizeof(nick), "ride");
  }
  return snprintf(buf, cap, "%s_%04u-%02u-%02u_%02u-%02u.csv", nick, when.year, when.month, when.day, when.hour,
                  when.minute);
}

}  // namespace ridelog

// ---- parser ------------------------------------------------------------------------------------------------------

namespace {
constexpr float kMovingKmh = 2.0f;

// Parses "YYYY-MM-DD HH:MM:SS[.mmm]" (also 'T' as separator). False on anything else.
bool parseDate(const char* s, const size_t n, DateTime& out) {
  if (n < 19) {
    return false;
  }
  const auto digits = [&](const size_t at, const size_t count, int& v) {
    v = 0;
    for (size_t i = at; i < at + count; ++i) {
      if (i >= n || !std::isdigit(static_cast<unsigned char>(s[i]))) {
        return false;
      }
      v = v * 10 + (s[i] - '0');
    }
    return true;
  };
  int y = 0;
  int mo = 0;
  int d = 0;
  int h = 0;
  int mi = 0;
  int se = 0;
  if (!digits(0, 4, y) || s[4] != '-' || !digits(5, 2, mo) || s[7] != '-' || !digits(8, 2, d) ||
      (s[10] != ' ' && s[10] != 'T') || !digits(11, 2, h) || s[13] != ':' || !digits(14, 2, mi) || s[16] != ':' ||
      !digits(17, 2, se)) {
    return false;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) {
    return false;
  }
  int ms = 0;
  if (n > 20 && s[19] == '.') {
    int scale = 100;
    for (size_t i = 20; i < n && std::isdigit(static_cast<unsigned char>(s[i])); ++i) {
      ms += (s[i] - '0') * scale;
      scale /= 10;
    }
  }
  out.year = static_cast<uint16_t>(y);
  out.month = static_cast<uint8_t>(mo);
  out.day = static_cast<uint8_t>(d);
  out.hour = static_cast<uint8_t>(h);
  out.minute = static_cast<uint8_t>(mi);
  out.second = static_cast<uint8_t>(se);
  out.millis = static_cast<uint16_t>(ms);
  return true;
}

// Splits `s` on commas into at most `maxCols` (start, length) pairs. Returns the column count.
struct Col {
  const char* p;
  size_t n;
};

size_t split(const char* s, const size_t n, Col* cols, const size_t maxCols) {
  size_t count = 0;
  size_t start = 0;
  for (size_t i = 0; i <= n && count < maxCols; ++i) {
    if (i == n || s[i] == ',') {
      cols[count++] = {s + start, i - start};
      start = i + 1;
    }
  }
  return count;
}

bool parseFloat(const Col& c, float& out) {
  if (c.n == 0 || c.n > 31) {
    return false;
  }
  char tmp[32];
  std::memcpy(tmp, c.p, c.n);
  tmp[c.n] = '\0';
  char* end = nullptr;
  const double v = std::strtod(tmp, &end);
  if (end == tmp || *end != '\0') {
    return false;
  }
  out = static_cast<float>(v);
  return true;
}

bool colIs(const Col& c, const char* name) {
  const size_t n = std::strlen(name);
  return c.n == n && std::strncmp(c.p, name, n) == 0;
}

constexpr size_t kMaxCols = 24;
}  // namespace

void RideLogParser::reset() { *this = RideLogParser(); }

void RideLogParser::feed(const char* data, const size_t size) {
  for (size_t i = 0; i < size; ++i) {
    const char c = data[i];
    if (c == '\n') {
      if (overflow_) {
        ++summary_.badLines;
      } else {
        line(buf_, len_);
      }
      len_ = 0;
      overflow_ = false;
    } else if (c == '\r') {
      continue;
    } else if (len_ < ridelog::kMaxLine) {
      buf_[len_++] = c;
    } else {
      overflow_ = true;
    }
  }
}

void RideLogParser::finish() {
  if (len_ > 0 || overflow_) {
    if (overflow_) {
      ++summary_.badLines;
    } else {
      line(buf_, len_);
    }
    len_ = 0;
    overflow_ = false;
  }
  if (!summary_.valid) {
    return;
  }
  summary_.durationSec = static_cast<uint32_t>(std::max<int64_t>(0, lastEpoch_ - startEpoch_));
  if (batterySeen_) {
    summary_.batteryUsedPct = std::max(0.0f, firstBattery_ - lastBattery_);
  }
  if (totalSeen_ && lastTotal_ > firstTotal_) {
    summary_.distanceKm = lastTotal_ - firstTotal_;
  } else if (tripSeen_ && lastTrip_ > firstTrip_) {
    summary_.distanceKm = lastTrip_ - firstTrip_;
  } else {
    summary_.distanceKm = integratedKm_;
  }
  summary_.avgSpeedKmh = speedCount_ > 0 ? static_cast<float>(speedSum_ / speedCount_) : 0.0f;
}

void RideLogParser::line(const char* s, size_t n) {
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
    --n;
  }
  if (n == 0) {
    return;
  }
  if (!headerSeen_) {
    Col cols[kMaxCols];
    const size_t count = split(s, n, cols, kMaxCols);
    for (size_t i = 0; i < count; ++i) {
      const int idx = static_cast<int>(i);
      if (colIs(cols[i], "Date")) {
        colDate_ = idx;
      } else if (colIs(cols[i], "Speed")) {
        colSpeed_ = idx;
      } else if (colIs(cols[i], "Voltage")) {
        colVoltage_ = idx;
      } else if (colIs(cols[i], "Battery level")) {
        colBattery_ = idx;
      } else if (colIs(cols[i], "Total mileage")) {
        colTotal_ = idx;
      } else if (colIs(cols[i], "Trip distance")) {
        colTrip_ = idx;
      }
    }
    headerSeen_ = true;
    if (colDate_ < 0) {
      ++summary_.badLines;  // not our header; every later line will fail on the date
    }
    return;
  }
  dataLine(s, n);
}

void RideLogParser::dataLine(const char* s, const size_t n) {
  Col cols[kMaxCols];
  const size_t count = split(s, n, cols, kMaxCols);
  DateTime when;
  if (colDate_ < 0 || static_cast<size_t>(colDate_) >= count || !parseDate(cols[colDate_].p, cols[colDate_].n, when)) {
    ++summary_.badLines;
    return;
  }
  const int64_t epoch = toEpochSeconds(when);
  const auto field = [&](const int idx, float& v) {
    return idx >= 0 && static_cast<size_t>(idx) < count && parseFloat(cols[idx], v);
  };
  float speed = 0.0f;
  const bool hasSpeed = field(colSpeed_, speed);
  const bool speedPresent = colSpeed_ >= 0 && static_cast<size_t>(colSpeed_) < count && cols[colSpeed_].n > 0;
  if ((speedPresent && !hasSpeed) || (hasSpeed && (speed < 0.0f || speed > 200.0f))) {
    ++summary_.badLines;  // a damaged number; the line is not trusted
    return;
  }

  if (!summary_.valid) {
    summary_.valid = true;
    summary_.start = when;
    startEpoch_ = epoch;
  } else if (hasSpeed && speed >= kMovingKmh && epoch >= lastEpoch_) {
    const double dt = static_cast<double>(epoch - lastEpoch_) + (static_cast<int>(when.millis) - lastMillis_) / 1000.0;
    if (dt > 0.0 && dt <= 60.0) {
      integratedKm_ += static_cast<float>(speed * dt / 3600.0);
    }
  }
  summary_.end = when;
  lastEpoch_ = epoch;
  lastMillis_ = when.millis;
  ++summary_.lines;

  if (hasSpeed) {
    summary_.topSpeedKmh = std::max(summary_.topSpeedKmh, speed);
    if (speed >= kMovingKmh) {
      speedSum_ += speed;
      ++speedCount_;
    }
  }
  float v = 0.0f;
  if (field(colVoltage_, v) && v > 0.0f) {
    if (summary_.minVoltage == 0.0f || v < summary_.minVoltage) {
      summary_.minVoltage = v;
    }
    summary_.maxVoltage = std::max(summary_.maxVoltage, v);
  }
  float b = 0.0f;
  if (field(colBattery_, b)) {
    if (!batterySeen_) {
      batterySeen_ = true;
      firstBattery_ = b;
    }
    lastBattery_ = b;
  }
  float total = 0.0f;
  if (field(colTotal_, total) && total > 0.0f) {
    if (!totalSeen_) {
      totalSeen_ = true;
      firstTotal_ = total;
    }
    lastTotal_ = std::max(lastTotal_, total);
  }
  float trip = 0.0f;
  if (field(colTrip_, trip)) {
    if (!tripSeen_) {
      tripSeen_ = true;
      firstTrip_ = trip;
    }
    lastTrip_ = std::max(lastTrip_, trip);
  }
}

}  // namespace euc
