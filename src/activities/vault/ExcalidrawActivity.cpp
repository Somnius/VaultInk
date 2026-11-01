#include "ExcalidrawActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cmath>

#include "activities/ActivityManager.h"
#include "fontIds.h"

namespace {
constexpr float MAX_ZOOM = 8.0f;  // times the fit scale
constexpr int CTRL_SIZE = 52;
constexpr int CTRL_GAP = 6;
constexpr int CTRL_RIGHT = 16;
constexpr int CTRL_BOTTOM = 72;
constexpr int CTRL_ROWS = 5;  // Back, +, level, -, Fit
constexpr int FULL_REFRESH_EVERY = 5;

int controlTop(const GfxRenderer& r) { return r.getScreenHeight() - CTRL_BOTTOM - CTRL_ROWS * (CTRL_SIZE + CTRL_GAP); }
int controlLeft(const GfxRenderer& r) { return r.getScreenWidth() - CTRL_RIGHT - CTRL_SIZE; }
}  // namespace

ExcalidrawActivity::ExcalidrawActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("Excalidraw", renderer, mappedInput), filePath(std::move(path)) {}

void ExcalidrawActivity::onEnter() {
  Activity::onEnter();
  drawing = makeUniqueNoThrow<excalidraw::Drawing>();
  HalFile file;
  if (drawing && Storage.openFileForRead("EXD", filePath, file)) {
    error = excalidraw::parse(
        [&file](char* buf, size_t cap) { return static_cast<size_t>(std::max(0, file.read(buf, cap))); }, *drawing);
    loaded = error == excalidraw::Error::Ok && !drawing->shapes.empty();
    file.close();
  }
  LOG_DBG("EXD", "%s: %u shapes%s", filePath.c_str(), drawing ? static_cast<unsigned>(drawing->shapes.size()) : 0u,
          drawing && drawing->truncated ? " (truncated)" : "");
  fit();
  draw(true);
}

void ExcalidrawActivity::fit() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  // The parser's box ignores rotation: grow it by the corners of every rotated shape.
  float minX = drawing ? drawing->minX : 0.0f, minY = drawing ? drawing->minY : 0.0f;
  float maxX = drawing ? drawing->maxX : 0.0f, maxY = drawing ? drawing->maxY : 0.0f;
  if (drawing) {
    for (const auto& shape : drawing->shapes) {
      if (shape.angle == 0.0f || shape.pointCount > 0) continue;
      const float cx = shape.x + shape.w / 2, cy = shape.y + shape.h / 2;
      const float cs = std::cos(shape.angle), sn = std::sin(shape.angle);
      for (int corner = 0; corner < 4; ++corner) {
        const float dx = (corner & 1 ? shape.w : -shape.w) / 2, dy = (corner & 2 ? shape.h : -shape.h) / 2;
        const float rx = cx + dx * cs - dy * sn, ry = cy + dx * sn + dy * cs;
        minX = std::min(minX, rx);
        minY = std::min(minY, ry);
        maxX = std::max(maxX, rx);
        maxY = std::max(maxY, ry);
      }
    }
  }
  const float bw = std::max(1.0f, maxX - minX);
  const float bh = std::max(1.0f, maxY - minY);
  fitScale = std::min(w / bw, h / bh) * 0.95f;
  scale = fitScale;
  viewX = minX - (w / scale - bw) / 2;
  viewY = minY - (h / scale - bh) / 2;
}

void ExcalidrawActivity::zoomBy(const float factor, const int screenX, const int screenY) {
  const float next = std::max(fitScale, std::min(fitScale * MAX_ZOOM, scale * factor));
  const float cx = viewX + screenX / scale;
  const float cy = viewY + screenY / scale;
  scale = next;
  viewX = cx - screenX / scale;
  viewY = cy - screenY / scale;
}

int ExcalidrawActivity::sx(const float x) const { return static_cast<int>(std::lround((x - viewX) * scale)); }
int ExcalidrawActivity::sy(const float y) const { return static_cast<int>(std::lround((y - viewY) * scale)); }

// A line in canvas coordinates; dashed and dotted styles are drawn as short segments.
void ExcalidrawActivity::line(const float x1, const float y1, const float x2, const float y2, const int width,
                              const uint8_t style) const {
  const int ax = sx(x1), ay = sy(y1), bx = sx(x2), by = sy(y2);
  if (style == 0) {
    renderer.drawLine(ax, ay, bx, by, width, true);
    return;
  }
  const float length = std::hypot(static_cast<float>(bx - ax), static_cast<float>(by - ay));
  const float dash = style == 1 ? 10.0f : 3.0f;
  const float gap = style == 1 ? 6.0f : 5.0f;
  if (length < 1.0f) return;
  for (float at = 0; at < length; at += dash + gap) {
    const float end = std::min(length, at + dash);
    renderer.drawLine(ax + static_cast<int>((bx - ax) * at / length), ay + static_cast<int>((by - ay) * at / length),
                      ax + static_cast<int>((bx - ax) * end / length), ay + static_cast<int>((by - ay) * end / length),
                      width, true);
  }
}

void ExcalidrawActivity::drawPolyline(const excalidraw::Shape& shape) const {
  if (shape.pointCount < 2) return;
  const int width = std::max(1, std::min(3, static_cast<int>(std::lround(shape.strokeWidth * scale))));
  const auto* p = &drawing->points[shape.pointStart];
  for (uint16_t i = 1; i < shape.pointCount; ++i) {
    line(shape.x + p[i - 1].x, shape.y + p[i - 1].y, shape.x + p[i].x, shape.y + p[i].y, width, shape.strokeStyle);
  }
  // Arrowheads: two short strokes at the end (and the start) along the last segment.
  const auto head = [&](const excalidraw::Point& from, const excalidraw::Point& tip) {
    const float dx = static_cast<float>(tip.x - from.x), dy = static_cast<float>(tip.y - from.y);
    const float len = std::hypot(dx, dy);
    if (len < 1.0f) return;
    const float size = std::max(10.0f, 14.0f / scale);
    const float ux = dx / len, uy = dy / len;
    const float tx = shape.x + tip.x, ty = shape.y + tip.y;
    line(tx, ty, tx - size * (ux * 0.866f - uy * 0.5f), ty - size * (uy * 0.866f + ux * 0.5f), width, 0);
    line(tx, ty, tx - size * (ux * 0.866f + uy * 0.5f), ty - size * (uy * 0.866f - ux * 0.5f), width, 0);
  };
  if (shape.endArrow) head(p[shape.pointCount - 2], p[shape.pointCount - 1]);
  if (shape.startArrow) head(p[1], p[0]);
}

void ExcalidrawActivity::drawShape(const excalidraw::Shape& shape) const {
  using excalidraw::Kind;
  const int width = std::max(1, std::min(3, static_cast<int>(std::lround(shape.strokeWidth * scale))));
  const float cx = shape.x + shape.w / 2, cy = shape.y + shape.h / 2;
  const float cs = std::cos(shape.angle), sn = std::sin(shape.angle);
  // A corner of the shape's box, rotated around its centre.
  const auto at = [&](const float px, const float py, float& ox, float& oy) {
    const float dx = px - cx, dy = py - cy;
    ox = cx + dx * cs - dy * sn;
    oy = cy + dx * sn + dy * cs;
  };
  switch (shape.kind) {
    case Kind::Rect:
    case Kind::Diamond: {
      float px[4], py[4];
      if (shape.kind == Kind::Rect) {
        at(shape.x, shape.y, px[0], py[0]);
        at(shape.x + shape.w, shape.y, px[1], py[1]);
        at(shape.x + shape.w, shape.y + shape.h, px[2], py[2]);
        at(shape.x, shape.y + shape.h, px[3], py[3]);
      } else {
        at(cx, shape.y, px[0], py[0]);
        at(shape.x + shape.w, cy, px[1], py[1]);
        at(cx, shape.y + shape.h, px[2], py[2]);
        at(shape.x, cy, px[3], py[3]);
      }
      if (shape.fill) {
        int fx[4], fy[4];
        for (int i = 0; i < 4; ++i) {
          fx[i] = sx(px[i]);
          fy[i] = sy(py[i]);
        }
        renderer.fillPolygon(fx, fy, 4, false);  // paper behind the shape, then the stroke
      }
      for (int i = 0; i < 4; ++i) line(px[i], py[i], px[(i + 1) % 4], py[(i + 1) % 4], width, shape.strokeStyle);
      break;
    }
    case Kind::Ellipse: {
      constexpr int kSteps = 28;
      float px[kSteps], py[kSteps];
      for (int i = 0; i < kSteps; ++i) {
        const float t = 6.2831853f * static_cast<float>(i) / kSteps;
        at(cx + shape.w / 2 * std::cos(t), cy + shape.h / 2 * std::sin(t), px[i], py[i]);
      }
      if (shape.fill) {
        int fx[kSteps], fy[kSteps];
        for (int i = 0; i < kSteps; ++i) {
          fx[i] = sx(px[i]);
          fy[i] = sy(py[i]);
        }
        renderer.fillPolygon(fx, fy, kSteps, false);
      }
      for (int i = 0; i < kSteps; ++i) {
        line(px[i], py[i], px[(i + 1) % kSteps], py[(i + 1) % kSteps], width, shape.strokeStyle);
      }
      break;
    }
    case Kind::Line:
    case Kind::Arrow:
    case Kind::FreeDraw:
      drawPolyline(shape);
      break;
    case Kind::Text: {
      const std::string text = drawing->textOf(shape);
      if (text.empty()) break;
      // The reader has a few fixed sizes: the smallest that is not tiny for how big the text is on screen.
      const int heightOnScreen = static_cast<int>(shape.h * scale);
      const int lines = static_cast<int>(std::count(text.begin(), text.end(), '\n')) + 1;
      const int perLine = heightOnScreen / std::max(1, lines);
      if (perLine < 8) break;  // too small to read: skip rather than draw a smear
      const int font = perLine < 22 ? SMALL_FONT_ID : (perLine < 34 ? UI_10_FONT_ID : UI_12_FONT_ID);
      const int lineH = renderer.getLineHeight(font);
      int y = sy(shape.y);
      size_t pos = 0;
      while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string row = text.substr(pos, end - pos);
        renderer.drawText(font, sx(shape.x), y, row.c_str(), true);
        y += std::max(lineH, perLine);
        pos = end + 1;
      }
      break;
    }
  }
}

void ExcalidrawActivity::draw(const bool firstFrame) {
  renderer.clearScreen();
  if (!loaded) {
    const char* message =
        error == excalidraw::Error::BadCompressed ? tr(STR_EXCALIDRAW_COMPRESSED) : tr(STR_FILE_OPEN_FAILED);
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, message);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  const auto savedClip = renderer.getClipRect();
  renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  for (const auto& shape : drawing->shapes) drawShape(shape);
  renderer.setClipRect(savedClip[0], savedClip[1], savedClip[2], savedClip[3]);
  drawControls();
  renderer.displayBuffer(firstFrame ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

void ExcalidrawActivity::drawControls() const {
  const int left = controlLeft(renderer), top = controlTop(renderer);
  const float zoom = scale / fitScale;
  char level[8];
  if (zoom < 1.5f) {
    snprintf(level, sizeof(level), "%s", tr(STR_CANVAS_FIT));
  } else {
    snprintf(level, sizeof(level), "%d\xc3\x97", static_cast<int>(zoom + 0.5f));
  }
  const char* labels[CTRL_ROWS] = {"\xc2\xab", "+", level, "\xe2\x88\x92", tr(STR_CANVAS_FIT)};
  for (int i = 0; i < CTRL_ROWS; ++i) {
    const int y = top + i * (CTRL_SIZE + CTRL_GAP);
    const bool button = i != 2;
    renderer.fillRect(left, y, CTRL_SIZE, CTRL_SIZE, false);
    if (button) renderer.drawRect(left, y, CTRL_SIZE, CTRL_SIZE, 2, true);
    const int font = i == 2 || i == 4 ? SMALL_FONT_ID : UI_12_FONT_ID;
    renderer.drawText(font, left + (CTRL_SIZE - renderer.getTextWidth(font, labels[i])) / 2,
                      y + (CTRL_SIZE - renderer.getLineHeight(font)) / 2, labels[i], true);
  }
}

ExcalidrawActivity::Control ExcalidrawActivity::controlAt(const int screenX, const int screenY) const {
  const int left = controlLeft(renderer), top = controlTop(renderer);
  if (screenX < left - CTRL_GAP || screenX > left + CTRL_SIZE + CTRL_GAP) return Control::None;
  const int row = (screenY - top) / (CTRL_SIZE + CTRL_GAP);
  if (screenY < top || row >= CTRL_ROWS || (screenY - top) % (CTRL_SIZE + CTRL_GAP) >= CTRL_SIZE) return Control::None;
  static constexpr Control ROWS[CTRL_ROWS] = {Control::Back, Control::ZoomIn, Control::None, Control::ZoomOut,
                                              Control::Fit};
  return ROWS[row];
}

void ExcalidrawActivity::applyZoom(const Control control) {
  if (control == Control::Back) {
    activityManager.goToFileBrowser(filePath);
    return;
  }
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  if (control == Control::ZoomIn) zoomBy(2.0f, w / 2, h / 2);
  if (control == Control::ZoomOut) zoomBy(0.5f, w / 2, h / 2);
  if (control == Control::Fit) fit();
  const bool cleanup = control == Control::Fit || ++zoomSteps >= FULL_REFRESH_EVERY;
  if (cleanup) zoomSteps = 0;
  draw(cleanup);
}

void ExcalidrawActivity::loop() {
  Activity::loop();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }
  if (!loaded) return;
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (const Control control = controlAt(tx, ty); control != Control::None) applyZoom(control);
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  int dx = 0, dy = 0;
  if (swipe == MappedInputManager::SwipeDir::Left) dx = w / 2;
  if (swipe == MappedInputManager::SwipeDir::Right) dx = -w / 2;
  if (swipe == MappedInputManager::SwipeDir::Up) dy = h / 2;
  if (swipe == MappedInputManager::SwipeDir::Down) dy = -h / 2;
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) dx = -w / 3;
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) dx = w / 3;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) dy = -h / 3;
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) dy = h / 3;
  if (dx || dy) {
    viewX += dx / scale;
    viewY += dy / scale;
    draw(false);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    applyZoom(scale >= fitScale * MAX_ZOOM * 0.99f ? Control::Fit : Control::ZoomIn);
  }
}
