// lib/TextInput as the runtime tests see it: the ring is real enough to pop what the runtime pushed, the translator
// knows a few US keys (letters, Enter, Backspace, Shift for capitals; Ctrl/Alt/Gui give nothing). lib/TextInput's own
// implementation has its own tests; these only check what the page turner hands it.

#include "TextInput.h"

namespace textinput {

bool Ring::push(const Event& event) {
  if (count_ >= kCapacity) {
    dropped_.fetch_add(1);
    return false;
  }
  items_[(head_ + count_) % kCapacity] = event;
  ++count_;
  return true;
}

bool Ring::pop(Event& out) {
  if (count_ == 0) return false;
  out = items_[head_];
  head_ = (head_ + 1) % kCapacity;
  --count_;
  return true;
}

void Ring::clear() {
  head_ = 0;
  count_ = 0;
}

size_t Ring::size() const { return count_; }
void Ring::lock() {}
void Ring::unlock() {}

Ring& global() {
  static Ring ring;
  return ring;
}

size_t HidTranslator::translate(const uint8_t usage, const uint8_t mods, Event* out, const size_t cap) {
  if (cap == 0 || (mods & kModCtrlAltGui) != 0) return 0;
  Event e{};
  if (usage >= 0x04 && usage <= 0x1D) {
    const uint32_t base = layout_ == Layout::Greek ? 0x3B1 : 'a';  // alpha, or a
    e.codepoint = base + (usage - 0x04);
    if ((mods & kModShift) != 0) e.codepoint -= 0x20;
  } else if (usage == 0x28) {
    e.special = Special::Enter;
  } else if (usage == 0x2A) {
    e.special = Special::Backspace;
  } else if (usage == 0x29) {
    e.special = Special::Escape;
  } else if (usage == 0x50) {
    e.special = Special::Left;
  } else if (usage == 0x4F) {
    e.special = Special::Right;
  } else {
    return 0;
  }
  out[0] = e;
  return 1;
}

}  // namespace textinput
