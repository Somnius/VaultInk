#include "TextInput.h"

#include <algorithm>
#include <array>

namespace textinput {

// --- Ring ---

void Ring::lock() {
  while (busy_.test_and_set(std::memory_order_acquire)) {
  }
}

void Ring::unlock() { busy_.clear(std::memory_order_release); }

bool Ring::push(const Event& event) {
  lock();
  if (count_ >= kCapacity) {
    unlock();
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  items_[(head_ + count_) % kCapacity] = event;
  ++count_;
  unlock();
  return true;
}

bool Ring::pop(Event& out) {
  lock();
  if (count_ == 0) {
    unlock();
    return false;
  }
  out = items_[head_];
  head_ = (head_ + 1) % kCapacity;
  --count_;
  unlock();
  return true;
}

void Ring::clear() {
  lock();
  head_ = 0;
  count_ = 0;
  unlock();
}

size_t Ring::size() const {
  // size() is a hint for the consumer; a torn read of one size_t cannot happen on the targets we build for.
  return count_;
}

Ring& global() {
  static Ring ring;
  return ring;
}

// --- UTF-8 helpers ---

namespace {

bool isContinuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

// Moves `pos` back to the start of the code point it is inside (or leaves it if already on a boundary).
size_t toBoundary(const std::string& text, size_t pos) {
  pos = std::min(pos, text.size());
  while (pos > 0 && pos < text.size() && isContinuation(static_cast<unsigned char>(text[pos]))) {
    --pos;
  }
  return pos;
}

size_t prevBoundary(const std::string& text, size_t pos) {
  if (pos == 0) {
    return 0;
  }
  --pos;
  while (pos > 0 && isContinuation(static_cast<unsigned char>(text[pos]))) {
    --pos;
  }
  return pos;
}

size_t nextBoundary(const std::string& text, size_t pos) {
  if (pos >= text.size()) {
    return text.size();
  }
  ++pos;
  while (pos < text.size() && isContinuation(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  return pos;
}

bool validCodepoint(uint32_t cp) {
  if (cp < 0x20 || cp == 0x7F) {
    return false;
  }
  if (cp >= 0xD800 && cp <= 0xDFFF) {
    return false;
  }
  return cp <= 0x10FFFF;
}

size_t encodeUtf8(uint32_t cp, std::array<char, 4>& out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

}  // namespace

// --- apply ---

Applied apply(std::string& text, size_t& cursor, size_t maxBytes, const Event& event) {
  cursor = toBoundary(text, cursor);

  switch (event.special) {
    case Special::Enter:
      return Applied::Finished;
    case Special::Escape:
      return Applied::Cancelled;
    case Special::Backspace: {
      if (cursor == 0) {
        return Applied::Nothing;
      }
      const size_t start = prevBoundary(text, cursor);
      text.erase(start, cursor - start);
      cursor = start;
      return Applied::Changed;
    }
    case Special::Delete: {
      if (cursor >= text.size()) {
        return Applied::Nothing;
      }
      text.erase(cursor, nextBoundary(text, cursor) - cursor);
      return Applied::Changed;
    }
    case Special::Left: {
      if (cursor == 0) {
        return Applied::Nothing;
      }
      cursor = prevBoundary(text, cursor);
      return Applied::Changed;
    }
    case Special::Right: {
      if (cursor >= text.size()) {
        return Applied::Nothing;
      }
      cursor = nextBoundary(text, cursor);
      return Applied::Changed;
    }
    case Special::Home: {
      if (cursor == 0) {
        return Applied::Nothing;
      }
      cursor = 0;
      return Applied::Changed;
    }
    case Special::End: {
      if (cursor >= text.size()) {
        return Applied::Nothing;
      }
      cursor = text.size();
      return Applied::Changed;
    }
    case Special::None:
      break;
  }

  if (!validCodepoint(event.codepoint)) {
    return Applied::Nothing;
  }
  std::array<char, 4> bytes{};
  const size_t len = encodeUtf8(event.codepoint, bytes);
  if (text.size() + len > maxBytes) {
    return Applied::Nothing;
  }
  text.insert(cursor, bytes.data(), len);
  cursor += len;
  return Applied::Changed;
}

// --- HID translation ---

namespace {

struct Pair {
  uint32_t plain;
  uint32_t shifted;
};

// Letters a..z (usage 0x04..0x1D) on the Greek layout, lower case. Capitals come from a lookup, not arithmetic, as
// the final sigma has no capital of its own and the ';' key carries punctuation.
constexpr std::array<Pair, 26> kGreekLetters = {{
    {0x3B1, 0x391},  // a α Α
    {0x3B2, 0x392},  // b β Β
    {0x3C8, 0x3A8},  // c ψ Ψ
    {0x3B4, 0x394},  // d δ Δ
    {0x3B5, 0x395},  // e ε Ε
    {0x3C6, 0x3A6},  // f φ Φ
    {0x3B3, 0x393},  // g γ Γ
    {0x3B7, 0x397},  // h η Η
    {0x3B9, 0x399},  // i ι Ι
    {0x3BE, 0x39E},  // j ξ Ξ
    {0x3BA, 0x39A},  // k κ Κ
    {0x3BB, 0x39B},  // l λ Λ
    {0x3BC, 0x39C},  // m μ Μ
    {0x3BD, 0x39D},  // n ν Ν
    {0x3BF, 0x39F},  // o ο Ο
    {0x3C0, 0x3A0},  // p π Π
    {';', ':'},      // q ; :
    {0x3C1, 0x3A1},  // r ρ Ρ
    {0x3C3, 0x3A3},  // s σ Σ
    {0x3C4, 0x3A4},  // t τ Τ
    {0x3B8, 0x398},  // u θ Θ
    {0x3C9, 0x3A9},  // v ω Ω
    {0x3C2, 0x3A3},  // w ς Σ
    {0x3C7, 0x3A7},  // x χ Χ
    {0x3C5, 0x3A5},  // y υ Υ
    {0x3B6, 0x396},  // z ζ Ζ
}};

// Digits 1..9, 0 (usage 0x1E..0x27).
constexpr std::array<Pair, 10> kDigits = {{
    {'1', '!'},
    {'2', '@'},
    {'3', '#'},
    {'4', '$'},
    {'5', '%'},
    {'6', '^'},
    {'7', '&'},
    {'8', '*'},
    {'9', '('},
    {'0', ')'},
}};

// Punctuation 0x2D..0x38 (0x32 is the non-US '#' key, mapped like the US one).
constexpr std::array<Pair, 12> kPunct = {{
    {'-', '_'},
    {'=', '+'},
    {'[', '{'},
    {']', '}'},
    {'\\', '|'},
    {'#', '~'},
    {';', ':'},
    {'\'', '"'},
    {'`', '~'},
    {',', '<'},
    {'.', '>'},
    {'/', '?'},
}};

constexpr uint8_t kUsageTonos = 0x33;  // the ';' key on a US keyboard
constexpr uint32_t kTonos = 0x384;     // GREEK TONOS

// The vowel with a tonos, or 0 when `cp` takes none.
uint32_t withTonos(uint32_t cp) {
  switch (cp) {
    case 0x3B1:
      return 0x3AC;  // ά
    case 0x3B5:
      return 0x3AD;  // έ
    case 0x3B7:
      return 0x3AE;  // ή
    case 0x3B9:
      return 0x3AF;  // ί
    case 0x3BF:
      return 0x3CC;  // ό
    case 0x3C5:
      return 0x3CD;  // ύ
    case 0x3C9:
      return 0x3CE;  // ώ
    case 0x391:
      return 0x386;  // Ά
    case 0x395:
      return 0x388;  // Έ
    case 0x397:
      return 0x389;  // Ή
    case 0x399:
      return 0x38A;  // Ί
    case 0x39F:
      return 0x38C;  // Ό
    case 0x3A5:
      return 0x38E;  // Ύ
    case 0x3A9:
      return 0x38F;  // Ώ
    default:
      return 0;
  }
}

Special specialFor(uint8_t usage) {
  switch (usage) {
    case 0x28:  // Enter
    case 0x58:  // keypad Enter
      return Special::Enter;
    case 0x29:
      return Special::Escape;
    case 0x2A:
      return Special::Backspace;
    case 0x4C:
      return Special::Delete;
    case 0x4F:
      return Special::Right;
    case 0x50:
      return Special::Left;
    case 0x4A:
      return Special::Home;
    case 0x4D:
      return Special::End;
    default:
      return Special::None;
  }
}

// The character a key gives on the US layout, 0 when it has none.
uint32_t usCharFor(uint8_t usage, bool shift) {
  if (usage >= 0x04 && usage <= 0x1D) {
    const uint32_t base = shift ? 'A' : 'a';
    return base + (usage - 0x04);
  }
  if (usage >= 0x1E && usage <= 0x27) {
    const Pair& p = kDigits[usage - 0x1E];
    return shift ? p.shifted : p.plain;
  }
  if (usage == 0x2C) {
    return ' ';
  }
  if (usage >= 0x2D && usage <= 0x38) {
    const Pair& p = kPunct[usage - 0x2D];
    return shift ? p.shifted : p.plain;
  }
  switch (usage) {
    case 0x54:
      return '/';
    case 0x55:
      return '*';
    case 0x56:
      return '-';
    case 0x57:
      return '+';
    case 0x63:
      return '.';
    default:
      break;
  }
  if (usage >= 0x59 && usage <= 0x61) {
    return '1' + (usage - 0x59);
  }
  if (usage == 0x62) {
    return '0';
  }
  return 0;
}

uint32_t greekCharFor(uint8_t usage, bool shift) {
  if (usage >= 0x04 && usage <= 0x1D) {
    const Pair& p = kGreekLetters[usage - 0x04];
    return shift ? p.shifted : p.plain;
  }
  return usCharFor(usage, shift);
}

}  // namespace

size_t HidTranslator::translate(uint8_t usage, uint8_t mods, Event* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  if ((mods & kModCtrlAltGui) != 0) {
    return 0;
  }
  const bool shift = (mods & kModShift) != 0;

  const Special special = specialFor(usage);
  if (special != Special::None) {
    deadAccent_ = false;  // a special key drops a pending accent
    out[0] = Event{0, special};
    return 1;
  }

  if (layout_ == Layout::Greek && usage == kUsageTonos) {
    if (deadAccent_) {
      deadAccent_ = false;
      out[0] = Event{kTonos, Special::None};
      return 1;
    }
    deadAccent_ = true;
    return 0;
  }

  const uint32_t cp = layout_ == Layout::Greek ? greekCharFor(usage, shift) : usCharFor(usage, shift);
  if (cp == 0) {
    return 0;  // Tab, function keys, modifiers, unknown usages
  }

  if (!deadAccent_) {
    out[0] = Event{cp, Special::None};
    return 1;
  }
  deadAccent_ = false;
  const uint32_t accented = withTonos(cp);
  if (accented != 0) {
    out[0] = Event{accented, Special::None};
    return 1;
  }
  out[0] = Event{kTonos, Special::None};
  if (cap < 2) {
    return 1;
  }
  out[1] = Event{cp, Special::None};
  return 2;
}

}  // namespace textinput
