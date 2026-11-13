#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

// Typed input from outside the touch keyboard (0.63.0): a Bluetooth keyboard, later the phone. Everything lands in
// one small ring; the on-screen keyboard activity is the only consumer and applies the events to its text.
// Platform-free (no Arduino/ESP/NimBLE), so it is tested on the computer.
namespace textinput {

// Tab, Up, Down, PageUp and PageDown come only from a translator with terminal keys on (the SSH terminal); a text
// line does nothing with them.
enum class Special : uint8_t {
  None,
  Backspace,
  Delete,
  Left,
  Right,
  Home,
  End,
  Enter,
  Escape,
  Tab,
  Up,
  Down,
  PageUp,
  PageDown
};

// One typed thing: a character (a Unicode code point) or a special key.
struct Event {
  uint32_t codepoint = 0;  // 0 for a special key
  Special special = Special::None;
};

// A fixed ring (64 events) that more than one task may push to and one task pops from. When it is full the NEWEST
// event is dropped, so what was typed first is kept in order. push/pop never block for more than a few instructions.
class Ring {
 public:
  static constexpr size_t kCapacity = 64;

  bool push(const Event& event);
  bool pop(Event& out);
  void clear();
  size_t size() const;
  size_t dropped() const { return dropped_.load(); }

 private:
  void lock();
  void unlock();

  Event items_[kCapacity];
  size_t head_ = 0;
  size_t count_ = 0;
  std::atomic<size_t> dropped_{0};
  std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
};

// The one ring of the firmware. The sources push, KeyboardEntryActivity pops.
Ring& global();

// What happened to the text when an event was applied.
enum class Applied : uint8_t {
  Nothing,    // the text did not change (full, nothing to delete, cursor at the edge)
  Changed,    // the text or the cursor moved: redraw
  Finished,   // Enter: the line is done
  Cancelled,  // Escape
};

// Applies one event to a UTF-8 line: `cursor` is a byte offset on a code point boundary; `maxBytes` is the line's
// limit (a character that does not fit is dropped). Characters are inserted at the cursor, Backspace and Delete
// remove a whole code point, Left/Right move by a code point, Home/End jump. Control characters (below 0x20 except
// none) and invalid code points are ignored.
Applied apply(std::string& text, size_t& cursor, size_t maxBytes, const Event& event);

// --- USB HID usage to typed events ---

enum class Layout : uint8_t { Us, Greek };

// HID modifier bits (the SDK's KeyEvent::mods).
constexpr uint8_t kModShift = 0x02 | 0x20;  // left or right shift
constexpr uint8_t kModCtrlAltGui = 0x01 | 0x04 | 0x08 | 0x10 | 0x40 | 0x80;
constexpr uint8_t kModCtrl = 0x01 | 0x10;  // left or right ctrl

// Translates key presses on a keyboard that sits on the US layout. For Layout::Greek the same key position gives the
// letter that key has on the Greek layout, and the tonos (the acute accent, on the ; key position, as on a Greek
// keyboard) is a dead key: it arms an accent for the next vowel; a non-vowel emits the accent character then itself.
class HidTranslator {
 public:
  explicit HidTranslator(Layout layout = Layout::Us) : layout_(layout) {}
  void setLayout(Layout layout) {
    layout_ = layout;
    deadAccent_ = false;
  }
  Layout layout() const { return layout_; }
  // Terminal keys (off by default, for the SSH terminal): Tab, Up, Down, PageUp and PageDown become events, and
  // Ctrl with a letter or one of [ \ ] gives the control character (Ctrl-C is code point 3, Ctrl-[ is 27). Alt and
  // Gui combinations still give nothing.
  void setTerminalKeys(bool on) { terminalKeys_ = on; }
  bool terminalKeys() const { return terminalKeys_; }

  // Returns the number of events written to out (0, 1 or 2). Keys with Ctrl/Alt/Gui held give no typed text.
  size_t translate(uint8_t usage, uint8_t mods, Event* out, size_t cap);

 private:
  Layout layout_;
  bool deadAccent_ = false;
  bool terminalKeys_ = false;
};

}  // namespace textinput
