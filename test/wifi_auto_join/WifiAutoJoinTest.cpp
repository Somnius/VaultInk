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

TEST(WifiAutoJoinTest, LastConnectedNetworkNotInTheScanGetsOneTryAtTheEnd) {
  // A network that hides its name is never listed; an old wifi.json does not say which one does.
  EXPECT_EQ(order({{"Home", -70}}, {"Home", "Secret"}, "Secret"), (Names{"Home", "Secret"}));
  EXPECT_EQ(order({{"Cafe", -40}}, {"Secret"}, "Secret"), (Names{"Secret"}));
}

TEST(WifiAutoJoinTest, LastConnectedOnlyWhenStillSavedAndNotListedTwice) {
  EXPECT_TRUE(order({}, {"Home"}, "Forgotten").empty());
  EXPECT_EQ(order({{"Secret", -50}, {"Home", -40}}, {"Secret", "Home"}, "Secret"), (Names{"Home", "Secret"}));
}

TEST(WifiAutoJoinTest, BlindOrderStartsWithTheLastConnected) {
  EXPECT_EQ(wifijoin::blindOrder({"A", "B", "Home"}, "Home"), (Names{"Home", "A", "B"}));
  EXPECT_EQ(wifijoin::blindOrder({"A", "B"}, ""), (Names{"A", "B"}));
  EXPECT_EQ(wifijoin::blindOrder({"A", "B"}, "Forgotten"), (Names{"A", "B"}));
  EXPECT_TRUE(wifijoin::blindOrder({}, "Home").empty());
}

TEST(WifiAutoJoinTest, BlindOrderIsCapped) {
  const Names order = wifijoin::blindOrder({"A", "B", "C", "D", "Home"}, "Home");
  EXPECT_EQ(order.size(), wifijoin::MAX_BLIND_TRIES);
  EXPECT_EQ(order, (Names{"Home", "A", "B"}));
}

TEST(WifiAutoJoinTest, JoinsTheFirstNetworkNotTriedYet) {
  const Names inRange{"Work", "Home"};
  auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, {});
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  EXPECT_EQ(next.ssid, "Work");

  next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, {"Work"});
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  EXPECT_EQ(next.ssid, "Home");
}

TEST(WifiAutoJoinTest, NoSavedNetworkInRangeShowsTheListNotAKeyboard) {
  const auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, {}, {});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::NoSavedInRange);
  EXPECT_TRUE(next.ssid.empty());
}

TEST(WifiAutoJoinTest, EverySavedNetworkTriedShowsTheListWithThatReason) {
  const auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, {"Work", "Home"}, {"Home", "Work"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::SavedFailed);
}

TEST(WifiAutoJoinTest, AScanThatFailedOrFoundNothingIsRepeatedOnce) {
  EXPECT_EQ(wifijoin::nextStep(wifijoin::Scan::Failed, 1, {"Home"}, {}).step, wifijoin::Step::Rescan);
  EXPECT_EQ(wifijoin::nextStep(wifijoin::Scan::Empty, 1, {"Home"}, {}).step, wifijoin::Step::Rescan);
  EXPECT_NE(wifijoin::nextStep(wifijoin::Scan::Empty, wifijoin::MAX_SCAN_TRIES, {"Home"}, {}).step,
            wifijoin::Step::Rescan);
}

TEST(WifiAutoJoinTest, AfterTwoUselessScansSavedNetworksAreTriedUnseen) {
  // The reader's scan listed nothing with the home network in range: it is tried anyway.
  const Names blind = wifijoin::blindOrder({"Work", "Home"}, "Home");
  for (const auto scan : {wifijoin::Scan::Empty, wifijoin::Scan::Failed}) {
    auto next = wifijoin::nextStep(scan, wifijoin::MAX_SCAN_TRIES, blind, {});
    EXPECT_EQ(next.step, wifijoin::Step::Join);
    EXPECT_EQ(next.ssid, "Home");
    next = wifijoin::nextStep(scan, wifijoin::MAX_SCAN_TRIES, blind, {"Home"});
    EXPECT_EQ(next.step, wifijoin::Step::Join);
    EXPECT_EQ(next.ssid, "Work");
  }
}

TEST(WifiAutoJoinTest, UnseenTriesOverShowTheListWithTheScansReason) {
  auto next = wifijoin::nextStep(wifijoin::Scan::Failed, wifijoin::MAX_SCAN_TRIES, {"Home"}, {"Home"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::ScanFailed);
  next = wifijoin::nextStep(wifijoin::Scan::Empty, wifijoin::MAX_SCAN_TRIES, {"Home"}, {"Home"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::NoSavedInRange);
}
