// S3 build: a text-entry screen (setTextEntry) with keyboard typing on starts the radio and types the keyboard's
// presses into textinput::global(); with the option off it is just another screen, and in the book nothing changes.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "Fakes.h"
#include "Runtime.h"
#include "TextInput.h"

namespace {

using bleturner::Action;
using bleturner::Config;
using bleturner::Scene;
using bleturner::Where;
using fake::host;
using fake::radio;
using textinput::Event;
using textinput::Special;

class TextEntryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    textinput::global().clear();
    config.enabled = 1;
    config.keyboardTyping = 1;
    bleturner::begin(fake::hostFns(), config);
    radio().queueStarts = true;
    host().heap = {124752, 81908};
    radio().changeHeapOnBegin = true;
    radio().heapAfterBegin = {78244, 36852};
  }
  void TearDown() override { bleturner::setTextEntry(false); }
  // What the host reports while the keyboard screen is in front: Elsewhere.
  static Scene elsewhere(const uint32_t visit = 7) {
    Scene s{};
    s.where = Where::Elsewhere;
    s.visit = visit;
    return s;
  }
  bool pass(const Scene& s) {
    const bool acted = bleturner::tick(s);
    if (radio().startQueued) radio().runStart();
    return acted;
  }
  void upAndLinked() {
    bleturner::setTextEntry(true);
    pass(elsewhere());
    ASSERT_TRUE(radio().running);
    radio().connected = true;
    radio().addr = "11:22:33:44:55:66";
  }
  static std::vector<Event> typed() {
    std::vector<Event> out;
    Event e;
    while (textinput::global().pop(e)) out.push_back(e);
    return out;
  }
  Config config;
};

TEST_F(TextEntryTest, StartsOnTheTextScreenOnlyWithTypingOn) {
  pass(elsewhere());
  EXPECT_EQ(radio().creates, 0u) << "Elsewhere, as before";
  bleturner::setTextEntry(true);
  EXPECT_TRUE(bleturner::textEntry());
  pass(elsewhere());
  EXPECT_EQ(radio().creates, 1u);
  EXPECT_TRUE(radio().running);
  EXPECT_EQ(bleturner::why(), bleturner::Why::Ok);
  for (int i = 0; i < 5; ++i) pass(elsewhere());
  EXPECT_EQ(radio().creates, 1u) << "once per visit";
}

TEST_F(TextEntryTest, TypingOffLeavesTheTextScreenAsItWas) {
  config.keyboardTyping = 0;
  bleturner::setTextEntry(true);
  pass(elsewhere());
  EXPECT_EQ(radio().creates, 0u);
  EXPECT_FALSE(radio().running);
  EXPECT_EQ(bleturner::why(), bleturner::Why::NotReading);
}

TEST_F(TextEntryTest, NeedsTheBluetoothSwitchAndABond) {
  bleturner::setTextEntry(true);
  config.enabled = 0;
  pass(elsewhere(1));
  EXPECT_EQ(radio().creates, 0u);
  config.enabled = 1;
  radio().bonds.clear();
  pass(elsewhere(2));
  EXPECT_EQ(radio().creates, 0u);
  EXPECT_EQ(bleturner::why(), bleturner::Why::NoBond);
  radio().bonds = {"aa:bb:cc:dd:ee:ff"};  // a keyboard, not the chosen remote (none chosen)
  EXPECT_STREQ(config.peerAddr, "");
  pass(elsewhere(3));
  EXPECT_EQ(radio().creates, 1u);
}

TEST_F(TextEntryTest, HeapTooTightIsRefusedWithoutARestart) {
  host().heap = {124752, 60000};
  bleturner::setTextEntry(true);
  for (uint32_t visit = 1; visit <= 6; ++visit) pass(elsewhere(visit));
  EXPECT_EQ(radio().beginCalls, 0u);
  EXPECT_FALSE(radio().running);
  EXPECT_EQ(host().restarts, 0u);
}

TEST_F(TextEntryTest, KeyPressesAreTypedNotTurnedIntoPages) {
  upAndLinked();
  // h, Shift+i, Right (a default next-page key), Enter (another), Backspace (a default previous-page key).
  radio().keys = {{0x0B, 0, true},
                  {0x0B, 0, false},
                  {0x0C, textinput::kModShift, true},
                  {bleturner::kUsageRight, 0, true},
                  {bleturner::kUsageEnter, 0, true},
                  {bleturner::kUsageBackspace, 0, true},
                  {0x2A, 0, false}};
  EXPECT_FALSE(pass(elsewhere())) << "typing is not a remote acting on the book";
  EXPECT_TRUE(host().delivered.empty()) << "nothing turned a page";
  const auto events = typed();
  ASSERT_EQ(events.size(), 5u) << "releases carry nothing";
  EXPECT_EQ(events[0].codepoint, 'h');
  EXPECT_EQ(events[1].codepoint, 'I');
  EXPECT_EQ(events[2].special, Special::Right);
  EXPECT_EQ(events[3].special, Special::Enter);
  EXPECT_EQ(events[4].special, Special::Backspace);
  EXPECT_TRUE(radio().keys.empty()) << "the SDK ring is drained";
}

TEST_F(TextEntryTest, ALearnedPageKeyTypesTooOnTheTextScreen) {
  config.nextKeyUsage = 0x0B;  // the user learned "h" as next page on a remote
  upAndLinked();
  radio().keys = {{0x0B, 0, true}};
  pass(elsewhere());
  EXPECT_TRUE(host().delivered.empty());
  const auto events = typed();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].codepoint, 'h');
}

TEST_F(TextEntryTest, RawEdgesAreDrainedNotRouted) {
  upAndLinked();
  bleturner::port::RawEdge edge;
  edge.reportId = 1;
  edge.byteIndex = 2;
  edge.value = 0x4F;
  edge.pressed = true;
  edge.keycode = bleturner::kUsageRight;
  radio().raw = {edge};
  pass(elsewhere());
  EXPECT_TRUE(host().delivered.empty());
  EXPECT_TRUE(radio().raw.empty());
  EXPECT_TRUE(typed().empty());
}

TEST_F(TextEntryTest, LayoutFollowsTheHost) {
  EXPECT_EQ(bleturner::keyboardLayout(), bleturner::KeyboardLayout::Us);
  bleturner::setKeyboardLayout(bleturner::KeyboardLayout::Greek);
  EXPECT_EQ(bleturner::keyboardLayout(), bleturner::KeyboardLayout::Greek);
  upAndLinked();
  radio().keys = {{0x04, 0, true}};
  pass(elsewhere());
  const auto events = typed();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].codepoint, 0x3B1u) << "alpha";
  bleturner::setKeyboardLayout(bleturner::KeyboardLayout::Us);
  radio().keys = {{0x04, 0, true}};
  pass(elsewhere());
  EXPECT_EQ(typed()[0].codepoint, 'a');
}

TEST_F(TextEntryTest, TheReaderPathIsUntouched) {
  // The text screen left: the book in front with the same radio up turns pages from the same keys.
  upAndLinked();
  bleturner::setTextEntry(false);
  std::strcpy(config.peerAddr, "11:22:33:44:55:66");
  pass(fake::reading(9));
  radio().keys = {{bleturner::kUsageRight, 0, true}, {0x0B, 0, true}};
  EXPECT_TRUE(pass(fake::reading(9)));
  EXPECT_EQ(host().delivered, (std::vector<Action>{Action::NextPage}));
  EXPECT_TRUE(typed().empty()) << "nothing is typed from the book";
}

TEST_F(TextEntryTest, TextScreenWhileReportedAsReaderStaysReader) {
  // setTextEntry only promotes Elsewhere: a host that still reports the book keeps the book's path.
  bleturner::setTextEntry(true);
  std::strcpy(config.peerAddr, "11:22:33:44:55:66");
  pass(fake::reading(3));
  ASSERT_TRUE(radio().running);
  radio().connected = true;
  radio().addr = "11:22:33:44:55:66";
  pass(fake::reading(3));
  radio().keys = {{bleturner::kUsageRight, 0, true}};
  EXPECT_TRUE(pass(fake::reading(3)));
  EXPECT_EQ(host().delivered, (std::vector<Action>{Action::NextPage}));
  EXPECT_TRUE(typed().empty());
}

TEST_F(TextEntryTest, IdleWithNoKeyboardStopsAfterThirtySeconds) {
  bleturner::setTextEntry(true);
  pass(elsewhere());
  ASSERT_TRUE(radio().running);
  radio().now += bleturner::kIdleOffMs + 1;
  pass(elsewhere());
  radio().now += bleturner::kIdleOffMs + 1;
  pass(elsewhere());
  EXPECT_FALSE(radio().running);
  EXPECT_TRUE(bleturner::status().idleStopped);
}

TEST_F(TextEntryTest, LeavingTheScreenStopsOnTheScreenChange) {
  upAndLinked();
  EXPECT_TRUE(bleturner::beforeScreenChange(0, false));
  EXPECT_FALSE(radio().running);
}

}  // namespace
