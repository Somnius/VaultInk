#include <gtest/gtest.h>

#include <string>

#include "SshInput.h"
#include "TextInput.h"

using textinput::Event;
using textinput::Special;

namespace {
std::string special(const Special s, const bool app = false) { return sshinput::bytesFor(Event{0, s}, false, app); }
std::string typed(const uint32_t cp, const bool ctrl = false) {
  return sshinput::bytesFor(Event{cp, Special::None}, ctrl, false);
}
}  // namespace

TEST(SshInput, SpecialKeys) {
  EXPECT_EQ(special(Special::Enter), "\r");
  EXPECT_EQ(special(Special::Backspace), "\x7f");
  EXPECT_EQ(special(Special::Tab), "\t");
  EXPECT_EQ(special(Special::Escape), "\x1b");
  EXPECT_EQ(special(Special::Delete), "\x1b[3~");
  EXPECT_EQ(special(Special::Up), "\x1b[A");
  EXPECT_EQ(special(Special::Down), "\x1b[B");
  EXPECT_EQ(special(Special::Right), "\x1b[C");
  EXPECT_EQ(special(Special::Left), "\x1b[D");
  EXPECT_EQ(special(Special::Home), "\x1b[H");
  EXPECT_EQ(special(Special::End), "\x1b[F");
  // The screen looks back through its lines with these: nothing goes to the server.
  EXPECT_EQ(special(Special::PageUp), "");
  EXPECT_EQ(special(Special::PageDown), "");
}

TEST(SshInput, ApplicationCursorKeys) {
  EXPECT_EQ(special(Special::Up, true), "\x1bOA");
  EXPECT_EQ(special(Special::Left, true), "\x1bOD");
  EXPECT_EQ(special(Special::Enter, true), "\r");
}

TEST(SshInput, CharactersAsUtf8) {
  EXPECT_EQ(typed('a'), "a");
  EXPECT_EQ(typed(0x3B1), "\xCE\xB1");
  EXPECT_EQ(typed(0x20AC), "\xE2\x82\xAC");
  EXPECT_EQ(typed(0x1F600), "\xF0\x9F\x98\x80");
  EXPECT_EQ(typed(0), "");
  EXPECT_EQ(typed(0xD800), "");
  EXPECT_EQ(typed(0x110000), "");
  // A control character a Bluetooth keyboard typed with Ctrl held (terminal keys) goes through.
  EXPECT_EQ(typed(3), "\x03");
}

TEST(SshInput, CtrlLatch) {
  EXPECT_EQ(typed('c', true), "\x03");
  EXPECT_EQ(typed('C', true), "\x03");
  EXPECT_EQ(typed('d', true), "\x04");
  EXPECT_EQ(typed('z', true), "\x1a");
  EXPECT_EQ(typed('[', true), "\x1b");
  EXPECT_EQ(typed('\\', true), "\x1c");
  EXPECT_EQ(typed('?', true), "\x7f");
  EXPECT_EQ(typed('@', true), std::string(1, '\0'));
  EXPECT_EQ(typed(' ', true), std::string(1, '\0'));
  // No control character for these: sent as typed.
  EXPECT_EQ(typed('1', true), "1");
  EXPECT_EQ(typed(0x3B1, true), "\xCE\xB1");
}

TEST(SshInput, LineFromTheOnScreenKeyboard) {
  EXPECT_EQ(sshinput::bytesForLine("ls -la", false, true), "ls -la\r");
  EXPECT_EQ(sshinput::bytesForLine("ls -la", false, false), "ls -la");
  EXPECT_EQ(sshinput::bytesForLine("", false, true), "\r");
  // Ctrl applies to the first character only.
  EXPECT_EQ(sshinput::bytesForLine("c", true, false), "\x03");
  EXPECT_EQ(sshinput::bytesForLine("cd", true, false),
            "\x03"
            "d");
  EXPECT_EQ(sshinput::bytesForLine("\xCE\xB1"
                                   "b",
                                   true, false),
            "\xCE\xB1"
            "b");
  EXPECT_EQ(sshinput::bytesForLine("", true, false), "");
}

// The Bluetooth keyboard side: terminal keys are off unless asked for, so text fields are as before.
TEST(SshInput, HidTerminalKeysAreOptIn) {
  textinput::HidTranslator plain;
  Event out[2];
  EXPECT_EQ(plain.translate(0x06, 0x01, out, 2), 0u);  // Ctrl-C
  EXPECT_EQ(plain.translate(0x2B, 0, out, 2), 0u);     // Tab
  EXPECT_EQ(plain.translate(0x52, 0, out, 2), 0u);     // Up
  EXPECT_FALSE(plain.terminalKeys());

  textinput::HidTranslator term;
  term.setTerminalKeys(true);
  ASSERT_EQ(term.translate(0x06, 0x01, out, 2), 1u);  // left Ctrl + c
  EXPECT_EQ(out[0].codepoint, 3u);
  ASSERT_EQ(term.translate(0x07, 0x10, out, 2), 1u);  // right Ctrl + d
  EXPECT_EQ(out[0].codepoint, 4u);
  ASSERT_EQ(term.translate(0x1D, 0x01 | 0x02, out, 2), 1u);  // Ctrl + Shift + z
  EXPECT_EQ(out[0].codepoint, 26u);
  ASSERT_EQ(term.translate(0x2F, 0x01, out, 2), 1u);  // Ctrl-[
  EXPECT_EQ(out[0].codepoint, 0x1Bu);
  EXPECT_EQ(term.translate(0x06, 0x01 | 0x04, out, 2), 0u);  // Ctrl + Alt + c: nothing
  EXPECT_EQ(term.translate(0x06, 0x08, out, 2), 0u);         // Gui + c: nothing
  EXPECT_EQ(term.translate(0x1E, 0x01, out, 2), 0u);         // Ctrl-1: nothing
  ASSERT_EQ(term.translate(0x2B, 0, out, 2), 1u);
  EXPECT_EQ(out[0].special, Special::Tab);
  ASSERT_EQ(term.translate(0x52, 0, out, 2), 1u);
  EXPECT_EQ(out[0].special, Special::Up);
  ASSERT_EQ(term.translate(0x51, 0, out, 2), 1u);
  EXPECT_EQ(out[0].special, Special::Down);
  ASSERT_EQ(term.translate(0x4B, 0, out, 2), 1u);
  EXPECT_EQ(out[0].special, Special::PageUp);
  ASSERT_EQ(term.translate(0x4E, 0, out, 2), 1u);
  EXPECT_EQ(out[0].special, Special::PageDown);
  ASSERT_EQ(term.translate(0x04, 0, out, 2), 1u);  // a plain letter is still a letter
  EXPECT_EQ(out[0].codepoint, static_cast<uint32_t>('a'));
}

TEST(SshInput, ATextLineIgnoresTerminalEvents) {
  std::string text = "abc";
  size_t cursor = 3;
  for (const Special s : {Special::Tab, Special::Up, Special::Down, Special::PageUp, Special::PageDown}) {
    EXPECT_EQ(textinput::apply(text, cursor, 100, Event{0, s}), textinput::Applied::Nothing);
  }
  EXPECT_EQ(textinput::apply(text, cursor, 100, Event{3, Special::None}), textinput::Applied::Nothing);
  EXPECT_EQ(text, "abc");
  EXPECT_EQ(cursor, 3u);
}
