#pragma once

#include <BookMap.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// What the book map draws, worked out by the reader once when the screen opens: plain numbers, no Epub.
struct BookMapData {
  static constexpr uint8_t TICK_BOOKMARK = 0;  // Tick::ref is the bookmark's index in the reader's list
  static constexpr uint8_t TICK_CLIPPING = 1;
  static constexpr size_t MAX_BOOKMARK_TICKS = 256;
  static constexpr size_t CLIPPING_SLOTS = 512;  // at most one clipping mark per 1/512 of the book

  std::vector<bookmap::Group> groups;  // the chapters, already merged to EpubReaderBookMapActivity::maxGroups()
  std::vector<bookmap::Tick> ticks;    // bookmarks and clippings
  int currentSpine = 0;                // where the reader is
  float currentFraction = 0.0f;        // Epub::visibleProgress() of that place
  float pageFirst = 0.0f;              // the page estimate (EpubReaderActivity::bookPageScale); no scale, no pages
  float pageScale = 0.0f;
  // The title of a contents entry, read from the book only when the label needs it.
  std::function<std::string(int tocIndex)> chapterTitle;
};

// Book map (KOReader's skim widget and book map in one screen, no thumbnails): the book as a strip, one segment
// per chapter sized by what counts, hidden chapters as hatched slivers, marks for bookmarks and clippings, the
// place the reader was at, and a marker that follows the finger or the buttons with the chapter, the percent
// and the estimated page above it. Letting go (or Confirm) jumps; a tap on a bookmark mark opens that bookmark.
// The answer is a BookMapResult; Back cancels.
class EpubReaderBookMapActivity final : public Activity, private UiAppHost {
 public:
  EpubReaderBookMapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, BookMapData&& data);

  // How many chapters the strip has room for on this screen (the reader merges the rest).
  static size_t maxGroups(const GfxRenderer& renderer);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static void mapScreen(UiScreen& screen, void* user);
  static int stripWidth(const GfxRenderer& renderer);

  void computeGeometry();
  void drawStrip() const;
  void drawLabel();
  void drawLegend() const;

  // The marker: to a pixel of the strip (a finger), to a share of the book or to a chapter (the buttons).
  void setCursorX(int x);
  void setCursorFraction(float fraction);
  void stepChapter(int delta);
  void stepPercent(int delta);
  // Shows the newest finger position, at most once per kScrubIntervalMs after the last frame was drawn.
  void flushScrub();
  void release(int x, bool tap);
  void confirm();
  void cancel();

  BookMapData data;
  bookmap::Layout strip;
  int stripY = 0;
  int stripHeight = 0;
  int labelY = 0;
  int legendY = 0;
  int originX = 0;  // "you were here"

  int cursorX = 0;
  int cursorSegment = -1;
  float cursorFraction = 0.0f;
  // The label's chapter title, read again only when the marker enters another chapter.
  int titleSegment = -2;
  std::string title;

  // A finger on the strip: where it went down and whether it has moved (a tap picks a mark, a drag does not).
  bool dragging = false;
  bool dragMoved = false;
  int dragStartX = 0;
  int pendingX = 0;
  bool scrubDirty = false;
  std::atomic<bool> renderPending{false};
  std::atomic<uint32_t> lastFrameMs{0};
  bool firstFrame = true;

  ButtonNavigator buttonNavigator;
};
