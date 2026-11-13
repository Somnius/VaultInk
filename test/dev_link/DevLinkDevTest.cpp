// The developer build's commands (VAULTINK_DEVLINK): the name table, the arguments, base64, the
// input queue and the SD path rule. DevLinkTest.cpp covers the table every other build has.
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "DevLink.h"
#include "DevLinkArgs.h"
#include "DevLinkInput.h"
#include "util/DevLinkPaths.h"

namespace {

using devlink::Command;
using devlink::InputInjector;
using devlink::KeyName;
using devlinkpaths::Access;
using devlinkpaths::isAllowed;

devlink::Parsed parse(const std::string& line) { return devlink::parse(line.data(), line.size()); }

KeyName keyArgs(const std::string& text, devlink::KeyArgs& out) {
  return devlink::parseKeyArgs(text.data(), text.size(), out);
}
bool tapArgs(const std::string& text, devlink::TouchArgs& out) {
  return devlink::parseTapArgs(text.data(), text.size(), out);
}
bool swipeArgs(const std::string& text, devlink::TouchArgs& out) {
  return devlink::parseSwipeArgs(text.data(), text.size(), out);
}
// The result points into the text, so these take a literal and not a temporary string.
bool getArgs(const char* text, devlink::FileArgs& out) { return devlink::parseGetArgs(text, strlen(text), out); }
bool putArgs(const char* text, devlink::FileArgs& out) { return devlink::parsePutArgs(text, strlen(text), out); }
std::string pathOf(const devlink::FileArgs& args) { return std::string(args.path, args.pathLength); }

std::string encode(const std::string& bytes) {
  char out[64];
  const size_t n =
      devlink::base64Encode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), out, sizeof(out));
  return std::string(out, n);
}
bool decode(const std::string& text, std::string& bytes, const size_t capacity = 64) {
  std::vector<uint8_t> out(capacity + 1);
  size_t n = 0;
  if (!devlink::base64Decode(text.data(), text.size(), out.data(), capacity, n)) return false;
  bytes.assign(reinterpret_cast<const char*>(out.data()), n);
  return true;
}

// The input library's debounce, as InputManager::update() does it: a state is taken once it has
// been read unchanged for more than 5 ms. Counts the presses and releases that get through.
struct Debounce {
  uint8_t last = 0;
  uint8_t current = 0;
  uint32_t lastChange = 0;
  int presses = 0;
  int releases = 0;
  uint32_t pressedAt = 0;
  uint32_t heldMs = 0;
  void update(const uint8_t state, const uint32_t now) {
    if (state != last) {
      lastChange = now;
      last = state;
    }
    if (now - lastChange > 5 && state != current) {
      if (state & ~current) {
        ++presses;
        pressedAt = now;
      }
      if (current & ~state) {
        ++releases;
        heldMs = now - pressedAt;
      }
      current = state;
    }
  }
};

// The panel pixel a 0..1 position stands for, as GfxRenderer::tapToLogical cuts it.
int pixel(const float n, const int extent) { return static_cast<int>(n * static_cast<float>(extent)); }

}  // namespace

// ---- the name table ----

TEST(DevParse, KnowsTheDeveloperCommands) {
  EXPECT_EQ(parse("CMD:KEY BACK").command, Command::Key);
  EXPECT_EQ(parse("CMD:TAP 10 20").command, Command::Tap);
  EXPECT_EQ(parse("CMD:SWIPE 1 2 3 4").command, Command::Swipe);
  EXPECT_EQ(parse("CMD:FLASH /firmware.bin").command, Command::Flash);
  EXPECT_EQ(parse("CMD:LS /debug").command, Command::Ls);
  EXPECT_EQ(parse("CMD:GET /debug/a.txt 0 100").command, Command::Get);
  EXPECT_EQ(parse("CMD:PUT /debug/a.txt 0 YQ==").command, Command::Put);
  EXPECT_EQ(parse("CMD:key BACK").command, Command::Unknown);
  EXPECT_EQ(parse("CMD:KEYS").command, Command::Unknown);
  EXPECT_EQ(parse("CMD:L").command, Command::Unknown);
}

TEST(DevParse, EveryListedCommandParses) {
  const std::string list = devlink::commandList();
  for (const char* name : {"KEY", "TAP", "SWIPE", "FLASH", "LS", "GET", "PUT"}) {
    EXPECT_NE((" " + list + " ").find(std::string(" ") + name + " "), std::string::npos) << name;
  }
  size_t start = 0;
  while (start < list.size()) {
    size_t end = list.find(' ', start);
    if (end == std::string::npos) end = list.size();
    EXPECT_NE(parse("CMD:" + list.substr(start, end - start)).command, Command::Unknown);
    start = end + 1;
  }
}

TEST(DevParse, ThePathKeepsItsBlanksInTheArguments) {
  const std::string line = "CMD:LS  /screenshots/My Book  ";
  const auto parsed = parse(line);
  EXPECT_EQ(std::string(parsed.args, parsed.argsLength), "/screenshots/My Book");
}

// ---- KEY ----

TEST(KeyArgs, EveryNameAndItsButton) {
  const char* names[] = {"BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN"};
  for (uint8_t i = 0; i < 6; ++i) {
    devlink::KeyArgs key;
    ASSERT_EQ(keyArgs(names[i], key), KeyName::Ok) << names[i];
    EXPECT_EQ(key.button, i);
    EXPECT_EQ(key.holdMs, devlink::DEFAULT_KEY_HOLD_MS);
  }
}

TEST(KeyArgs, PowerIsRefusedByName) {
  devlink::KeyArgs key;
  EXPECT_EQ(keyArgs("POWER", key), KeyName::Power);
  EXPECT_EQ(keyArgs("POWER 100", key), KeyName::Power);
  uint8_t button = 9;
  EXPECT_EQ(devlink::parseKeyName("POWER", 5, button), KeyName::Power);
  EXPECT_EQ(button, 9);
}

TEST(KeyArgs, TheHoldTime) {
  devlink::KeyArgs key;
  EXPECT_EQ(keyArgs("CONFIRM 1500", key), KeyName::Ok);
  EXPECT_EQ(key.holdMs, 1500u);
  EXPECT_EQ(keyArgs("  UP   20 ", key), KeyName::Ok);
  EXPECT_EQ(key.holdMs, 20u);
  EXPECT_EQ(keyArgs("UP 5000", key), KeyName::Ok);
  EXPECT_EQ(keyArgs("UP 19", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("UP 5001", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("UP -5", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("UP 1.5", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("UP 100 100", key), KeyName::Unknown);
}

TEST(KeyArgs, WrongNames) {
  devlink::KeyArgs key;
  key.button = 3;
  EXPECT_EQ(keyArgs("", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("   ", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("back", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("BACKS", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("HOME", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("SLEEP", key), KeyName::Unknown);
  EXPECT_EQ(keyArgs("6", key), KeyName::Unknown);
  EXPECT_EQ(key.button, 3);  // a refused command leaves `out` alone
}

// ---- TAP and SWIPE ----

TEST(TouchArgs, Tap) {
  devlink::TouchArgs touch;
  ASSERT_TRUE(tapArgs("240 400", touch));
  EXPECT_EQ(touch.x1, 240u);
  EXPECT_EQ(touch.y1, 400u);
  EXPECT_EQ(touch.x2, 240u);
  EXPECT_EQ(touch.y2, 400u);
  EXPECT_EQ(touch.holdMs, devlink::DEFAULT_TAP_HOLD_MS);
  ASSERT_TRUE(tapArgs(" 0\t0  700 ", touch));
  EXPECT_EQ(touch.holdMs, 700u);
  EXPECT_FALSE(tapArgs("", touch));
  EXPECT_FALSE(tapArgs("240", touch));
  EXPECT_FALSE(tapArgs("240 400 80 1", touch));
  EXPECT_FALSE(tapArgs("-1 400", touch));
  EXPECT_FALSE(tapArgs("240,400", touch));
  EXPECT_FALSE(tapArgs("0.5 0.5", touch));
  EXPECT_FALSE(tapArgs("240 400 10", touch));
  EXPECT_FALSE(tapArgs("240 400 6000", touch));
  EXPECT_FALSE(tapArgs("70000 1", touch));
  EXPECT_FALSE(tapArgs("99999999999 1", touch));
}

TEST(TouchArgs, Swipe) {
  devlink::TouchArgs touch;
  ASSERT_TRUE(swipeArgs("400 300 80 300", touch));
  EXPECT_EQ(touch.x1, 400u);
  EXPECT_EQ(touch.y1, 300u);
  EXPECT_EQ(touch.x2, 80u);
  EXPECT_EQ(touch.y2, 300u);
  EXPECT_EQ(touch.holdMs, devlink::DEFAULT_SWIPE_MS);
  ASSERT_TRUE(swipeArgs("1 2 3 4 500", touch));
  EXPECT_EQ(touch.holdMs, 500u);
  EXPECT_FALSE(swipeArgs("1 2 3", touch));
  EXPECT_FALSE(swipeArgs("1 2 3 4 5 6", touch));
  EXPECT_FALSE(swipeArgs("1 2 3 x", touch));
  EXPECT_FALSE(swipeArgs("1 2 3 4 5", touch));  // 5 ms is under the shortest time
}

TEST(TouchArgs, CoordinatesAreHeldInsideTheScreen) {
  EXPECT_EQ(devlink::clampToExtent(0, 480), 0);
  EXPECT_EQ(devlink::clampToExtent(479, 480), 479);
  EXPECT_EQ(devlink::clampToExtent(480, 480), 479);
  EXPECT_EQ(devlink::clampToExtent(65535, 480), 479);
  EXPECT_EQ(devlink::clampToExtent(5, 0), 0);
}

// GfxRenderer::tapToLogical, line for line, to check the inverse against.
void tapToLogical(const int orientation, int phyX, int phyY, const int panelWidth, const int panelHeight, int& outX,
                  int& outY) {
  switch (orientation) {
    case 0:
      outX = panelHeight - 1 - phyY;
      outY = phyX;
      break;
    case 2:
      outX = phyY;
      outY = panelWidth - 1 - phyX;
      break;
    case 1:
      outX = panelWidth - 1 - phyX;
      outY = panelHeight - 1 - phyY;
      break;
    default:
      outX = phyX;
      outY = phyY;
      break;
  }
}

TEST(TouchArgs, ScreenToPanelIsTheInverseOfTheRenderersMapping) {
  const int panelWidth = 800;
  const int panelHeight = 480;
  for (int orientation = 0; orientation < 4; ++orientation) {
    const bool portrait = orientation == 0 || orientation == 2;
    const int screenWidth = portrait ? panelHeight : panelWidth;
    const int screenHeight = portrait ? panelWidth : panelHeight;
    for (const int lx : {0, 1, 17, screenWidth / 2, screenWidth - 1}) {
      for (const int ly : {0, 3, 250, screenHeight - 1}) {
        uint16_t px = 0;
        uint16_t py = 0;
        devlink::logicalToPanel(static_cast<uint8_t>(orientation), static_cast<uint16_t>(lx), static_cast<uint16_t>(ly),
                                panelWidth, panelHeight, px, py);
        ASSERT_LT(px, panelWidth);
        ASSERT_LT(py, panelHeight);
        int backX = -1;
        int backY = -1;
        tapToLogical(orientation, px, py, panelWidth, panelHeight, backX, backY);
        EXPECT_EQ(backX, lx) << "orientation " << orientation;
        EXPECT_EQ(backY, ly) << "orientation " << orientation;
      }
    }
  }
}

TEST(TouchArgs, ScreenToPanelNeverLeavesThePanel) {
  for (uint8_t orientation = 0; orientation < 6; ++orientation) {
    uint16_t px = 9999;
    uint16_t py = 9999;
    devlink::logicalToPanel(orientation, 65535, 65535, 800, 480, px, py);
    EXPECT_LT(px, 800);
    EXPECT_LT(py, 480);
  }
}

// ---- GET and PUT arguments ----

TEST(FileArgs, Get) {
  devlink::FileArgs args;
  ASSERT_TRUE(getArgs("/.vaultink/bench.csv 0 2880", args));
  EXPECT_EQ(pathOf(args), "/.vaultink/bench.csv");
  EXPECT_EQ(args.offset, 0u);
  EXPECT_EQ(args.length, 2880u);
  ASSERT_TRUE(getArgs("  /screenshots/My Book/My Book_p3.bmp   4096\t100  ", args));
  EXPECT_EQ(pathOf(args), "/screenshots/My Book/My Book_p3.bmp");
  EXPECT_EQ(args.offset, 4096u);
  EXPECT_EQ(args.length, 100u);
  EXPECT_FALSE(getArgs("", args));
  EXPECT_FALSE(getArgs("/debug/a.txt", args));
  EXPECT_FALSE(getArgs("/debug/a.txt 0", args));
  EXPECT_FALSE(getArgs("0 100", args));
  EXPECT_FALSE(getArgs("/debug/a.txt x 100", args));
  EXPECT_FALSE(getArgs("/debug/a.txt 0 -1", args));
  EXPECT_FALSE(getArgs("/debug/a.txt 0 99999999999", args));
}

TEST(FileArgs, Put) {
  devlink::FileArgs args;
  ASSERT_TRUE(putArgs("/debug/note.txt 12 aGVsbG8=", args));
  EXPECT_EQ(pathOf(args), "/debug/note.txt");
  EXPECT_EQ(args.offset, 12u);
  EXPECT_EQ(std::string(args.data, args.dataLength), "aGVsbG8=");
  ASSERT_TRUE(putArgs("/debug/a b.txt 0 YQ==", args));
  EXPECT_EQ(pathOf(args), "/debug/a b.txt");
  EXPECT_FALSE(putArgs("/debug/note.txt 0", args));
  EXPECT_FALSE(putArgs("/debug/note.txt", args));
  EXPECT_FALSE(putArgs("/debug/note.txt x YQ==", args));
  EXPECT_FALSE(putArgs("", args));
}

TEST(SplitArgs, CountsWordsAndSaysWhenThereAreTooMany) {
  devlink::Token tokens[2];
  EXPECT_EQ(devlink::splitArgs("", 0, tokens, 2), 0u);
  EXPECT_EQ(devlink::splitArgs("  ", 2, tokens, 2), 0u);
  EXPECT_EQ(devlink::splitArgs(" a  bc ", 7, tokens, 2), 2u);
  EXPECT_EQ(std::string(tokens[1].text, tokens[1].length), "bc");
  EXPECT_EQ(devlink::splitArgs("a b c", 5, tokens, 2), 3u);
}

// ---- base64 ----

TEST(Base64, KnownTexts) {
  EXPECT_EQ(encode(""), "");
  EXPECT_EQ(encode("f"), "Zg==");
  EXPECT_EQ(encode("fo"), "Zm8=");
  EXPECT_EQ(encode("foo"), "Zm9v");
  EXPECT_EQ(encode("foob"), "Zm9vYg==");
  EXPECT_EQ(encode("fooba"), "Zm9vYmE=");
  EXPECT_EQ(encode("foobar"), "Zm9vYmFy");
  EXPECT_EQ(encode(std::string("\xff\xfe\x00", 3)), "//4A");
  EXPECT_EQ(devlink::base64Length(0), 0u);
  EXPECT_EQ(devlink::base64Length(1), 4u);
  EXPECT_EQ(devlink::base64Length(540), 720u);
  EXPECT_EQ(devlink::base64Length(2880), 3840u);
}

TEST(Base64, EncoderRefusesABufferThatIsTooSmall) {
  char out[7];
  memset(out, '#', sizeof(out));
  EXPECT_EQ(devlink::base64Encode(reinterpret_cast<const uint8_t*>("foob"), 4, out, sizeof(out)), 0u);
  EXPECT_EQ(out[0], '#');
}

TEST(Base64, RoundTripOfEveryByte) {
  std::string bytes;
  for (int i = 0; i < 256; ++i) bytes.push_back(static_cast<char>(i));
  for (const size_t length : {size_t{1}, size_t{2}, size_t{3}, size_t{179}, size_t{180}, size_t{256}}) {
    const std::string part = bytes.substr(0, length);
    std::vector<char> text(devlink::base64Length(length));
    const size_t n =
        devlink::base64Encode(reinterpret_cast<const uint8_t*>(part.data()), length, text.data(), text.size());
    ASSERT_EQ(n, text.size());
    std::string back;
    ASSERT_TRUE(decode(std::string(text.data(), n), back, 256));
    EXPECT_EQ(back, part);
  }
}

TEST(Base64, DecoderTakesOnlyWholePaddedGroups) {
  std::string bytes;
  ASSERT_TRUE(decode("Zm9vYmE=", bytes));
  EXPECT_EQ(bytes, "fooba");
  EXPECT_FALSE(decode("", bytes));
  EXPECT_FALSE(decode("Zg", bytes));        // no padding
  EXPECT_FALSE(decode("Zg=", bytes));       // wrong length
  EXPECT_FALSE(decode("Zg==Zg==", bytes));  // padding in the middle
  EXPECT_FALSE(decode("Z===", bytes));
  EXPECT_FALSE(decode("====", bytes));
  EXPECT_FALSE(decode("Zm9v Zm9v", bytes));
  EXPECT_FALSE(decode("Zm9-", bytes));  // the URL alphabet is not taken
  EXPECT_FALSE(decode("Zm9_", bytes));
  EXPECT_FALSE(decode("Zm\n=", bytes));
  EXPECT_FALSE(decode("Zh==", bytes));  // bits left over under the padding
  EXPECT_FALSE(decode("Zm9=", bytes));
}

TEST(Base64, DecoderStopsAtTheCapacity) {
  std::string bytes;
  EXPECT_TRUE(decode("Zm9vYmFy", bytes, 6));
  EXPECT_FALSE(decode("Zm9vYmFy", bytes, 5));
  EXPECT_FALSE(decode("Zm9vYmE=", bytes, 4));
  EXPECT_TRUE(decode("Zm9vYmE=", bytes, 5));
}

// ---- the input queue ----

TEST(Injector, AKeyIsPressedAndReleasedThroughTheDebounceAtAnyLoopSpeed) {
  for (const uint32_t step : {1u, 2u, 3u, 5u, 6u, 8u, 13u, 50u, 300u}) {
    InputInjector input;
    Debounce debounce;
    ASSERT_TRUE(input.queueKey(1, 120));
    EXPECT_EQ(input.queued(), 2u);
    for (uint32_t now = 1000; now < 4000; now += step) {
      input.advance(now);
      debounce.update(input.buttonMask(), now);
    }
    EXPECT_EQ(debounce.presses, 1) << "step " << step;
    EXPECT_EQ(debounce.releases, 1) << "step " << step;
    EXPECT_GE(debounce.heldMs + 2 * step + 10, 120u) << "step " << step;
    EXPECT_EQ(input.buttonMask(), 0);
    EXPECT_EQ(input.queued(), 0u);
  }
}

TEST(Injector, AnUnevenLoopDoesNotLoseThePress) {
  // 3 ms then 8 ms: the second pass is too early for the debounce, the third must not release yet.
  InputInjector input;
  Debounce debounce;
  ASSERT_TRUE(input.queueKey(0, 20));
  uint32_t now = 500;
  for (int i = 0; i < 400; ++i) {
    input.advance(now);
    debounce.update(input.buttonMask(), now);
    now += (i % 2 == 0) ? 3 : 8;
  }
  EXPECT_EQ(debounce.presses, 1);
  EXPECT_EQ(debounce.releases, 1);
}

TEST(Injector, EightKeysInARowAllArriveInOrder) {
  InputInjector input;
  for (uint8_t i = 0; i < 8; ++i) ASSERT_TRUE(input.queueKey(i % 6, 40));
  EXPECT_EQ(input.queued(), InputInjector::MAX_EVENTS);
  std::vector<uint8_t> seen;
  uint8_t before = 0;
  Debounce debounce;
  for (uint32_t now = 0; now < 5000; now += 4) {
    input.advance(now);
    debounce.update(input.buttonMask(), now);
    if (debounce.current != before && debounce.current != 0) {
      for (uint8_t bit = 0; bit < 8; ++bit) {
        if (debounce.current & (1u << bit)) seen.push_back(bit);
      }
    }
    before = debounce.current;
  }
  EXPECT_EQ(seen, (std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 0, 1}));
  EXPECT_EQ(debounce.releases, 8);
}

TEST(Injector, TheQueueIsBounded) {
  InputInjector input;
  for (int i = 0; i < 8; ++i) EXPECT_TRUE(input.queueKey(0, 100));
  EXPECT_FALSE(input.queueKey(0, 100));
  EXPECT_FALSE(input.queueTouch(1, 1, 1, 1, 80));
  EXPECT_EQ(input.queued(), 16u);
  input.advance(10);  // plays one event
  EXPECT_EQ(input.queued(), 15u);
  EXPECT_FALSE(input.queueKey(0, 100));  // a key needs room for both of its events
}

TEST(Injector, ThePowerKeyAndUnknownButtonsNeverGoIn) {
  InputInjector input;
  EXPECT_FALSE(input.queueKey(6, 100));  // HalGPIO::BTN_POWER
  EXPECT_FALSE(input.queueKey(7, 100));
  EXPECT_FALSE(input.queueKey(255, 100));
  EXPECT_EQ(input.queued(), 0u);
  for (uint32_t now = 0; now < 500; now += 5) {
    input.advance(now);
    EXPECT_EQ(input.buttonMask() & 0xC0, 0);
  }
}

TEST(Injector, GesturesAreSpacedByTheGap) {
  InputInjector input;
  ASSERT_TRUE(input.queueKey(0, 30));
  ASSERT_TRUE(input.queueKey(0, 30));
  uint32_t firstUp = 0;
  uint32_t secondDown = 0;
  uint8_t before = 0;
  int downs = 0;
  for (uint32_t now = 0; now < 1000; ++now) {
    input.advance(now);
    const uint8_t mask = input.buttonMask();
    if (mask && !before && ++downs == 2) secondDown = now;
    if (!mask && before && firstUp == 0) firstUp = now;
    before = mask;
  }
  ASSERT_GT(firstUp, 0u);
  ASSERT_GT(secondDown, 0u);
  EXPECT_GE(secondDown - firstUp, InputInjector::GAP_MS);
}

TEST(Injector, ATapIsATapAtTheStartPoint) {
  InputInjector input;
  input.setPanel(800, 480);
  ASSERT_TRUE(input.queueTouch(300, 100, 300, 100, 80));
  bool pressed = false;
  bool tapped = false;
  bool swiped = false;
  bool longPress = false;
  int activity = 0;
  int candidates = 0;
  for (uint32_t now = 100; now < 600; now += 7) {
    input.advance(now);
    float nx = 0;
    float ny = 0;
    float ex = 0;
    float ey = 0;
    unsigned long held = 0;
    if (input.wasTouchPressed(nx, ny)) {
      pressed = true;
      EXPECT_EQ(pixel(nx, 800), 300);
      EXPECT_EQ(pixel(ny, 480), 100);
    }
    if (input.isTouchTapCandidate(nx, ny, held)) {
      ++candidates;
      EXPECT_LT(held, 200u);
    }
    if (input.wasTouchTap(nx, ny)) {
      tapped = true;
      EXPECT_TRUE(input.wasTouchReleased());
      EXPECT_EQ(pixel(nx, 800), 300);
      EXPECT_EQ(pixel(ny, 480), 100);
      EXPECT_EQ(input.lastTouchHeldMs(), 80u);
    }
    swiped = swiped || input.wasSwipe(nx, ny, ex, ey);
    longPress = longPress || input.wasTouchLongPress(nx, ny);
    if (input.wasTouchActivity()) ++activity;
  }
  EXPECT_TRUE(pressed);
  EXPECT_TRUE(tapped);
  EXPECT_FALSE(swiped);
  EXPECT_FALSE(longPress);
  EXPECT_EQ(activity, 2);  // the press and the release: what the auto-sleep timer looks at
  EXPECT_GT(candidates, 3);
  EXPECT_FALSE(input.touchDown());
}

TEST(Injector, ALongHoldFiresTheLongPressOnceWhileDown) {
  InputInjector input;
  ASSERT_TRUE(input.queueTouch(10, 10, 10, 10, 900));
  int longPresses = 0;
  uint32_t firedAt = 0;
  for (uint32_t now = 0; now < 2000; now += 9) {
    input.advance(now);
    float nx = 0;
    float ny = 0;
    if (input.wasTouchLongPress(nx, ny)) {
      ++longPresses;
      firedAt = now;
      EXPECT_TRUE(input.touchDown());
    }
  }
  EXPECT_EQ(longPresses, 1);
  EXPECT_GE(firedAt, InputInjector::LONG_PRESS_MS);
  EXPECT_LT(firedAt, 600u);
}

TEST(Injector, AShortTapPlayedLateIsNotALongPress) {
  InputInjector input;
  ASSERT_TRUE(input.queueTouch(10, 10, 10, 10, 80));
  bool longPress = false;
  bool tapped = false;
  // The loop stalls for 800 ms with the finger down.
  for (const uint32_t now : {0u, 5u, 805u, 810u, 830u, 850u}) {
    input.advance(now);
    float nx = 0;
    float ny = 0;
    longPress = longPress || input.wasTouchLongPress(nx, ny);
    tapped = tapped || input.wasTouchTap(nx, ny);
  }
  EXPECT_FALSE(longPress);
  EXPECT_TRUE(tapped);
  EXPECT_EQ(input.lastTouchHeldMs(), 80u);
}

TEST(Injector, SuppressingTheContactSwallowsItsRelease) {
  InputInjector input;
  ASSERT_TRUE(input.queueTouch(10, 10, 10, 10, 700));
  bool tapped = false;
  bool released = false;
  for (uint32_t now = 0; now < 1500; now += 10) {
    input.advance(now);
    float nx = 0;
    float ny = 0;
    if (input.wasTouchLongPress(nx, ny)) input.suppressTouchContact();
    tapped = tapped || input.wasTouchTap(nx, ny);
    released = released || input.wasTouchReleased();
    EXPECT_FALSE(input.isTouchHeldAt(nx, ny) && now > 600);
  }
  EXPECT_FALSE(tapped);
  EXPECT_FALSE(released);
  // The next touch starts clean.
  ASSERT_TRUE(input.queueTouch(20, 20, 20, 20, 80));
  for (uint32_t now = 1500; now < 2000; now += 10) {
    input.advance(now);
    float nx = 0;
    float ny = 0;
    tapped = tapped || input.wasTouchTap(nx, ny);
  }
  EXPECT_TRUE(tapped);
}

TEST(Injector, ASwipeIsASwipeAndNotATap) {
  InputInjector input;
  input.setPanel(800, 480);
  ASSERT_TRUE(input.queueTouch(600, 240, 200, 240, 250));
  bool tapped = false;
  bool swiped = false;
  bool longPress = false;
  bool movedWhileDown = false;
  for (uint32_t now = 0; now < 1000; now += 8) {
    input.advance(now);
    float sx = 0;
    float sy = 0;
    float ex = 0;
    float ey = 0;
    if (input.isTouchHeldAt(sx, sy)) {
      const int x = pixel(sx, 800);
      EXPECT_LE(x, 600);
      EXPECT_GE(x, 200);
      movedWhileDown = movedWhileDown || (x < 590 && x > 210);
    }
    tapped = tapped || input.wasTouchTap(sx, sy);
    longPress = longPress || input.wasTouchLongPress(sx, sy);
    if (input.wasSwipe(sx, sy, ex, ey)) {
      swiped = true;
      EXPECT_EQ(pixel(sx, 800), 600);
      EXPECT_EQ(pixel(sy, 480), 240);
      EXPECT_EQ(pixel(ex, 800), 200);
      EXPECT_EQ(pixel(ey, 480), 240);
    }
  }
  EXPECT_TRUE(swiped);
  EXPECT_FALSE(tapped);
  EXPECT_FALSE(longPress);
  EXPECT_TRUE(movedWhileDown);
}

TEST(Injector, TheSwipeThresholds) {
  const auto run = [](const uint16_t distance, const uint32_t ms, bool& tapped, bool& swiped) {
    InputInjector input;
    input.queueTouch(100, 100, static_cast<uint16_t>(100 + distance), 100, ms);
    tapped = swiped = false;
    for (uint32_t now = 0; now < ms + 500; now += 5) {
      input.advance(now);
      float a = 0;
      float b = 0;
      float c = 0;
      float d = 0;
      tapped = tapped || input.wasTouchTap(a, b);
      swiped = swiped || input.wasSwipe(a, b, c, d);
    }
  };
  bool tapped = false;
  bool swiped = false;
  run(59, 200, tapped, swiped);  // under the swipe distance: still a tap
  EXPECT_TRUE(tapped);
  EXPECT_FALSE(swiped);
  run(60, 200, tapped, swiped);
  EXPECT_FALSE(tapped);
  EXPECT_TRUE(swiped);
  run(300, 700, tapped, swiped);
  EXPECT_TRUE(swiped);
  run(300, 701, tapped, swiped);  // too slow: a drag, neither a tap nor a swipe
  EXPECT_FALSE(tapped);
  EXPECT_FALSE(swiped);
}

TEST(Injector, CoordinatesOutsideThePanelAreHeldInside) {
  InputInjector input;
  input.setPanel(800, 480);
  ASSERT_TRUE(input.queueTouch(5000, 5000, 5000, 5000, 80));
  bool pressed = false;
  for (uint32_t now = 0; now < 300; now += 5) {
    input.advance(now);
    float nx = 0;
    float ny = 0;
    if (input.wasTouchPressed(nx, ny)) {
      pressed = true;
      EXPECT_EQ(pixel(nx, 800), 799);
      EXPECT_EQ(pixel(ny, 480), 479);
      EXPECT_LT(nx, 1.0f);
      EXPECT_LT(ny, 1.0f);
    }
  }
  EXPECT_TRUE(pressed);
}

TEST(Injector, ARealTouchTakesTheHeldTimeBack) {
  InputInjector input;
  EXPECT_FALSE(input.ownsLastTouch());
  ASSERT_TRUE(input.queueTouch(10, 10, 10, 10, 80));
  input.advance(0);
  EXPECT_TRUE(input.ownsLastTouch());
  input.noteRealTouch();  // while the made-up finger is down it keeps the say
  EXPECT_TRUE(input.ownsLastTouch());
  for (uint32_t now = 5; now < 400; now += 5) input.advance(now);
  EXPECT_TRUE(input.ownsLastTouch());
  input.noteRealTouch();
  EXPECT_FALSE(input.ownsLastTouch());
}

TEST(Injector, AWrappedClockStillPlays) {
  InputInjector input;
  Debounce debounce;
  ASSERT_TRUE(input.queueKey(2, 60));
  ASSERT_TRUE(input.queueKey(3, 60));
  uint32_t now = 0xFFFFFFFFu - 100;
  for (int i = 0; i < 200; ++i) {
    input.advance(now);
    debounce.update(input.buttonMask(), now);
    now += 4;  // passes zero on the way
  }
  EXPECT_EQ(debounce.presses, 2);
  EXPECT_EQ(debounce.releases, 2);
}

// ---- the SD path rule ----

TEST(DevPaths, TheThreeFoldersAndWhatMayBeDoneThere) {
  EXPECT_TRUE(isAllowed("/debug", Access::List));
  EXPECT_TRUE(isAllowed("/debug/2026", Access::List));
  EXPECT_TRUE(isAllowed("/debug/log.txt", Access::Read));
  EXPECT_TRUE(isAllowed("/debug/log.txt", Access::Write));
  EXPECT_TRUE(isAllowed("/debug/run 1/note.txt", Access::Write));
  EXPECT_FALSE(isAllowed("/debug", Access::Read));
  EXPECT_FALSE(isAllowed("/debug", Access::Write));

  EXPECT_TRUE(isAllowed("/screenshots", Access::List));
  EXPECT_TRUE(isAllowed("/screenshots/My Book", Access::List));
  EXPECT_TRUE(isAllowed("/screenshots/screenshot-12.bmp", Access::Read));
  EXPECT_TRUE(isAllowed("/screenshots/My Book/My Book_ch2_p3_40pct_12.bmp", Access::Read));
  EXPECT_FALSE(isAllowed("/screenshots/a.bmp", Access::Write));
  EXPECT_FALSE(isAllowed("/screenshots", Access::Read));

  EXPECT_TRUE(isAllowed("/.vaultink", Access::List));
  EXPECT_TRUE(isAllowed("/.vaultink/bench.csv", Access::Read));
  EXPECT_TRUE(isAllowed("/.vaultink/bench.old.csv", Access::Read));
  EXPECT_TRUE(isAllowed("/.vaultink/bench.cfg", Access::Read));
  EXPECT_TRUE(isAllowed("/.vaultink/battery-log.tsv", Access::Read));
  EXPECT_TRUE(isAllowed("/.vaultink/bench.cfg", Access::Write));
  EXPECT_FALSE(isAllowed("/.vaultink/bench.csv", Access::Write));
  EXPECT_FALSE(isAllowed("/.vaultink/battery-log.tsv", Access::Write));
  EXPECT_FALSE(isAllowed("/.vaultink/new.txt", Access::Write));
}

TEST(DevPaths, TheRestOfTheDeviceFolderIsClosed) {
  for (const Access access : {Access::List, Access::Read, Access::Write, Access::Flash}) {
    EXPECT_FALSE(isAllowed("/.vaultink/lock-pin.txt", access));
    EXPECT_FALSE(isAllowed("/.vaultink/livesync", access));
    EXPECT_FALSE(isAllowed("/.vaultink/livesync/profile.json", access));
    EXPECT_FALSE(isAllowed("/.vaultink/livesync/setup-uri.txt", access));
    EXPECT_FALSE(isAllowed("/.vaultink/phone-text.txt", access));
    EXPECT_FALSE(isAllowed("/.vaultink/habits.txt", access));
    EXPECT_FALSE(isAllowed("/.vaultink/exports/a.md", access));
    EXPECT_FALSE(isAllowed("/.vaultink/bench.csv.bak", access));
    EXPECT_FALSE(isAllowed("/.vaultink/bench.cfg/x", access));
    EXPECT_FALSE(isAllowed("/.crosspoint", access));
    EXPECT_FALSE(isAllowed("/.crosspoint/settings.json", access));
    EXPECT_FALSE(isAllowed("/.crosspoint/wifi.json", access));
    EXPECT_FALSE(isAllowed("/.crosspoint/opds.json", access));
    EXPECT_FALSE(isAllowed("/.crosspoint/koreader.json", access));
    EXPECT_FALSE(isAllowed("/.crosspoint/remotes.json", access));
    EXPECT_FALSE(isAllowed("/.crosspoint/firmware.bin", access));
    EXPECT_FALSE(isAllowed("/.vaultink/firmware.bin", access));
  }
}

TEST(DevPaths, EverythingElseOnTheCardIsClosed) {
  for (const Access access : {Access::List, Access::Read, Access::Write}) {
    EXPECT_FALSE(isAllowed("/", access));
    EXPECT_FALSE(isAllowed("", access));
    EXPECT_FALSE(isAllowed("/Vaults", access));
    EXPECT_FALSE(isAllowed("/Vaults/Main/note.md", access));
    EXPECT_FALSE(isAllowed("/books/a.epub", access));
    EXPECT_FALSE(isAllowed("/Books", access));
    EXPECT_FALSE(isAllowed("/firmware.bin", access));
    EXPECT_FALSE(isAllowed("/debugger/a.txt", access));  // a longer name is another folder
    EXPECT_FALSE(isAllowed("/debug.txt", access));
    EXPECT_FALSE(isAllowed("/screenshots2/a.bmp", access));
    EXPECT_FALSE(isAllowed("/.vaultink2/bench.cfg", access));
    EXPECT_FALSE(isAllowed("/x/debug/a.txt", access));  // the folder must be at the root
    EXPECT_FALSE(isAllowed("/Vaults/screenshots/a.bmp", access));
    EXPECT_FALSE(isAllowed("debug/a.txt", access));  // not absolute
    EXPECT_FALSE(isAllowed("screenshots", access));
  }
}

TEST(DevPaths, TraversalIsRefused) {
  for (const Access access : {Access::List, Access::Read, Access::Write, Access::Flash}) {
    EXPECT_FALSE(isAllowed("/debug/../.crosspoint/wifi.json", access));
    EXPECT_FALSE(isAllowed("/debug/../.crosspoint/settings.json", access));
    EXPECT_FALSE(isAllowed("/debug/..", access));
    EXPECT_FALSE(isAllowed("/debug/../Vaults/a.md", access));
    EXPECT_FALSE(isAllowed("/screenshots/a/../../.vaultink/lock-pin.txt", access));
    EXPECT_FALSE(isAllowed("/../debug/a.txt", access));
    EXPECT_FALSE(isAllowed("/debug/./a.txt", access));
    EXPECT_FALSE(isAllowed("/./debug/a.txt", access));
    EXPECT_FALSE(isAllowed("/debug/a..b.txt", access));  // any ".." at all
    EXPECT_FALSE(isAllowed("/debug\\..\\.crosspoint\\wifi.json", access));
    EXPECT_FALSE(isAllowed("/debug/a\\b.txt", access));
    EXPECT_FALSE(isAllowed("/../firmware.bin", access));
    EXPECT_FALSE(isAllowed("/a/../firmware.bin", access));
  }
}

TEST(DevPaths, DoubleSlashesAndEndSlashesAreRefused) {
  for (const Access access : {Access::List, Access::Read, Access::Write, Access::Flash}) {
    EXPECT_FALSE(isAllowed("//debug/a.txt", access));
    EXPECT_FALSE(isAllowed("/debug//a.txt", access));
    EXPECT_FALSE(isAllowed("/debug/a.txt/", access));
    EXPECT_FALSE(isAllowed("/debug/", access));
    EXPECT_FALSE(isAllowed("//.crosspoint//wifi.json", access));
    EXPECT_FALSE(isAllowed("/.vaultink//bench.cfg", access));
    EXPECT_FALSE(isAllowed("//firmware.bin", access));
    EXPECT_FALSE(isAllowed("//", access));
  }
}

TEST(DevPaths, CaseFollowsFat) {
  // FAT names ignore case: these are the same folders and files.
  EXPECT_TRUE(isAllowed("/DEBUG/Log.TXT", Access::Read));
  EXPECT_TRUE(isAllowed("/Debug", Access::List));
  EXPECT_TRUE(isAllowed("/Screenshots/A.BMP", Access::Read));
  EXPECT_TRUE(isAllowed("/.VaultInk/BENCH.CSV", Access::Read));
  EXPECT_TRUE(isAllowed("/.VAULTINK/Bench.Cfg", Access::Write));
  EXPECT_TRUE(isAllowed("/.VaultInk", Access::List));
  // And so are these.
  for (const Access access : {Access::List, Access::Read, Access::Write, Access::Flash}) {
    EXPECT_FALSE(isAllowed("/.CrossPoint/WIFI.json", access));
    EXPECT_FALSE(isAllowed("/.CROSSPOINT/settings.json", access));
    EXPECT_FALSE(isAllowed("/.CROSSPOINT", access));
    EXPECT_FALSE(isAllowed("/.VaultInk/Lock-Pin.txt", access));
    EXPECT_FALSE(isAllowed("/.VAULTINK/LIVESYNC/PROFILE.JSON", access));
  }
  EXPECT_FALSE(isAllowed("/VAULTS/a.md", Access::Read));
  EXPECT_FALSE(isAllowed("/SCREENSHOTS/a.bmp", Access::Write));
}

TEST(DevPaths, ShortAliasesAreRefused) {
  for (const Access access : {Access::List, Access::Read, Access::Write, Access::Flash}) {
    EXPECT_FALSE(isAllowed("/CROSSP~1/wifi.json", access));
    EXPECT_FALSE(isAllowed("/CROSSP~1", access));
    EXPECT_FALSE(isAllowed("/VAULTI~1/lock-pin.txt", access));
    EXPECT_FALSE(isAllowed("/VAULTI~1/LOCK-P~1.TXT", access));
    EXPECT_FALSE(isAllowed("/VAULTI~1/bench.cfg", access));
    EXPECT_FALSE(isAllowed("/SCREEN~1/a.bmp", access));
    EXPECT_FALSE(isAllowed("/debug/SECRET~1.TXT", access));
    EXPECT_FALSE(isAllowed("/.vaultink/LIVESY~1/PROFIL~1.JSO", access));
    EXPECT_FALSE(isAllowed("/.vaultink/BENCH~1.CSV", access));
    EXPECT_FALSE(isAllowed("/FIRMWA~1.BIN", access));
    EXPECT_FALSE(isAllowed("/crossp~1/wifi.json", access));
  }
  EXPECT_TRUE(isAllowed("/debug/run~a.txt", Access::Read));  // a tilde with no digit is a plain name
}

TEST(DevPaths, NamesSdFatWouldTrimAreRefused) {
  for (const Access access : {Access::List, Access::Read, Access::Write, Access::Flash}) {
    EXPECT_FALSE(isAllowed("/ .crosspoint/wifi.json", access));
    EXPECT_FALSE(isAllowed("/.crosspoint./wifi.json", access));
    EXPECT_FALSE(isAllowed("/.crosspoint /settings.json", access));
    EXPECT_FALSE(isAllowed("/.vaultink./lock-pin.txt", access));
    EXPECT_FALSE(isAllowed("/.vaultink/lock-pin.txt.", access));
    EXPECT_FALSE(isAllowed("/.vaultink/bench.cfg.", access));
    EXPECT_FALSE(isAllowed("/.vaultink/bench.cfg ", access));
    EXPECT_FALSE(isAllowed("/debug /a.txt", access));
    EXPECT_FALSE(isAllowed("/debug/ a.txt", access));
    EXPECT_FALSE(isAllowed("/debug/a.txt.", access));
    EXPECT_FALSE(isAllowed(" /debug/a.txt", access));
    EXPECT_FALSE(isAllowed("/firmware.bin.", access));
    EXPECT_FALSE(isAllowed("/firmware.bin ", access));
  }
}

TEST(DevPaths, ControlBytesAndLongPathsAreRefused) {
  EXPECT_FALSE(isAllowed(std::string("/debug/a\0b.txt", 14), Access::Read));
  EXPECT_FALSE(isAllowed("/debug/a\nb.txt", Access::Read));
  EXPECT_FALSE(isAllowed("/debug/a\tb.txt", Access::Write));
  EXPECT_FALSE(isAllowed("/debug/a\x7f.txt", Access::Read));
  EXPECT_FALSE(isAllowed("/debug/C:a.txt", Access::Read));
  EXPECT_TRUE(isAllowed("/debug/" + std::string(devlinkpaths::MAX_PATH_BYTES - 7, 'a'), Access::Read));
  EXPECT_FALSE(isAllowed("/debug/" + std::string(devlinkpaths::MAX_PATH_BYTES - 6, 'a'), Access::Read));
  EXPECT_TRUE(isAllowed("/screenshots/\xce\x92\xce\xb9\xce\xb2\xce\xbb\xce\xaf\xce\xbf/p1.bmp", Access::Read));
}

TEST(DevPaths, FlashTakesABinOutsideTheDeviceFolders) {
  EXPECT_TRUE(isAllowed("/firmware.bin", Access::Flash));
  EXPECT_TRUE(isAllowed("/FIRMWARE.BIN", Access::Flash));
  EXPECT_TRUE(isAllowed("/fw/vaultink-0.75.0-x4pro-dev.bin", Access::Flash));
  EXPECT_TRUE(isAllowed("/debug/test build.bin", Access::Flash));
  EXPECT_FALSE(isAllowed("/firmware.bin.txt", Access::Flash));
  EXPECT_FALSE(isAllowed("/firmware", Access::Flash));
  EXPECT_FALSE(isAllowed("/.bin", Access::Flash));
  EXPECT_FALSE(isAllowed("firmware.bin", Access::Flash));
  EXPECT_FALSE(isAllowed("/books/a.epub", Access::Flash));
  EXPECT_FALSE(isAllowed("/.crosspoint/wifi.json", Access::Flash));
}

TEST(DevPaths, ParentFolder) {
  EXPECT_EQ(devlinkpaths::parentOf("/debug/a.txt"), "/debug");
  EXPECT_EQ(devlinkpaths::parentOf("/debug/run 1/a.txt"), "/debug/run 1");
  EXPECT_EQ(devlinkpaths::parentOf("/a.txt"), "/");
}
