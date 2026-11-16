#pragma once

#include <cstdint>

// Quick settings pulled down over an open book menu (kos25k #15). The menu closes first, the page under it is
// painted again, and only then does the panel open, so the two are never on the glass together. This decides,
// once per main-loop pass, whether that moment has come.
namespace panelaftermenu {

// A page that is still not back after this time (a long chapter build) keeps the swipe from opening anything.
constexpr uint32_t WAIT_MS = 3000;

enum class Step : uint8_t { Wait, Open, Drop };

struct Screen {
  bool changePending;      // a push, pop or replace is still queued
  bool updateRequested;    // a frame was asked for and the render task was not told yet
  bool rendering;          // the render task holds the lock
  bool paintedSinceClose;  // a frame was finished after the menu closed
  bool visitPainted;       // the screen in front has painted a frame of its own
  bool readerPage;         // the screen in front is a book with no menu, panel or popup open
  bool disturbed;          // a key, a touch or a remote click came after the swipe, or the screen was turned
};

constexpr Step step(const uint32_t elapsedMs, const Screen& s) {
  // The reader acts on that input (a page turn, a new layout): the panel must not land on top of it.
  if (s.disturbed) return Step::Drop;
  if (s.changePending || s.updateRequested || s.rendering || !s.paintedSinceClose || !s.visitPainted) {
    return elapsedMs > WAIT_MS ? Step::Drop : Step::Wait;
  }
  // The page is back: open, also when that was only seen a moment after the wait ran out.
  return s.readerPage ? Step::Open : Step::Drop;
}

}  // namespace panelaftermenu
