#pragma once

// Sunrise and sunset for Auto Warmth's "Sunset to Sunrise" window. Platform-free (only <cmath>), host-tested in
// test/auto_schedule against the U.S. Naval Observatory's tables.
//
// The formulas are the ones of NOAA's solar calculator (Global Monitoring Laboratory, "General Solar Position
// Calculations" and the NOAA_Solar_Calculations_day spreadsheet, https://gml.noaa.gov/grad/solcalc/calcdetails.html),
// which follow Jean Meeus, Astronomical Algorithms: the sun's mean longitude and anomaly, the equation of centre,
// the apparent longitude, the obliquity, the declination, the equation of time, and the hour angle of a sun whose
// centre is 90.833 degrees from the zenith (refraction and the sun's radius). They are evaluated once, at local noon
// of the date, which is good to about a minute away from the poles.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "AutoSchedule.h"

namespace autoschedule {

enum class SunDay : uint8_t {
  Normal,      // the sun rises and sets
  PolarDay,    // it never sets on this date
  PolarNight,  // it never rises on this date
  NoLocation,  // no usable latitude/longitude
};

// Minutes after local midnight of the date asked for. A place whose clock is far from its sun time can have a value
// below 0 or above 1439 (Reykjavik in June: sunset a few minutes after midnight).
struct SunTimes {
  SunDay kind = SunDay::NoLocation;
  int16_t sunrise = 0;
  int16_t sunset = 0;
};

constexpr int kMaxLatitudeTenths = 900;
constexpr int kMaxLongitudeTenths = 1800;

// Reads degrees with one decimal ("37.9", "-122.4", "+5", "23,7"; blanks around it are fine) into tenths of a
// degree. A second decimal rounds the first. False for anything else or a value beyond +-limitTenths.
inline bool parseDegrees(const char* text, const int limitTenths, int& tenths) {
  if (text == nullptr) return false;
  const char* p = text;
  while (*p == ' ') ++p;
  bool negative = false;
  if (*p == '-' || *p == '+') {
    negative = *p == '-';
    ++p;
  }
  if (*p < '0' || *p > '9') return false;
  long whole = 0;
  while (*p >= '0' && *p <= '9') {
    whole = whole * 10 + (*p - '0');
    if (whole > 10000) return false;
    ++p;
  }
  long value = whole * 10;
  if (*p == '.' || *p == ',') {
    ++p;
    if (*p >= '0' && *p <= '9') {
      value += *p - '0';
      ++p;
      if (*p >= '5' && *p <= '9') value += 1;
      while (*p >= '0' && *p <= '9') ++p;
    }
  }
  while (*p == ' ') ++p;
  if (*p != '\0' || value > limitTenths) return false;
  tenths = static_cast<int>(negative ? -value : value);
  return true;
}

// Writes tenths of a degree back as text ("-122.4"); cap includes the NUL (8 is enough).
inline void formatDegrees(const int tenths, char* out, const size_t cap) {
  const int magnitude = tenths < 0 ? -tenths : tenths;
  std::snprintf(out, cap, "%s%d.%d", tenths < 0 ? "-" : "", magnitude / 10, magnitude % 10);
}

// Days from 1970-01-01 to a civil date (Howard Hinnant's days_from_civil).
inline long daysFromCivil(int year, const int month, const int day) {
  year -= month <= 2 ? 1 : 0;
  const long era = (year >= 0 ? year : year - 399) / 400;
  const long yoe = year - era * 400;
  const long doy = (153L * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}

// The local clock's offset from UTC in minutes (east positive), from the same moment broken down as local time and
// as UTC: years, day of year, and minute of day.
inline int utcOffsetMinutes(const int localYear, const int localYearDay, const int localMinute, const int utcYear,
                            const int utcYearDay, const int utcMinute) {
  int dayDelta = 0;
  if (localYear != utcYear) {
    dayDelta = localYear > utcYear ? 1 : -1;
  } else if (localYearDay != utcYearDay) {
    dayDelta = localYearDay > utcYearDay ? 1 : -1;
  }
  return dayDelta * 1440 + localMinute - utcMinute;
}

// Sunrise and sunset of a local calendar date at latitude/longitude (tenths of a degree, north and east positive)
// for a clock that is `utcOffsetMin` minutes ahead of UTC (daylight saving included).
inline SunTimes sunTimes(const int year, const int month, const int day, const int latitudeTenths,
                         const int longitudeTenths, const int utcOffsetMin) {
  SunTimes out;
  if (latitudeTenths < -kMaxLatitudeTenths || latitudeTenths > kMaxLatitudeTenths ||
      longitudeTenths < -kMaxLongitudeTenths || longitudeTenths > kMaxLongitudeTenths || month < 1 || month > 12 ||
      day < 1 || day > 31) {
    return out;
  }
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kRad = kPi / 180.0;
  // At the pole itself cos(latitude) is 0; a tenth of a degree away the formulas still hold.
  const int clampedLat = latitudeTenths > 899 ? 899 : latitudeTenths < -899 ? -899 : latitudeTenths;
  const double lat = clampedLat / 10.0 * kRad;
  const double lon = longitudeTenths / 10.0;

  // Julian century of local noon.
  const double julianDay =
      static_cast<double>(daysFromCivil(year, month, day)) + 2440587.5 + 0.5 - utcOffsetMin / 1440.0;
  const double t = (julianDay - 2451545.0) / 36525.0;

  const double meanLong = std::fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
  const double meanAnom = 357.52911 + t * (35999.05029 - 0.0001537 * t);
  const double ecc = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
  const double centre = std::sin(meanAnom * kRad) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                        std::sin(2 * meanAnom * kRad) * (0.019993 - 0.000101 * t) +
                        std::sin(3 * meanAnom * kRad) * 0.000289;
  const double omega = (125.04 - 1934.136 * t) * kRad;
  const double appLong = (meanLong + centre - 0.00569 - 0.00478 * std::sin(omega)) * kRad;
  const double meanObliq = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
  const double obliq = (meanObliq + 0.00256 * std::cos(omega)) * kRad;
  const double decl = std::asin(std::sin(obliq) * std::sin(appLong));

  const double y = std::tan(obliq / 2) * std::tan(obliq / 2);
  const double l0 = meanLong * kRad;
  const double m = meanAnom * kRad;
  const double eqTime = 4.0 / kRad *
                        (y * std::sin(2 * l0) - 2 * ecc * std::sin(m) + 4 * ecc * y * std::sin(m) * std::cos(2 * l0) -
                         0.5 * y * y * std::sin(4 * l0) - 1.25 * ecc * ecc * std::sin(2 * m));  // minutes

  const double cosHour = std::cos(90.833 * kRad) / (std::cos(lat) * std::cos(decl)) - std::tan(lat) * std::tan(decl);
  if (cosHour > 1.0) {
    out.kind = SunDay::PolarNight;
    return out;
  }
  if (cosHour < -1.0) {
    out.kind = SunDay::PolarDay;
    return out;
  }
  const double halfDay = 4.0 * std::acos(cosHour) / kRad;  // minutes from sunrise to solar noon
  const double noon = 720.0 - 4.0 * lon - eqTime + utcOffsetMin;
  out.kind = SunDay::Normal;
  out.sunrise = static_cast<int16_t>(std::lround(noon - halfDay));
  out.sunset = static_cast<int16_t>(std::lround(noon + halfDay));
  return out;
}

// True when `minuteOfDay` (0-1439) is between sunset and sunrise. Only for SunDay::Normal.
inline bool inSunNight(const SunTimes& sun, const int minuteOfDay) {
  if (sun.kind != SunDay::Normal || minuteOfDay < 0 || minuteOfDay > 1439) return false;
  const int rise = ((sun.sunrise % 1440) + 1440) % 1440;
  const int set = ((sun.sunset % 1440) + 1440) % 1440;
  if (rise == set) return false;
  if (rise < set) return minuteOfDay < rise || minuteOfDay >= set;
  return minuteOfDay >= set && minuteOfDay < rise;  // the clock is far from sun time: the night is inside one day
}

// Like next(), for the "Sunset to Sunrise" window. On a date with no sunrise or no sunset (polar day or night), and
// with no usable location, the fixed hours decide instead.
inline Step nextSun(const bool enabled, const SunTimes& sun, const uint8_t startHour, const uint8_t endHour,
                    const uint8_t hour, const uint8_t minute, const uint8_t applied) {
  if (sun.kind != SunDay::Normal) return next(enabled, startHour, endHour, hour, applied);
  if (!enabled || hour > 23 || minute > 59) return Step::None;
  const bool night = inSunNight(sun, hour * 60 + minute);
  const bool appliedNight = (applied & 1) != 0;
  if (night && !appliedNight) return Step::EnterNight;
  if (!night && appliedNight) return Step::EnterDay;
  return Step::None;
}

}  // namespace autoschedule
