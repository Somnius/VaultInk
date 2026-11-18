#include <gtest/gtest.h>

#include <algorithm>
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

TEST(WifiAutoJoinTest, ANetworkWhoseTryTimedOutIsTriedOnceMore) {
  // 2026-11-18 on an X4 Pro: the one 7 s try of the home network timed out, the list came up with
  // "Saved network didn't join" and the home network first at full signal.
  auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, {"Home"}, {"Home"}, {"Home"});
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  EXPECT_EQ(next.ssid, "Home");

  next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, {"Home"}, {"Home", "Home"}, {"Home"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::SavedFailed);
}

TEST(WifiAutoJoinTest, ARefusedOrMissingNetworkIsNotTriedAgain) {
  // "Away" ended with "connect failed" or "no such network", so the screen did not put it in
  // timedOut: a second try would only cost time.
  const auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, {"Away", "Home"}, {"Away", "Home"}, {});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::SavedFailed);
}

TEST(WifiAutoJoinTest, SecondTriesComeAfterEveryFirstTry) {
  const Names inRange{"Work", "Home"};
  auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, {"Work"}, {"Work"});
  EXPECT_EQ(next.ssid, "Home");  // Home's first try before Work's second
  next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, {"Work", "Home"}, {"Work"});
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  EXPECT_EQ(next.ssid, "Work");
  next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, {"Work", "Home", "Work"}, {"Work"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
}

TEST(WifiAutoJoinTest, ATimedOutUnseenNetworkIsTriedOnceMoreToo) {
  const Names blind = wifijoin::blindOrder({"Home"}, "Home");
  auto next = wifijoin::nextStep(wifijoin::Scan::Empty, wifijoin::MAX_SCAN_TRIES, blind, {"Home"}, {"Home"});
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  next = wifijoin::nextStep(wifijoin::Scan::Empty, wifijoin::MAX_SCAN_TRIES, blind, {"Home", "Home"}, {"Home"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::NoSavedInRange);
}

TEST(WifiAutoJoinTest, ATimedOutNameThatIsNoCandidateIsIgnored) {
  const auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, {"Home"}, {"Home"}, {"Other"});
  EXPECT_EQ(next.step, wifijoin::Step::List);
}

TEST(WifiAutoJoinTest, AWrongPasswordThatSatOutTheSevenSecondsIsNotTriedAgain) {
  // A wrong password can stay at "disconnected" until the limit: the radio's reason tells.
  for (const uint8_t reason :
       {wifijoin::REASON_4WAY_HANDSHAKE_TIMEOUT, wifijoin::REASON_HANDSHAKE_TIMEOUT, wifijoin::REASON_AUTH_FAIL,
        wifijoin::REASON_MIC_FAILURE, wifijoin::REASON_802_1X_AUTH_FAILED, wifijoin::REASON_BAD_CIPHER_OR_AKM,
        wifijoin::REASON_NO_AP_FOUND, uint8_t{18}, uint8_t{22}}) {
    EXPECT_FALSE(wifijoin::retryAfterTimeout(reason)) << static_cast<int>(reason);
  }
  // No reason, or one that also means the access point did not answer in time.
  for (const uint8_t reason : {uint8_t{0}, uint8_t{2}, uint8_t{39}, uint8_t{200}, uint8_t{203}}) {
    EXPECT_TRUE(wifijoin::retryAfterTimeout(reason)) << static_cast<int>(reason);
  }
}

TEST(WifiAutoJoinTest, TheReadersOwnLeaveIsNotTheReasonOfATry) {
  // attemptConnection disconnects first; that event can arrive after the reason was cleared.
  EXPECT_EQ(wifijoin::mergeReason(0, wifijoin::REASON_ASSOC_LEAVE), 0);
  EXPECT_EQ(wifijoin::mergeReason(wifijoin::REASON_AUTH_FAIL, wifijoin::REASON_ASSOC_LEAVE),
            wifijoin::REASON_AUTH_FAIL);
  EXPECT_EQ(wifijoin::mergeReason(0, 200), 200);
  EXPECT_EQ(wifijoin::mergeReason(200, 2), 2);
  // A password reason stays, whatever the radio says while it tries again by itself.
  EXPECT_EQ(wifijoin::mergeReason(wifijoin::REASON_4WAY_HANDSHAKE_TIMEOUT, 200),
            wifijoin::REASON_4WAY_HANDSHAKE_TIMEOUT);
  EXPECT_EQ(wifijoin::mergeReason(2, wifijoin::REASON_AUTH_FAIL), wifijoin::REASON_AUTH_FAIL);
}

TEST(WifiAutoJoinTest, SecondTriesStopAtTheBudget) {
  constexpr uint32_t TRY_MS = 7000;  // AUTO_CONNECTION_TIMEOUT_MS
  // One network: scan 3 s, first try 7 s, so the second starts at 10 s and ends by 17 s.
  EXPECT_TRUE(wifijoin::secondTryFits(10000, TRY_MS));
  // The last moment a second try may start: it ends at the 30 s.
  EXPECT_TRUE(wifijoin::secondTryFits(wifijoin::SECOND_TRY_BUDGET_MS - TRY_MS, TRY_MS));
  EXPECT_FALSE(wifijoin::secondTryFits(wifijoin::SECOND_TRY_BUDGET_MS - TRY_MS + 1, TRY_MS));
  // Four networks that all timed out (28 s of first tries): none is tried again.
  EXPECT_FALSE(wifijoin::secondTryFits(3000 + 4 * TRY_MS, TRY_MS));
  EXPECT_FALSE(wifijoin::secondTryFits(wifijoin::SECOND_TRY_BUDGET_MS + 1, 0));

  // Without the budget nextStep ends the join where it would have tried again.
  const Names inRange{"Work", "Home"};
  auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, {"Work", "Home"}, {"Work", "Home"}, false);
  EXPECT_EQ(next.step, wifijoin::Step::List);
  EXPECT_EQ(next.notice, wifijoin::Notice::SavedFailed);
  // First tries are not cut by it.
  next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, {"Work"}, {"Work"}, false);
  EXPECT_EQ(next.step, wifijoin::Step::Join);
  EXPECT_EQ(next.ssid, "Home");
}

TEST(WifiAutoJoinTest, WorstCaseOfSecondTriesIsBounded) {
  // Walk the join as the screen does, every try timing out after 7 s, for 1 to 5 saved networks.
  constexpr uint32_t TRY_MS = 7000;
  constexpr uint32_t SCAN_MS = 3000;
  for (int count = 1; count <= 5; ++count) {
    Names inRange;
    for (int i = 0; i < count; ++i) inRange.push_back("Net" + std::to_string(i));
    Names attempted;
    Names timedOut;
    uint32_t elapsed = SCAN_MS;
    for (;;) {
      const auto next = wifijoin::nextStep(wifijoin::Scan::Ok, 1, inRange, attempted, timedOut,
                                           wifijoin::secondTryFits(elapsed, TRY_MS));
      if (next.step != wifijoin::Step::Join) break;
      const bool second = std::find(attempted.begin(), attempted.end(), next.ssid) != attempted.end();
      if (second) {
        EXPECT_LE(elapsed + TRY_MS, wifijoin::SECOND_TRY_BUDGET_MS);
      }
      attempted.push_back(next.ssid);
      if (!second) timedOut.push_back(next.ssid);
      elapsed += TRY_MS;
      ASSERT_LE(attempted.size(), static_cast<size_t>(count) * wifijoin::MAX_JOIN_TRIES);
    }
    // Every network had its first try; the total is the first tries, or 30 s when second tries ran.
    EXPECT_GE(attempted.size(), static_cast<size_t>(count));
    EXPECT_LE(elapsed, std::max<uint32_t>(wifijoin::SECOND_TRY_BUDGET_MS, SCAN_MS + count * TRY_MS));
    if (count == 1) {
      EXPECT_EQ(elapsed, SCAN_MS + 2 * TRY_MS);
    }
    if (count >= 4) {
      EXPECT_EQ(attempted.size(), static_cast<size_t>(count));
    }
  }
}
