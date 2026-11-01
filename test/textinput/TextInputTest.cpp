#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "TextInput.h"

using textinput::Applied;
using textinput::Event;
using textinput::HidTranslator;
using textinput::Layout;
using textinput::Ring;
using textinput::Special;

namespace {

Event ch(uint32_t cp) { return Event{cp, Special::None}; }
Event sp(Special s) { return Event{0, s}; }

// --- Ring ---

TEST(Ring, FifoOrder) {
  Ring ring;
  EXPECT_EQ(ring.size(), 0u);
  EXPECT_TRUE(ring.push(ch('a')));
  EXPECT_TRUE(ring.push(ch('b')));
  EXPECT_TRUE(ring.push(sp(Special::Enter)));
  EXPECT_EQ(ring.size(), 3u);
  Event e;
  ASSERT_TRUE(ring.pop(e));
  EXPECT_EQ(e.codepoint, 'a');
  ASSERT_TRUE(ring.pop(e));
  EXPECT_EQ(e.codepoint, 'b');
  ASSERT_TRUE(ring.pop(e));
  EXPECT_EQ(e.special, Special::Enter);
  EXPECT_FALSE(ring.pop(e));
  EXPECT_EQ(ring.size(), 0u);
}

TEST(Ring, DropsNewestWhenFull) {
  Ring ring;
  for (uint32_t i = 0; i < Ring::kCapacity; ++i) {
    EXPECT_TRUE(ring.push(ch('a' + (i % 26))));
  }
  EXPECT_FALSE(ring.push(ch('Z')));
  EXPECT_FALSE(ring.push(ch('Z')));
  EXPECT_EQ(ring.dropped(), 2u);
  EXPECT_EQ(ring.size(), Ring::kCapacity);
  Event e;
  ASSERT_TRUE(ring.pop(e));
  EXPECT_EQ(e.codepoint, 'a');  // the oldest is kept
  size_t popped = 1;
  while (ring.pop(e)) {
    ++popped;
    EXPECT_NE(e.codepoint, 'Z');
  }
  EXPECT_EQ(popped, Ring::kCapacity);
}

TEST(Ring, ClearEmpties) {
  Ring ring;
  ring.push(ch('a'));
  ring.push(ch('b'));
  ring.clear();
  EXPECT_EQ(ring.size(), 0u);
  Event e;
  EXPECT_FALSE(ring.pop(e));
  EXPECT_TRUE(ring.push(ch('c')));
  ASSERT_TRUE(ring.pop(e));
  EXPECT_EQ(e.codepoint, 'c');
}

TEST(Ring, WrapsAround) {
  Ring ring;
  Event e;
  for (uint32_t round = 0; round < 5; ++round) {
    for (uint32_t i = 0; i < 50; ++i) {
      ASSERT_TRUE(ring.push(ch(round * 100 + i)));
    }
    for (uint32_t i = 0; i < 50; ++i) {
      ASSERT_TRUE(ring.pop(e));
      EXPECT_EQ(e.codepoint, round * 100 + i);
    }
  }
}

TEST(Ring, TwoProducersOneConsumer) {
  Ring ring;
  constexpr uint32_t kPerProducer = 20000;
  std::atomic<bool> done{false};
  std::atomic<uint32_t> accepted{0};

  auto producer = [&](uint32_t tag) {
    for (uint32_t i = 0; i < kPerProducer; ++i) {
      while (!ring.push(ch((tag << 24) | i))) {
        std::this_thread::yield();
      }
      accepted.fetch_add(1);
    }
  };

  std::vector<uint32_t> seen[2];
  std::thread consumer([&] {
    Event e;
    while (!done.load() || ring.size() > 0) {
      if (ring.pop(e)) {
        seen[e.codepoint >> 24].push_back(e.codepoint & 0xFFFFFF);
      } else {
        std::this_thread::yield();
      }
    }
    while (ring.pop(e)) {
      seen[e.codepoint >> 24].push_back(e.codepoint & 0xFFFFFF);
    }
  });
  std::thread p0(producer, 0);
  std::thread p1(producer, 1);
  p0.join();
  p1.join();
  done.store(true);
  consumer.join();

  EXPECT_EQ(accepted.load(), 2 * kPerProducer);
  for (uint32_t tag = 0; tag < 2; ++tag) {
    ASSERT_EQ(seen[tag].size(), kPerProducer);
    for (uint32_t i = 0; i < kPerProducer; ++i) {
      ASSERT_EQ(seen[tag][i], i);  // each producer's order is kept
    }
  }
}

TEST(Ring, GlobalIsOneInstance) {
  textinput::global().clear();
  textinput::global().push(ch('g'));
  EXPECT_EQ(textinput::global().size(), 1u);
  textinput::global().clear();
}

// --- apply ---

TEST(Apply, InsertAsciiAtCursor) {
  std::string text = "ac";
  size_t cursor = 1;
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch('b')), Applied::Changed);
  EXPECT_EQ(text, "abc");
  EXPECT_EQ(cursor, 2u);
}

TEST(Apply, GreekAndEmojiMultiByte) {
  std::string text;
  size_t cursor = 0;
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0x3B1)), Applied::Changed);  // α
  EXPECT_EQ(text, "\xCE\xB1");
  EXPECT_EQ(cursor, 2u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0x1F600)), Applied::Changed);  // 😀
  EXPECT_EQ(text, "\xCE\xB1\xF0\x9F\x98\x80");
  EXPECT_EQ(cursor, 6u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0x20AC)), Applied::Changed);  // €
  EXPECT_EQ(text, "\xCE\xB1\xF0\x9F\x98\x80\xE2\x82\xAC");
  EXPECT_EQ(cursor, 9u);

  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Left)), Applied::Changed);
  EXPECT_EQ(cursor, 6u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Left)), Applied::Changed);
  EXPECT_EQ(cursor, 2u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Delete)), Applied::Changed);  // removes the emoji
  EXPECT_EQ(text, "\xCE\xB1\xE2\x82\xAC");
  EXPECT_EQ(cursor, 2u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Backspace)), Applied::Changed);  // removes α
  EXPECT_EQ(text, "\xE2\x82\xAC");
  EXPECT_EQ(cursor, 0u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Right)), Applied::Changed);
  EXPECT_EQ(cursor, 3u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Right)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Home)), Applied::Changed);
  EXPECT_EQ(cursor, 0u);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Home)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::End)), Applied::Changed);
  EXPECT_EQ(cursor, 3u);
}

TEST(Apply, MaxBytesDropsWhatDoesNotFit) {
  std::string text = "abc";
  size_t cursor = 3;
  EXPECT_EQ(textinput::apply(text, cursor, 4, ch(0x3B1)), Applied::Nothing);  // α needs 2 bytes
  EXPECT_EQ(text, "abc");
  EXPECT_EQ(textinput::apply(text, cursor, 4, ch('d')), Applied::Changed);
  EXPECT_EQ(text, "abcd");
  EXPECT_EQ(textinput::apply(text, cursor, 4, ch('e')), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 4, sp(Special::Backspace)), Applied::Changed);  // deleting still works
  EXPECT_EQ(text, "abc");
}

TEST(Apply, BadCursorSnapsToBoundary) {
  std::string text = "\xCE\xB1\xCE\xB2";  // αβ
  size_t cursor = 3;                      // inside β
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch('x')), Applied::Changed);
  EXPECT_EQ(text, "\xCE\xB1x\xCE\xB2");
  EXPECT_EQ(cursor, 3u);

  cursor = 999;  // past the end
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch('y')), Applied::Changed);
  EXPECT_EQ(text, "\xCE\xB1x\xCE\xB2y");
  EXPECT_EQ(cursor, text.size());

  cursor = 1;  // inside α: snaps back to 0, so Backspace has nothing to remove
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Backspace)), Applied::Nothing);
  EXPECT_EQ(text, "\xCE\xB1x\xCE\xB2y");
  EXPECT_EQ(cursor, 0u);
  cursor = 1;  // inside α: Right lands after it
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Right)), Applied::Changed);
  EXPECT_EQ(cursor, 2u);
}

TEST(Apply, BackspaceAtZeroAndDeleteAtEnd) {
  std::string text = "ab";
  size_t cursor = 0;
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Backspace)), Applied::Nothing);
  EXPECT_EQ(text, "ab");
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Left)), Applied::Nothing);
  cursor = 2;
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Delete)), Applied::Nothing);
  std::string empty;
  cursor = 0;
  EXPECT_EQ(textinput::apply(empty, cursor, 100, sp(Special::Backspace)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(empty, cursor, 100, sp(Special::End)), Applied::Nothing);
}

TEST(Apply, EnterFinishesEscapeCancels) {
  std::string text = "abc";
  size_t cursor = 1;
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Enter)), Applied::Finished);
  EXPECT_EQ(textinput::apply(text, cursor, 100, sp(Special::Escape)), Applied::Cancelled);
  EXPECT_EQ(text, "abc");
  EXPECT_EQ(cursor, 1u);
}

TEST(Apply, IgnoresControlAndInvalidCodepoints) {
  std::string text;
  size_t cursor = 0;
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch('\n')), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0x1F)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0x7F)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0xD800)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0xDFFF)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0x110000)), Applied::Nothing);
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0xFFFFFFFF)), Applied::Nothing);
  EXPECT_TRUE(text.empty());
  EXPECT_EQ(textinput::apply(text, cursor, 100, ch(0x10FFFF)), Applied::Changed);
  EXPECT_EQ(text.size(), 4u);
}

// --- HidTranslator ---

constexpr uint8_t kShift = 0x02;
constexpr uint8_t kRightShift = 0x20;
constexpr uint8_t kCtrl = 0x01;
constexpr uint8_t kAlt = 0x04;
constexpr uint8_t kGui = 0x08;

// One key press; returns the single code point or 0 when nothing / not exactly one character came out.
uint32_t one(HidTranslator& t, uint8_t usage, uint8_t mods = 0) {
  Event out[2];
  const size_t n = t.translate(usage, mods, out, 2);
  if (n != 1 || out[0].special != Special::None) {
    return 0;
  }
  return out[0].codepoint;
}

std::string typed(HidTranslator& t, const std::vector<std::pair<uint8_t, uint8_t>>& keys) {
  std::string text;
  size_t cursor = 0;
  for (const auto& [usage, mods] : keys) {
    Event out[2];
    const size_t n = t.translate(usage, mods, out, 2);
    for (size_t i = 0; i < n; ++i) {
      textinput::apply(text, cursor, 1000, out[i]);
    }
  }
  return text;
}

TEST(Hid, UsLetters) {
  HidTranslator t;
  for (uint8_t u = 0x04; u <= 0x1D; ++u) {
    EXPECT_EQ(one(t, u), static_cast<uint32_t>('a' + (u - 0x04)));
    EXPECT_EQ(one(t, u, kShift), static_cast<uint32_t>('A' + (u - 0x04)));
    EXPECT_EQ(one(t, u, kRightShift), static_cast<uint32_t>('A' + (u - 0x04)));
  }
}

TEST(Hid, UsDigitsAndShiftedSymbols) {
  HidTranslator t;
  const std::string plain = "1234567890";
  const std::string shifted = "!@#$%^&*()";
  for (uint8_t i = 0; i < 10; ++i) {
    EXPECT_EQ(one(t, 0x1E + i), static_cast<uint32_t>(plain[i]));
    EXPECT_EQ(one(t, 0x1E + i, kShift), static_cast<uint32_t>(shifted[i]));
  }
}

TEST(Hid, UsPunctuation) {
  HidTranslator t;
  const std::string plain = "-=[]\\#;'`,./";
  const std::string shifted = "_+{}|~:\"~<>?";
  for (uint8_t i = 0; i < 12; ++i) {
    EXPECT_EQ(one(t, 0x2D + i), static_cast<uint32_t>(plain[i])) << "usage " << int(0x2D + i);
    EXPECT_EQ(one(t, 0x2D + i, kShift), static_cast<uint32_t>(shifted[i])) << "usage " << int(0x2D + i);
  }
  EXPECT_EQ(one(t, 0x2C), ' ');
  EXPECT_EQ(one(t, 0x2C, kShift), ' ');
}

TEST(Hid, SpecialKeys) {
  HidTranslator t;
  const std::vector<std::pair<uint8_t, Special>> keys = {
      {0x28, Special::Enter},  {0x29, Special::Escape}, {0x2A, Special::Backspace},
      {0x4C, Special::Delete}, {0x4F, Special::Right},  {0x50, Special::Left},
      {0x4A, Special::Home},   {0x4D, Special::End},    {0x58, Special::Enter},
  };
  for (const auto& [usage, special] : keys) {
    Event out[2];
    ASSERT_EQ(t.translate(usage, 0, out, 2), 1u);
    EXPECT_EQ(out[0].special, special);
    EXPECT_EQ(out[0].codepoint, 0u);
    ASSERT_EQ(t.translate(usage, kShift, out, 2), 1u);  // shift does not change a special
    EXPECT_EQ(out[0].special, special);
  }
}

TEST(Hid, Keypad) {
  HidTranslator t;
  const std::string digits = "1234567890";
  for (uint8_t i = 0; i < 10; ++i) {
    EXPECT_EQ(one(t, 0x59 + i), static_cast<uint32_t>(digits[i]));
  }
  EXPECT_EQ(one(t, 0x54), '/');
  EXPECT_EQ(one(t, 0x55), '*');
  EXPECT_EQ(one(t, 0x56), '-');
  EXPECT_EQ(one(t, 0x57), '+');
  EXPECT_EQ(one(t, 0x63), '.');
}

TEST(Hid, IgnoredKeys) {
  HidTranslator t;
  Event out[2];
  EXPECT_EQ(t.translate(0x2B, 0, out, 2), 0u);  // Tab
  EXPECT_EQ(t.translate(0x00, 0, out, 2), 0u);  // no key
  EXPECT_EQ(t.translate(0x39, 0, out, 2), 0u);  // Caps Lock
  EXPECT_EQ(t.translate(0x3A, 0, out, 2), 0u);  // F1
  EXPECT_EQ(t.translate(0xE0, 0, out, 2), 0u);  // left Ctrl itself
  EXPECT_EQ(t.translate(0x04, 0, nullptr, 2), 0u);
  EXPECT_EQ(t.translate(0x04, 0, out, 0), 0u);
}

TEST(Hid, CtrlAltGuiGiveNothing) {
  HidTranslator t;
  Event out[2];
  EXPECT_EQ(t.translate(0x06, kCtrl, out, 2), 0u);  // Ctrl+C
  EXPECT_EQ(t.translate(0x06, kAlt, out, 2), 0u);
  EXPECT_EQ(t.translate(0x06, kGui, out, 2), 0u);
  EXPECT_EQ(t.translate(0x06, kCtrl | kShift, out, 2), 0u);
  EXPECT_EQ(t.translate(0x28, kCtrl, out, 2), 0u);  // Ctrl+Enter too
  EXPECT_EQ(t.translate(0x06, 0x40, out, 2), 0u);   // right Alt
  EXPECT_EQ(t.translate(0x06, 0x80, out, 2), 0u);   // right Gui
  HidTranslator g(Layout::Greek);
  EXPECT_EQ(g.translate(0x33, kCtrl, out, 2), 0u);  // Ctrl+tonos does not arm the dead key
  EXPECT_EQ(one(g, 0x04), 0x3B1u);
}

TEST(Hid, GreekLetters) {
  HidTranslator t(Layout::Greek);
  EXPECT_EQ(t.layout(), Layout::Greek);
  // Row 1: q w e r t y u i o p -> ; ς ε ρ τ υ θ ι ο π
  EXPECT_EQ(one(t, 0x14), ';');
  EXPECT_EQ(one(t, 0x1A), 0x3C2u);  // ς
  EXPECT_EQ(one(t, 0x08), 0x3B5u);  // ε
  EXPECT_EQ(one(t, 0x15), 0x3C1u);  // ρ
  EXPECT_EQ(one(t, 0x17), 0x3C4u);  // τ
  EXPECT_EQ(one(t, 0x1C), 0x3C5u);  // υ
  EXPECT_EQ(one(t, 0x18), 0x3B8u);  // θ
  EXPECT_EQ(one(t, 0x0C), 0x3B9u);  // ι
  EXPECT_EQ(one(t, 0x12), 0x3BFu);  // ο
  EXPECT_EQ(one(t, 0x13), 0x3C0u);  // π
  // Row 2: a s d f g h j k l -> α σ δ φ γ η ξ κ λ
  EXPECT_EQ(one(t, 0x04), 0x3B1u);
  EXPECT_EQ(one(t, 0x16), 0x3C3u);
  EXPECT_EQ(one(t, 0x07), 0x3B4u);
  EXPECT_EQ(one(t, 0x09), 0x3C6u);
  EXPECT_EQ(one(t, 0x0A), 0x3B3u);
  EXPECT_EQ(one(t, 0x0B), 0x3B7u);
  EXPECT_EQ(one(t, 0x0D), 0x3BEu);
  EXPECT_EQ(one(t, 0x0E), 0x3BAu);
  EXPECT_EQ(one(t, 0x0F), 0x3BBu);
  // Row 3: z x c v b n m -> ζ χ ψ ω β ν μ
  EXPECT_EQ(one(t, 0x1D), 0x3B6u);
  EXPECT_EQ(one(t, 0x1B), 0x3C7u);
  EXPECT_EQ(one(t, 0x06), 0x3C8u);
  EXPECT_EQ(one(t, 0x19), 0x3C9u);
  EXPECT_EQ(one(t, 0x05), 0x3B2u);
  EXPECT_EQ(one(t, 0x11), 0x3BDu);
  EXPECT_EQ(one(t, 0x10), 0x3BCu);
  // Capitals
  EXPECT_EQ(one(t, 0x04, kShift), 0x391u);  // Α
  EXPECT_EQ(one(t, 0x19, kShift), 0x3A9u);  // Ω
  EXPECT_EQ(one(t, 0x1A, kShift), 0x3A3u);  // shift+w -> Σ
  EXPECT_EQ(one(t, 0x16, kShift), 0x3A3u);  // shift+s -> Σ
  EXPECT_EQ(one(t, 0x14, kShift), ':');
  // Digits and punctuation are unchanged
  EXPECT_EQ(one(t, 0x1E), '1');
  EXPECT_EQ(one(t, 0x1E, kShift), '!');
  EXPECT_EQ(one(t, 0x37), '.');
  EXPECT_EQ(one(t, 0x2C), ' ');
}

TEST(Hid, TonosDeadKey) {
  HidTranslator t(Layout::Greek);
  Event out[2];
  // The dead key alone gives nothing.
  EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
  // ...and a vowel after it is accented.
  EXPECT_EQ(one(t, 0x04), 0x3ACu);  // ά
  // Accent consumed: the next vowel is plain.
  EXPECT_EQ(one(t, 0x04), 0x3B1u);

  // Every vowel, lower and upper case.
  const std::vector<std::pair<uint8_t, std::pair<uint32_t, uint32_t>>> vowels = {
      {0x04, {0x3AC, 0x386}}, {0x08, {0x3AD, 0x388}}, {0x0B, {0x3AE, 0x389}}, {0x0C, {0x3AF, 0x38A}},
      {0x12, {0x3CC, 0x38C}}, {0x1C, {0x3CD, 0x38E}}, {0x19, {0x3CE, 0x38F}},
  };
  for (const auto& [usage, forms] : vowels) {
    EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
    EXPECT_EQ(one(t, usage), forms.first);
    EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
    EXPECT_EQ(one(t, usage, kShift), forms.second);
  }

  // A non-vowel after the dead key: the accent, then the character.
  EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
  ASSERT_EQ(t.translate(0x05, 0, out, 2), 2u);  // β
  EXPECT_EQ(out[0].codepoint, 0x384u);
  EXPECT_EQ(out[1].codepoint, 0x3B2u);
  // Same with a space and a digit.
  EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
  ASSERT_EQ(t.translate(0x2C, 0, out, 2), 2u);
  EXPECT_EQ(out[0].codepoint, 0x384u);
  EXPECT_EQ(out[1].codepoint, ' ');
  // With room for one event only, the accent is kept and the character dropped.
  EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
  ASSERT_EQ(t.translate(0x1E, 0, out, 1), 1u);
  EXPECT_EQ(out[0].codepoint, 0x384u);

  // Two dead keys in a row: one accent character.
  EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
  ASSERT_EQ(t.translate(0x33, 0, out, 2), 1u);
  EXPECT_EQ(out[0].codepoint, 0x384u);
  EXPECT_EQ(one(t, 0x04), 0x3B1u);  // disarmed

  // A special key after the dead key drops the accent.
  EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
  ASSERT_EQ(t.translate(0x2A, 0, out, 2), 1u);
  EXPECT_EQ(out[0].special, Special::Backspace);
  EXPECT_EQ(one(t, 0x08), 0x3B5u);

  // setLayout disarms it; on US the ';' key is plain.
  EXPECT_EQ(t.translate(0x33, 0, out, 2), 0u);
  t.setLayout(Layout::Us);
  EXPECT_EQ(one(t, 0x04), 'a');
  EXPECT_EQ(one(t, 0x33), ';');
  EXPECT_EQ(one(t, 0x33, kShift), ':');
}

TEST(Hid, TypesGreekWord) {
  HidTranslator t(Layout::Greek);
  // "καλή" : k a l tonos h
  const std::string word = typed(t, {{0x0E, 0}, {0x04, 0}, {0x0F, 0}, {0x33, 0}, {0x0B, 0}});
  EXPECT_EQ(word, "\xCE\xBA\xCE\xB1\xCE\xBB\xCE\xAE");
  // "Σοφός" with the final sigma on w.
  const std::string sigma = typed(t, {{0x16, kShift}, {0x12, 0}, {0x09, 0}, {0x33, 0}, {0x12, 0}, {0x1A, 0}});
  EXPECT_EQ(sigma, "\xCE\xA3\xCE\xBF\xCF\x86\xCF\x8C\xCF\x82");
}

}  // namespace
