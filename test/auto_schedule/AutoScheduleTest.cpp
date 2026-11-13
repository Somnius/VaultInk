#include <AutoSchedule.h>
#include <SunTimes.h>
#include <gtest/gtest.h>

using namespace autoschedule;

TEST(AutoSchedule, WindowThatCrossesMidnight) {
  EXPECT_TRUE(inNightWindow(20, 7, 20));
  EXPECT_TRUE(inNightWindow(20, 7, 23));
  EXPECT_TRUE(inNightWindow(20, 7, 0));
  EXPECT_TRUE(inNightWindow(20, 7, 6));
  EXPECT_FALSE(inNightWindow(20, 7, 7));  // the end hour is day
  EXPECT_FALSE(inNightWindow(20, 7, 12));
  EXPECT_FALSE(inNightWindow(20, 7, 19));
}

TEST(AutoSchedule, WindowInsideOneDay) {
  EXPECT_TRUE(inNightWindow(1, 5, 1));
  EXPECT_TRUE(inNightWindow(1, 5, 4));
  EXPECT_FALSE(inNightWindow(1, 5, 5));
  EXPECT_FALSE(inNightWindow(1, 5, 0));
}

TEST(AutoSchedule, EmptyOrBadWindowIsNeverNight) {
  EXPECT_FALSE(inNightWindow(7, 7, 7));
  EXPECT_FALSE(inNightWindow(24, 7, 3));
  EXPECT_FALSE(inNightWindow(20, 99, 3));
  EXPECT_FALSE(inNightWindow(20, 7, 24));
}

TEST(AutoSchedule, StepsOnlyOnTransitions) {
  EXPECT_EQ(next(true, 20, 7, 21, 0), Step::EnterNight);
  EXPECT_EQ(next(true, 20, 7, 22, 1), Step::None);  // already applied; a manual change stays
  EXPECT_EQ(next(true, 20, 7, 7, 1), Step::EnterDay);
  EXPECT_EQ(next(true, 20, 7, 12, 0), Step::None);
}

TEST(AutoSchedule, DisabledDoesNothing) {
  EXPECT_EQ(next(false, 20, 7, 22, 0), Step::None);
  EXPECT_EQ(next(false, 20, 7, 12, 1), Step::None);
}

TEST(AutoSchedule, OtherBitsOfAppliedDoNotMatter) {
  EXPECT_EQ(next(true, 20, 7, 22, 3), Step::None);  // night, applied night plus the inverted bit
  EXPECT_EQ(next(true, 20, 7, 12, 3), Step::EnterDay);
  EXPECT_EQ(next(true, 20, 7, 22, 2), Step::EnterNight);  // bit 1 alone is the day side
}

// --- Sunset to sunrise (lib/AutoSchedule/SunTimes.h) ---

namespace {

struct SunCase {
  const char* place;
  int year, month, day;
  int latTenths, lonTenths;
  int offsetMin;
  int sunrise, sunset;  // minutes after local midnight
};

constexpr int hm(const int h, const int m) { return h * 60 + m; }

// Rise and Set from the U.S. Naval Observatory's "Complete Sun and Moon Data for One Day" service
// (https://aa.usno.navy.mil/api/rstt/oneday?date=..&coords=lat,lon&tz=..), fetched 2026-11-13 for these
// coordinates and offsets.
constexpr SunCase kUsno[] = {
    {"Athens, summer", 2026, 6, 21, 380, 237, 180, hm(6, 3), hm(20, 51)},
    {"Athens, winter", 2026, 12, 21, 380, 237, 120, hm(7, 37), hm(17, 9)},
    // The sunset of this solar day is 00:02-00:03 on the 22nd (USNO lists 00:02 for the evening before).
    {"Reykjavik, summer", 2026, 6, 21, 641, -219, 0, hm(2, 56), hm(24, 2)},
    {"Reykjavik, winter", 2026, 12, 21, 641, -219, 0, hm(11, 21), hm(15, 30)},
    {"Sydney, summer", 2026, 12, 21, -339, 1512, 660, hm(5, 41), hm(20, 6)},
    {"Sydney, winter", 2026, 6, 21, -339, 1512, 600, hm(7, 0), hm(16, 54)},
    {"Quito, equinox", 2026, 3, 20, -2, -785, -300, hm(6, 18), hm(18, 25)},
    {"Fiji, near the date line", 2026, 9, 23, -177, 1784, 720, hm(5, 56), hm(18, 2)},
};

}  // namespace

// The task asked for 10 minutes and the plan for 2; the formulas hold the 2.
TEST(SunTimes, MatchesUsnoWithinTwoMinutes) {
  for (const SunCase& c : kUsno) {
    const SunTimes sun = sunTimes(c.year, c.month, c.day, c.latTenths, c.lonTenths, c.offsetMin);
    ASSERT_EQ(sun.kind, SunDay::Normal) << c.place;
    EXPECT_NEAR(sun.sunrise, c.sunrise, 2) << c.place;
    EXPECT_NEAR(sun.sunset, c.sunset, 2) << c.place;
  }
}

// USNO for Tromso (69.7 N, 19.0 E): the sun is "continuously above the horizon" on 2026-06-21 and "continuously
// below the horizon" on 2026-12-21.
TEST(SunTimes, PolarDayAndNight) {
  EXPECT_EQ(sunTimes(2026, 6, 21, 697, 190, 120).kind, SunDay::PolarDay);
  EXPECT_EQ(sunTimes(2026, 12, 21, 697, 190, 60).kind, SunDay::PolarNight);
  EXPECT_EQ(sunTimes(2026, 6, 21, 900, 0, 0).kind, SunDay::PolarDay);  // the pole itself does not divide by zero
  EXPECT_EQ(sunTimes(2026, 6, 21, -900, 0, 0).kind, SunDay::PolarNight);
}

TEST(SunTimes, PhysicalInvariants) {
  // The equator has about twelve hours of day all year (a few minutes more, for refraction and the sun's disc).
  for (int month = 1; month <= 12; ++month) {
    const SunTimes sun = sunTimes(2026, month, 15, 0, 0, 0);
    ASSERT_EQ(sun.kind, SunDay::Normal);
    EXPECT_NEAR(sun.sunset - sun.sunrise, 727, 6) << month;
  }
  // In northern summer the day grows with latitude, in winter it shrinks; the south mirrors it.
  int previous = 0;
  for (int lat = 0; lat <= 600; lat += 100) {
    const SunTimes summer = sunTimes(2026, 6, 21, lat, 0, 0);
    const int length = summer.sunset - summer.sunrise;
    EXPECT_GT(length, previous) << lat;
    previous = length;
    const SunTimes winter = sunTimes(2026, 12, 21, lat, 0, 0);
    if (lat > 0) {
      EXPECT_LT(winter.sunset - winter.sunrise, 720) << lat;
    }
    const SunTimes south = sunTimes(2026, 12, 21, -lat, 0, 0);
    EXPECT_NEAR(south.sunset - south.sunrise, length, 12) << lat;
  }
  // The clock's offset only shifts both times; fifteen degrees of longitude are one hour.
  const SunTimes utc = sunTimes(2026, 4, 10, 380, 237, 0);
  const SunTimes plus3 = sunTimes(2026, 4, 10, 380, 237, 180);
  EXPECT_NEAR(plus3.sunrise - utc.sunrise, 180, 1);
  EXPECT_NEAR(plus3.sunset - utc.sunset, 180, 1);
  const SunTimes west = sunTimes(2026, 4, 10, 380, 87, 0);
  EXPECT_NEAR(west.sunrise - utc.sunrise, 60, 1);
}

TEST(SunTimes, BadInputHasNoLocation) {
  EXPECT_EQ(sunTimes(2026, 6, 21, 901, 0, 0).kind, SunDay::NoLocation);
  EXPECT_EQ(sunTimes(2026, 6, 21, 0, -1801, 0).kind, SunDay::NoLocation);
  EXPECT_EQ(sunTimes(2026, 13, 1, 0, 0, 0).kind, SunDay::NoLocation);
  EXPECT_EQ(SunTimes{}.kind, SunDay::NoLocation);
}

TEST(SunTimes, ParseAndFormatDegrees) {
  int v = 0;
  EXPECT_TRUE(parseDegrees("37.9", kMaxLatitudeTenths, v));
  EXPECT_EQ(v, 379);
  EXPECT_TRUE(parseDegrees(" -122.4 ", kMaxLongitudeTenths, v));
  EXPECT_EQ(v, -1224);
  EXPECT_TRUE(parseDegrees("+5", kMaxLatitudeTenths, v));
  EXPECT_EQ(v, 50);
  EXPECT_TRUE(parseDegrees("23,7", kMaxLongitudeTenths, v));
  EXPECT_EQ(v, 237);
  EXPECT_TRUE(parseDegrees("37.98", kMaxLatitudeTenths, v));  // the second decimal rounds
  EXPECT_EQ(v, 380);
  EXPECT_TRUE(parseDegrees("12.", kMaxLatitudeTenths, v));
  EXPECT_EQ(v, 120);
  EXPECT_TRUE(parseDegrees("-0.2", kMaxLatitudeTenths, v));
  EXPECT_EQ(v, -2);
  EXPECT_TRUE(parseDegrees("90", kMaxLatitudeTenths, v));
  v = 7;
  EXPECT_FALSE(parseDegrees("90.1", kMaxLatitudeTenths, v));
  EXPECT_FALSE(parseDegrees("180.1", kMaxLongitudeTenths, v));
  EXPECT_FALSE(parseDegrees("", kMaxLatitudeTenths, v));
  EXPECT_FALSE(parseDegrees("-", kMaxLatitudeTenths, v));
  EXPECT_FALSE(parseDegrees(".5", kMaxLatitudeTenths, v));
  EXPECT_FALSE(parseDegrees("37.9N", kMaxLatitudeTenths, v));
  EXPECT_FALSE(parseDegrees("3 7", kMaxLatitudeTenths, v));
  EXPECT_FALSE(parseDegrees("99999999999999999999", kMaxLatitudeTenths, v));
  EXPECT_FALSE(parseDegrees(nullptr, kMaxLatitudeTenths, v));
  EXPECT_EQ(v, 7);  // a failed parse leaves the value alone

  char buf[8];
  formatDegrees(-1224, buf, sizeof(buf));
  EXPECT_STREQ(buf, "-122.4");
  formatDegrees(-2, buf, sizeof(buf));
  EXPECT_STREQ(buf, "-0.2");
  formatDegrees(380, buf, sizeof(buf));
  EXPECT_STREQ(buf, "38.0");
  formatDegrees(-1800, buf, sizeof(buf));
  EXPECT_STREQ(buf, "-180.0");
}

TEST(SunTimes, UtcOffsetFromTwoBreakdowns) {
  EXPECT_EQ(utcOffsetMinutes(2026, 100, hm(15, 0), 2026, 100, hm(12, 0)), 180);
  EXPECT_EQ(utcOffsetMinutes(2026, 101, hm(1, 0), 2026, 100, hm(23, 0)), 120);   // local is already tomorrow
  EXPECT_EQ(utcOffsetMinutes(2026, 100, hm(20, 0), 2026, 101, hm(1, 0)), -300);  // local is still yesterday
  EXPECT_EQ(utcOffsetMinutes(2027, 0, hm(2, 0), 2026, 364, hm(12, 0)), 840);     // UTC+14 across the new year
  EXPECT_EQ(utcOffsetMinutes(2026, 364, hm(14, 0), 2027, 0, hm(2, 0)), -720);
  EXPECT_EQ(utcOffsetMinutes(2026, 100, hm(17, 45), 2026, 100, hm(12, 0)), 345);  // Kathmandu
}

TEST(SunTimes, NightIsFromSunsetToSunrise) {
  const SunTimes athens{SunDay::Normal, hm(6, 3), hm(20, 51)};
  EXPECT_TRUE(inSunNight(athens, hm(0, 0)));
  EXPECT_TRUE(inSunNight(athens, hm(6, 2)));
  EXPECT_FALSE(inSunNight(athens, hm(6, 3)));
  EXPECT_FALSE(inSunNight(athens, hm(20, 50)));
  EXPECT_TRUE(inSunNight(athens, hm(20, 51)));
  EXPECT_TRUE(inSunNight(athens, hm(23, 59)));
  // Reykjavik in June: sunset just after midnight, so the night sits inside one calendar day.
  const SunTimes reykjavik{SunDay::Normal, hm(2, 56), hm(24, 2)};
  EXPECT_FALSE(inSunNight(reykjavik, hm(0, 1)));
  EXPECT_TRUE(inSunNight(reykjavik, hm(0, 2)));
  EXPECT_TRUE(inSunNight(reykjavik, hm(2, 55)));
  EXPECT_FALSE(inSunNight(reykjavik, hm(2, 56)));
  EXPECT_FALSE(inSunNight(reykjavik, hm(23, 0)));
  // A sunrise before local midnight (negative) wraps the same way.
  const SunTimes early{SunDay::Normal, -30, hm(18, 0)};
  EXPECT_TRUE(inSunNight(early, hm(18, 0)));
  EXPECT_TRUE(inSunNight(early, hm(23, 29)));
  EXPECT_FALSE(inSunNight(early, hm(23, 30)));
  EXPECT_FALSE(inSunNight(early, hm(12, 0)));
  EXPECT_FALSE(inSunNight(SunTimes{SunDay::PolarNight, 0, 0}, hm(12, 0)));
  EXPECT_FALSE(inSunNight(athens, 1440));
}

TEST(SunTimes, StepsOnSunsetAndSunrise) {
  const SunTimes athens{SunDay::Normal, hm(6, 3), hm(20, 51)};
  EXPECT_EQ(nextSun(true, athens, 20, 7, 20, 50, 0), Step::None);  // the fixed hours would already say night
  EXPECT_EQ(nextSun(true, athens, 20, 7, 20, 51, 0), Step::EnterNight);
  EXPECT_EQ(nextSun(true, athens, 20, 7, 23, 0, 1), Step::None);
  EXPECT_EQ(nextSun(true, athens, 20, 7, 6, 2, 1), Step::None);
  EXPECT_EQ(nextSun(true, athens, 20, 7, 6, 3, 1), Step::EnterDay);  // the fixed hours would wait until 7
  EXPECT_EQ(nextSun(true, athens, 20, 7, 6, 3, 3), Step::EnterDay);
  EXPECT_EQ(nextSun(false, athens, 20, 7, 23, 0, 0), Step::None);
  EXPECT_EQ(nextSun(true, athens, 20, 7, 24, 0, 0), Step::None);
}

TEST(SunTimes, PolarDatesAndNoLocationFollowTheFixedHours) {
  for (const SunDay kind : {SunDay::PolarDay, SunDay::PolarNight, SunDay::NoLocation}) {
    const SunTimes sun{kind, 0, 0};
    EXPECT_EQ(nextSun(true, sun, 20, 7, 21, 0, 0), Step::EnterNight);
    EXPECT_EQ(nextSun(true, sun, 20, 7, 12, 0, 0), Step::None);
    EXPECT_EQ(nextSun(true, sun, 20, 7, 7, 0, 1), Step::EnterDay);
    EXPECT_EQ(nextSun(false, sun, 20, 7, 21, 0, 0), Step::None);
  }
}
