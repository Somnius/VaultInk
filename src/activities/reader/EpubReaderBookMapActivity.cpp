#include "EpubReaderBookMapActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_STRIP = 1;
constexpr int kSideMargin = 16;
constexpr int kStripHeight = 64;
constexpr int kMarkerRoom = 26;  // above the strip: the marker's arrow and the bookmark marks
constexpr int kBelowRoom = 30;   // below it: the clipping marks and the "you were here" box
constexpr int kTapSlop = 10;     // a finger that moved less than this tapped
constexpr uint32_t kScrubIntervalMs = 200;
constexpr int kTitleFont = UI_12_FONT_ID;
constexpr int kSmallFont = UI_10_FONT_ID;

void drawBookmarkMark(const GfxRenderer& renderer, const int x, const int y) { renderer.fillRect(x - 1, y, 3, 10); }
void drawClippingMark(const GfxRenderer& renderer, const int x, const int y) { renderer.fillRect(x, y, 2, 8); }
void drawOriginMark(const GfxRenderer& renderer, const int x, const int y) { renderer.drawRect(x - 4, y, 9, 9); }
}  // namespace

EpubReaderBookMapActivity::EpubReaderBookMapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     BookMapData&& data)
    : Activity("EpubReaderBookMap", renderer, mappedInput), UiAppHost(renderer), data(std::move(data)) {}

int EpubReaderBookMapActivity::stripWidth(const GfxRenderer& renderer) {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  return std::max(1, safe.width - 2 * kSideMargin);
}

size_t EpubReaderBookMapActivity::maxGroups(const GfxRenderer& renderer) {
  return bookmap::maxGroupsFor(stripWidth(renderer));
}

void EpubReaderBookMapActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.setScreen(&EpubReaderBookMapActivity::mapScreen, this);
  computeGeometry();
  firstFrame = true;
  requestUpdate();
}

void EpubReaderBookMapActivity::computeGeometry() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  labelY = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  stripY = labelY + renderer.getLineHeight(kTitleFont) + renderer.getLineHeight(kSmallFont) + 12 + kMarkerRoom;
  stripHeight = kStripHeight;
  legendY = stripY + stripHeight + kBelowRoom + 12;

  strip = bookmap::layout(data.groups, safe.x + kSideMargin, stripWidth(renderer));
  data.groups.clear();
  data.groups.shrink_to_fit();  // the layout has its own copy
  bookmap::placeTicks(strip, data.ticks);

  originX = bookmap::xAtPlace(strip, data.currentSpine, data.currentFraction);
  cursorX = pendingX = originX;
  cursorFraction = std::clamp(data.currentFraction, 0.0f, 1.0f);
  // In a hidden chapter the marker stands on its sliver; otherwise on the segment drawn at that pixel.
  const int own = bookmap::segmentOfSpine(strip, data.currentSpine);
  cursorSegment =
      own >= 0 && strip.segments[static_cast<size_t>(own)].hidden ? own : bookmap::segmentContaining(strip, cursorX);
}

void EpubReaderBookMapActivity::setCursorX(const int x) {
  cursorSegment = bookmap::segmentAt(strip, x);
  if (cursorSegment < 0) return;
  const bookmap::Segment& segment = strip.segments[static_cast<size_t>(cursorSegment)];
  // A thin chapter is hit from beside it: the marker stands on the chapter, not under the finger.
  cursorX = std::clamp(x, segment.x, segment.x + std::max(segment.width, 1) - 1);
  cursorFraction = bookmap::fractionIn(strip, cursorSegment, cursorX);
}

void EpubReaderBookMapActivity::setCursorFraction(const float fraction) {
  cursorFraction = std::clamp(fraction, 0.0f, 1.0f);
  cursorX = bookmap::xAt(strip, cursorFraction);
  cursorSegment = bookmap::segmentContaining(strip, cursorX);
}

void EpubReaderBookMapActivity::stepPercent(const int delta) {
  setCursorFraction(cursorFraction + static_cast<float>(delta) / 100.0f);
  pendingX = cursorX;
  requestUpdate();
}

void EpubReaderBookMapActivity::stepChapter(const int delta) {
  const int count = static_cast<int>(strip.segments.size());
  if (count == 0) return;
  cursorSegment = std::clamp(cursorSegment + delta, 0, count - 1);
  const bookmap::Segment& segment = strip.segments[static_cast<size_t>(cursorSegment)];
  cursorX = segment.hidden ? segment.x + segment.width / 2 : segment.x;
  cursorFraction = bookmap::fractionIn(strip, cursorSegment, cursorX);
  pendingX = cursorX;
  requestUpdate();
}

void EpubReaderBookMapActivity::flushScrub() {
  if (!scrubDirty || renderPending.load()) return;
  if (static_cast<uint32_t>(millis()) - lastFrameMs.load() < kScrubIntervalMs) return;
  scrubDirty = false;
  const int beforeX = cursorX;
  const int beforeSegment = cursorSegment;
  setCursorX(pendingX);
  if (cursorX == beforeX && cursorSegment == beforeSegment) return;  // a resting finger draws nothing
  renderPending = true;
  requestUpdate();
}

void EpubReaderBookMapActivity::release(const int x, const bool tap) {
  if (tap) {
    const int index = bookmap::nearestTick(data.ticks, x);
    if (index >= 0) {
      const bookmap::Tick& tick = data.ticks[static_cast<size_t>(index)];
      BookMapResult result;
      result.fraction = tick.fraction;
      if (tick.kind == BookMapData::TICK_BOOKMARK) {
        result.bookmarkIndex = tick.ref;
      } else {
        const int segment = bookmap::segmentOfSpine(strip, tick.spineIndex);
        // A clipping in a hidden chapter: the chapter itself, its place is not in the counted book.
        if (segment >= 0 && strip.segments[static_cast<size_t>(segment)].hidden) result.spineIndex = tick.spineIndex;
      }
      setResult(std::move(result));
      finish();
      return;
    }
  }
  setCursorX(x);
  confirm();
}

void EpubReaderBookMapActivity::confirm() {
  BookMapResult result;
  result.fraction = cursorFraction;
  if (cursorSegment >= 0 && cursorSegment < static_cast<int>(strip.segments.size())) {
    const bookmap::Segment& segment = strip.segments[static_cast<size_t>(cursorSegment)];
    if (segment.hidden) result.spineIndex = segment.firstSpine;
  }
  setResult(std::move(result));
  finish();
}

void EpubReaderBookMapActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderBookMapActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the strip as a drag region, so the position arrives
  // as dragPermille on every held frame and once more on release. It runs before the Back handler because the
  // release of a drag can also register as a swipe (the left-edge back gesture): the drag has to consume it.
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route && route.event.action == ACTION_STRIP && route.event.dragPermille >= 0) {
    const int x = strip.x + (static_cast<int>(route.event.dragPermille) * (std::max(strip.width, 1) - 1) + 500) / 1000;
    if (!dragging) {
      dragging = true;
      dragMoved = false;
      dragStartX = x;
    }
    if (std::abs(x - dragStartX) > kTapSlop) dragMoved = true;
    if (route.snap.touchReleased) {
      dragging = false;
      app.clearTapFlash();  // the release leaves this screen
      release(x, !dragMoved);
      return;
    }
    // Only the newest position is kept: the ones the panel had no time for are dropped.
    pendingX = x;
    scrubDirty = true;
    flushScrub();
    return;
  }
  if (route) return;
  if (routingReady() && dragging) {
    // The finger left without a release on the strip: swallow what that produced.
    if (!route.snap.touchHeld) dragging = false;
    return;
  }
  flushScrub();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { stepPercent(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { stepPercent(1); });
  // The side buttons go a chapter at a time. On edge-button boards (X3, X4 Pro) BTN_UP is the left one, as in
  // Go to %.
  const int upDelta = gpio.hasEdgeSideButtons() ? -1 : 1;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] { stepChapter(upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, upDelta] { stepChapter(-upDelta); });
}

void EpubReaderBookMapActivity::mapScreen(UiScreen& screen, void* user) {
  const auto* self = static_cast<EpubReaderBookMapActivity*>(user);
  // The whole band is the finger's target, marks included; x maps onto the strip itself.
  screen.frame().hit(fui::Rect{static_cast<int16_t>(self->strip.x), static_cast<int16_t>(self->stripY - kMarkerRoom),
                               static_cast<int16_t>(self->strip.width),
                               static_cast<int16_t>(self->stripHeight + kMarkerRoom + kBelowRoom)},
                     ACTION_STRIP, 0, fui::InputTouch | fui::InputDrag);
}

void EpubReaderBookMapActivity::drawLabel() {
  const bookmap::Segment* segment = cursorSegment >= 0 && cursorSegment < static_cast<int>(strip.segments.size())
                                        ? &strip.segments[static_cast<size_t>(cursorSegment)]
                                        : nullptr;
  if (titleSegment != cursorSegment) {
    titleSegment = cursorSegment;
    title = segment && segment->tocIndex >= 0 && data.chapterTitle ? data.chapterTitle(segment->tocIndex) : "";
  }
  if (!title.empty()) {
    const std::string line = renderer.truncatedText(kTitleFont, title.c_str(), strip.width, EpdFontFamily::BOLD);
    renderer.drawText(kTitleFont, strip.x, labelY, line.c_str(), true, EpdFontFamily::BOLD);
  }

  char info[64];
  if (segment && segment->hidden) {
    snprintf(info, sizeof(info), "%s", tr(STR_CHAPTER_HIDDEN));
  } else {
    const int percent = std::clamp(static_cast<int>(cursorFraction * 100.0f + 0.5f), 0, 100);
    const int total = bookmap::pageAt(1.0f, data.pageFirst, data.pageScale);
    if (total > 0) {
      const int page = std::clamp(bookmap::pageAt(cursorFraction, data.pageFirst, data.pageScale), 1, total);
      snprintf(info, sizeof(info), "%d%%   %s %d / %d", percent, tr(STR_PAGE_SHORT), page, total);
    } else {
      snprintf(info, sizeof(info), "%d%%", percent);
    }
  }
  renderer.drawText(kSmallFont, strip.x, labelY + renderer.getLineHeight(kTitleFont) + 4, info);
}

void EpubReaderBookMapActivity::drawStrip() const {
  renderer.drawRect(strip.x - 1, stripY - 1, strip.width + 2, stripHeight + 2);
  for (const bookmap::Segment& segment : strip.segments) {
    if (segment.x > strip.x) renderer.drawLine(segment.x, stripY, segment.x, stripY + stripHeight - 1);
    if (!segment.hidden) continue;
    for (int y = 0; y < stripHeight; y++) {
      for (int x = segment.x + 1; x < segment.x + segment.width; x++) {
        if ((x + y) % 3 == 0) renderer.drawPixel(x, stripY + y);
      }
    }
  }
  for (const bookmap::Tick& tick : data.ticks) {
    if (tick.kind == BookMapData::TICK_BOOKMARK) {
      drawBookmarkMark(renderer, tick.x, stripY - 13);
    } else {
      drawClippingMark(renderer, tick.x, stripY + stripHeight + 3);
    }
  }
  // You were here: a box under the strip and a dotted line through it.
  drawOriginMark(renderer, originX, stripY + stripHeight + 15);
  for (int y = 1; y < stripHeight; y += 3) renderer.drawPixel(originX, stripY + y);
  // The marker: an arrow above, a bar through the strip.
  for (int i = 0; i <= 8; i++) renderer.fillRect(cursorX - (8 - i), stripY - kMarkerRoom + 2 + i, 2 * (8 - i) + 1, 1);
  renderer.fillRect(cursorX - 1, stripY - kMarkerRoom + 10, 3, stripHeight + kMarkerRoom - 10 + 4);
}

void EpubReaderBookMapActivity::drawLegend() const {
  const int lineHeight = renderer.getLineHeight(kSmallFont);
  const int textX = strip.x + 18;
  int y = legendY;
  const auto has = [this](const uint8_t kind) {
    return std::any_of(data.ticks.begin(), data.ticks.end(), [kind](const bookmap::Tick& t) { return t.kind == kind; });
  };
  if (has(BookMapData::TICK_BOOKMARK)) {
    drawBookmarkMark(renderer, strip.x + 5, y + (lineHeight - 10) / 2);
    renderer.drawText(kSmallFont, textX, y, tr(STR_BOOKMARKS));
    y += lineHeight;
  }
  if (has(BookMapData::TICK_CLIPPING)) {
    drawClippingMark(renderer, strip.x + 4, y + (lineHeight - 8) / 2);
    renderer.drawText(kSmallFont, textX, y, tr(STR_CLIPPINGS));
    y += lineHeight;
  }
  drawOriginMark(renderer, strip.x + 5, y + (lineHeight - 9) / 2);
  renderer.drawText(kSmallFont, textX, y, tr(STR_BOOK_MAP_ORIGIN));
  y += lineHeight + 8;
  for (const auto& line : renderer.wrappedText(kSmallFont, tr(STR_BOOK_MAP_HINT), strip.width, 3)) {
    renderer.drawText(kSmallFont, strip.x, y, line.c_str());
    y += lineHeight;
  }
}

void EpubReaderBookMapActivity::render(RenderLock&&) {
  renderer.clearScreen();
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), tr(STR_BOOK_MAP));
  drawLabel();
  drawStrip();
  drawLegend();
  renderUi();  // registers the strip's drag region; it draws nothing

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // The display has no usable partial-window refresh (see the note in docs), so the marker moves with fast
  // refreshes of the whole frame, like the Go to % slider; the first frame is a clean one.
  renderer.displayBuffer(firstFrame ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstFrame = false;
  lastFrameMs = static_cast<uint32_t>(millis());
  renderPending = false;
}
