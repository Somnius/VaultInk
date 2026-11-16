#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "src/network/WifiAutoJoin.h"

namespace {
using Names = std::vector<std::string>;

Names order(const std::vector<wifijoin::Seen>& scan, const std::set<std::string>& saved,
            const std::string& hidden = "") {
  return wifijoin::savedInRange(scan, [&saved](const std::string& ssid) { return saved.count(ssid) != 0; }, hidden);
}
}  // namespace

// Which saved network a job that needs Wi-Fi joins by itself, and what the Wi-Fi screen shows
// when it can't join one.

TEST(WifiAutoJoinTest, SavedNetworksStrongestFirst) {
  EXPECT_EQ(order({{"Cafe", -40}, {"Home", -70}, {"Work", -55}, {"Phone", -62}}, {"Home", "Work", "Phone"}),
            (Names{"Work", "Phone", "Home"}));
}

TEST(WifiAutoJoinTest, UnknownNetworksAreNeverTried) {
  EXPECT_TRUE(order({{"Cafe", -40}, {"Neighbour", -50}}, {"Home"}).empty());
  EXPECT_TRUE(order({{"Cafe", -40}}, {}).empty());
}

TEST(WifiAutoJoinTest, NothingInTheScan) { EXPECT_TRUE(order({}, {"Home"}).empty()); }

TEST(WifiAutoJoinTest, OneEntryPerNameWithItsStrongestAccessPoint) {
  // Home has two access points: the stronger one puts it before Work.
  EXPECT_EQ(order({{"Home", -80}, {"Work", -60}, {"Home", -50}}, {"Home", "Work"}), (Names{"Home", "Work"}));
}

TEST(WifiAutoJoinTest, NamelessNetworksAreSkipped) {
  EXPECT_EQ(order({{"", -30}, {"Home", -70}}, {"Home", ""}), (Names{"Home"}));
}

TEST(WifiAutoJoinTest, EqualStrengthKeepsScanOrder) {
  EXPECT_EQ(order({{"B", -60}, {"A", -60}}, {"A", "B"}), (Names{"B", "A"}));
}

TEST(WifiAutoJoinTest, SavedHiddenNetworkIsTriedLast) {
  EXPECT_EQ(order({{"Home", -70}}, {"Home", "Secret"}, "Secret"), (Names{"Home", "Secret"}));
  EXPECT_EQ(order({}, {"Secret"}, "Secret"), (Names{"Secret"}));
}

TEST(WifiAutoJoinTest, HiddenNetworkOnlyWhenStillSavedAndNotListedTwice) {
  EXPECT_TRUE(order({}, {"Home"}, "Forgotten").empty());
  EXPECT_EQ(order({{"Secret", -50}}, {"Secret"}, "Secret"), (Names{"Secret"}));
}

TEST(WifiAutoJoinTest, JoinsTheFirstNetworkNotTriedYet) {
  const Names inRange{"Work", "Home"};
  auto next = wifijoin::nextStep(false, 1, inRange, {});
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  EXPECT_EQ(next.ssid, "Work");

  next = wifijoin::nextStep(false, 1, inRange, {"Work"});
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  EXPECT_EQ(next.ssid, "Home");
}

TEST(WifiAutoJoinTest, NoSavedNetworkInRangeShowsTheListNotAKeyboard) {
  const auto next = wifijoin::nextStep(false, 1, {}, {});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::NoSavedInRange);
  EXPECT_TRUE(next.ssid.empty());
}

TEST(WifiAutoJoinTest, EverySavedNetworkTriedShowsTheListWithThatReason) {
  const auto next = wifijoin::nextStep(false, 1, {"Work", "Home"}, {"Home", "Work"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::SavedFailed);
}

TEST(WifiAutoJoinTest, AFailedScanIsRepeatedOnceThenShown) {
  EXPECT_EQ(wifijoin::nextStep(true, 1, {}, {}).step, wifijoin::Step::Rescan);
  const auto next = wifijoin::nextStep(true, wifijoin::MAX_SCAN_TRIES, {}, {});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::ScanFailed);
}

TEST(WifiAutoJoinTest, AFailedScanNeverJoinsFromAnOlderList) {
  const auto next = wifijoin::nextStep(true, wifijoin::MAX_SCAN_TRIES, {"Home"}, {});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_TRUE(next.ssid.empty());
}
