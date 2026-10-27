#include "CanvasActivity.h"

#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <VaultLink.h>
#include <VaultSdFs.h>

#include <algorithm>
#include <cmath>
#include <functional>

#include "activities/ActivityManager.h"
#include "fontIds.h"
#include "util/VaultFiles.h"
#include "util/VaultNavigation.h"

namespace {

constexpr float MAX_ZOOM = 8.0f;  // times the fit scale
constexpr int CARD_PAD = 4;
// A text card opened from a canvas is written here and read like a note.
constexpr char CARD_NOTE[] = "/.vaultink/canvas-card.md";
constexpr uint32_t MAX_CARD_BYTES = 64 * 1024;
// Zoom controls: finger-sized, inset from the right edge, well above the bottom (Home swipe).
constexpr int CTRL_SIZE = 52;
constexpr int CTRL_GAP = 6;
constexpr int CTRL_RIGHT = 16;
constexpr int CTRL_BOTTOM = 72;
constexpr int CTRL_ROWS = 5;           // Back, +, level, −, Fit
constexpr int FULL_REFRESH_EVERY = 5;  // zoom taps between ghost-clearing refreshes

// The last canvas's view, so Back from a card returns to the same pan and zoom.
struct SavedView {
  std::string path;
  float viewX = 0, viewY = 0, scale = 0;
};
SavedView savedView;

std::string folderOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

// The first lines of a card's Markdown, without heading marks, for the box on screen.
std::string cardText(const std::string& markdown) {
  std::string out;
  size_t pos = 0;
  while (pos < markdown.size()) {
    size_t end = markdown.find('\n', pos);
    if (end == std::string::npos) end = markdown.size();
    std::string line = markdown.substr(pos, end - pos);
    size_t start = line.find_first_not_of("# >-*");
    line = start == std::string::npos ? std::string() : line.substr(start);
    if (!line.empty()) {
      if (!out.empty()) out += ' ';
      out += line;
    }
    pos = end + 1;
  }
  return out;
}

// Liang-Barsky: trims the segment to [0,w)x[0,h); false when none of it is on screen.
bool clipLine(int& x1, int& y1, int& x2, int& y2, const int w, const int h) {
  float t0 = 0, t1 = 1;
  const float dx = static_cast<float>(x2 - x1), dy = static_cast<float>(y2 - y1);
  const float p[4] = {-dx, dx, -dy, dy};
  const float q[4] = {static_cast<float>(x1), static_cast<float>(w - 1 - x1), static_cast<float>(y1),
                      static_cast<float>(h - 1 - y1)};
  for (int i = 0; i < 4; ++i) {
    if (p[i] == 0) {
      if (q[i] < 0) return false;
      continue;
    }
    const float t = q[i] / p[i];
    if (p[i] < 0) {
      if (t > t1) return false;
      t0 = std::max(t0, t);
    } else {
      if (t < t0) return false;
      t1 = std::min(t1, t);
    }
  }
  const int ox = x1, oy = y1;
  x1 = static_cast<int>(ox + t0 * dx);
  y1 = static_cast<int>(oy + t0 * dy);
  x2 = static_cast<int>(ox + t1 * dx);
  y2 = static_cast<int>(oy + t1 * dy);
  return true;
}

}  // namespace

CanvasActivity::CanvasActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("Canvas", renderer, mappedInput), filePath(std::move(path)) {}

void CanvasActivity::onEnter() {
  Activity::onEnter();
  HalFile file;
  loaded = Storage.openFileForRead("CNV", filePath, file) &&
           canvas::parse(
               [&file](char* buf, size_t cap) { return static_cast<size_t>(std::max(0, file.read(buf, cap))); }, model);
  file.close();
  LOG_DBG("CNV", "%s: %u nodes, %u edges%s", filePath.c_str(), static_cast<unsigned>(model.nodes.size()),
          static_cast<unsigned>(model.edges.size()), model.truncated ? " (truncated)" : "");
  imagePaths.assign(model.nodes.size(), std::string());
  imageResolved.assign(model.nodes.size(), 0);
  if (loaded) vaultfiles::rememberLastNote(filePath);  // the vault opens on it next time
  vaultnav::opened(filePath);                          // opened from the browser: older link history no longer applies
  fit();
  if (savedView.path == filePath && savedView.scale > 0) {
    viewX = savedView.viewX;
    viewY = savedView.viewY;
    scale = savedView.scale;
  }
  draw(true);
}

void CanvasActivity::fit() {
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const float bw = static_cast<float>(std::max<int32_t>(1, model.maxX - model.minX));
  const float bh = static_cast<float>(std::max<int32_t>(1, model.maxY - model.minY));
  fitScale = std::min(w / bw, h / bh) * 0.95f;
  scale = fitScale;
  viewX = model.minX - (w / scale - bw) / 2;
  viewY = model.minY - (h / scale - bh) / 2;
}

void CanvasActivity::zoomBy(const float factor, const int screenX, const int screenY) {
  const float next = std::max(fitScale, std::min(fitScale * MAX_ZOOM, scale * factor));
  // Keep the canvas point under (screenX, screenY) in place.
  const float cx = viewX + screenX / scale;
  const float cy = viewY + screenY / scale;
  scale = next;
  viewX = cx - screenX / scale;
  viewY = cy - screenY / scale;
}

void CanvasActivity::pan(const int dxScreen, const int dyScreen) {
  viewX += dxScreen / scale;
  viewY += dyScreen / scale;
}

const std::string& CanvasActivity::imagePathFor(const size_t index) {
  if (!imageResolved[index]) {
    imageResolved[index] = 1;
    const auto& node = model.nodes[index];
    if (node.type == canvas::NodeType::File &&
        (FsHelpers::hasJpgExtension(node.label) || FsHelpers::hasPngExtension(node.label))) {
      const auto fs = vaultsd::fs();
      vaultlink::Target target{node.label, ""};
      imagePaths[index] = vaultlink::resolve(vaultlink::vaultRoot(filePath, fs), folderOf(filePath), target, fs);
    }
  }
  return imagePaths[index];
}

int CanvasActivity::toScreenX(const int32_t x) const { return static_cast<int>(std::lround((x - viewX) * scale)); }
int CanvasActivity::toScreenY(const int32_t y) const { return static_cast<int>(std::lround((y - viewY) * scale)); }

void CanvasActivity::drawEdge(const canvas::Edge& edge) const {
  const auto& a = model.nodes[edge.from];
  const auto& b = model.nodes[edge.to];
  // Anchor on the named side's middle, or the centre when no side is given.
  const auto anchor = [](const canvas::Node& n, const canvas::Side side, int32_t& x, int32_t& y) {
    x = n.x + n.width / 2;
    y = n.y + n.height / 2;
    switch (side) {
      case canvas::Side::Top:
        y = n.y;
        break;
      case canvas::Side::Bottom:
        y = n.y + n.height;
        break;
      case canvas::Side::Left:
        x = n.x;
        break;
      case canvas::Side::Right:
        x = n.x + n.width;
        break;
      case canvas::Side::None:
        break;
    }
  };
  int32_t ax, ay, bx, by;
  anchor(a, edge.fromSide, ax, ay);
  anchor(b, edge.toSide, bx, by);
  const int x1 = toScreenX(ax), y1 = toScreenY(ay), x2 = toScreenX(bx), y2 = toScreenY(by);
  int cx1 = x1, cy1 = y1, cx2 = x2, cy2 = y2;
  if (!clipLine(cx1, cy1, cx2, cy2, renderer.getScreenWidth(), renderer.getScreenHeight())) return;
  renderer.drawLine(cx1, cy1, cx2, cy2, 1, true);
  const auto arrow = [&](const int tipX, const int tipY, const int fromX, const int fromY) {
    const float dx = static_cast<float>(tipX - fromX), dy = static_cast<float>(tipY - fromY);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1) return;
    const float ux = dx / len, uy = dy / len, size = 9;
    const int xs[3] = {tipX, static_cast<int>(tipX - ux * size - uy * size / 2),
                       static_cast<int>(tipX - ux * size + uy * size / 2)};
    const int ys[3] = {tipY, static_cast<int>(tipY - uy * size + ux * size / 2),
                       static_cast<int>(tipY - uy * size - ux * size / 2)};
    renderer.fillPolygon(xs, ys, 3, true);
  };
  if (edge.toArrow) arrow(x2, y2, x1, y1);
  if (edge.fromArrow) arrow(x1, y1, x2, y2);
  if (!edge.label.empty()) {
    const std::string text = renderer.truncatedText(SMALL_FONT_ID, edge.label.c_str(), 140);
    const int tw = renderer.getTextWidth(SMALL_FONT_ID, text.c_str());
    const int lh = renderer.getLineHeight(SMALL_FONT_ID);
    const int mx = (x1 + x2) / 2 - tw / 2, my = (y1 + y2) / 2 - lh / 2;
    renderer.fillRect(mx - 2, my, tw + 4, lh, false);
    renderer.drawText(SMALL_FONT_ID, mx, my, text.c_str(), true);
  }
}

bool CanvasActivity::drawImage(const canvas::Node& node, const std::string& imagePath, const int x, const int y,
                               const int w, const int h) const {
  (void)node;
  const int boxW = w - 2 * CARD_PAD, boxH = h - 2 * CARD_PAD;
  const auto decode = [&](ImageToFramebufferDecoder& decoder) {
    ImageDimensions dims;
    if (!decoder.getDimensions(imagePath, dims) || dims.width <= 0 || dims.height <= 0) return false;
    const float fit = std::min(static_cast<float>(boxW) / dims.width, static_cast<float>(boxH) / dims.height);
    RenderConfig config{0, 0, std::max(1, std::min(boxW, static_cast<int>(dims.width * fit))),
                        std::max(1, std::min(boxH, static_cast<int>(dims.height * fit)))};
    config.x = x + CARD_PAD + (boxW - config.maxWidth) / 2;
    config.y = y + CARD_PAD + (boxH - config.maxHeight) / 2;
    // Decoders draw unclipped: only a picture that fits on the screen is drawn (else the name).
    if (config.x < 0 || config.y < 0 || config.x + config.maxWidth > renderer.getScreenWidth() ||
        config.y + config.maxHeight > renderer.getScreenHeight()) {
      return false;
    }
    config.useExactDimensions = true;
    config.useGrayscale = false;  // the canvas is drawn in one black-and-white pass
    return decoder.decodeToFramebuffer(imagePath, renderer, config);
  };
  // Only the decoder this picture needs is built.
  if (FsHelpers::hasJpgExtension(imagePath)) {
    JpegToFramebufferConverter jpeg;
    return decode(jpeg);
  }
  PngToFramebufferConverter png;
  return decode(png);
}

void CanvasActivity::drawNode(const canvas::Node& node, const std::string& imagePath) const {
  const int x = toScreenX(node.x), y = toScreenY(node.y);
  const int w = std::max(2, static_cast<int>(node.width * scale)),
            h = std::max(2, static_cast<int>(node.height * scale));
  if (x > renderer.getScreenWidth() || y > renderer.getScreenHeight() || x + w < 0 || y + h < 0) return;
  if (node.type == canvas::NodeType::Group) {
    // A coloured group is shaded light gray, so it stands out behind its cards.
    if (node.color) renderer.fillRectDither(x, y, w, h, Color::LightGray);
    renderer.drawRect(x, y, w, h, 1, true);
    if (!node.label.empty()) {
      const std::string text = renderer.truncatedText(SMALL_FONT_ID, node.label.c_str(), std::max(8, w - 8));
      renderer.drawText(SMALL_FONT_ID, x + 4, y - renderer.getLineHeight(SMALL_FONT_ID), text.c_str(), true);
    }
    return;
  }
  renderer.fillRect(x, y, w, h, false);
  // Obsidian's six colours become one light gray on e-ink: a coloured card is shaded and framed
  // heavier; black text stays readable on the gray.
  if (node.color) renderer.fillRectDither(x, y, w, h, Color::LightGray);
  renderer.drawRect(x, y, w, h, node.color ? 2 : 1, true);
  // An image card shows the picture, fitted inside the frame; a card too small for it, or a
  // picture that can't be read, falls back to the file name.
  if (!imagePath.empty() && w > 3 * CARD_PAD && h > 3 * CARD_PAD && drawImage(node, imagePath, x, y, w, h)) return;
  std::string text;
  switch (node.type) {
    case canvas::NodeType::Text:
      text = cardText(node.label);
      break;
    case canvas::NodeType::File:
      text = node.label.substr(node.label.find_last_of('/') + 1);
      break;
    default:
      text = node.label;
  }
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int maxLines = (h - 2 * CARD_PAD) / lineHeight;
  if (maxLines <= 0 || w < 24 || text.empty()) return;
  const auto lines = renderer.wrappedText(SMALL_FONT_ID, text.c_str(), w - 2 * CARD_PAD, maxLines);
  // Text never leaves its card, also when a word is wider than the card.
  const int left = std::max(0, x + 1), top = std::max(0, y + 1);
  const int right = std::min(renderer.getScreenWidth(), x + w - 1),
            bottom = std::min(renderer.getScreenHeight(), y + h - 1);
  if (right <= left || bottom <= top) return;
  const auto screenClip = renderer.getClipRect();
  renderer.setClipRect(left, top, right - left, bottom - top);
  int ly = y + CARD_PAD;
  for (const auto& line : lines) {
    renderer.drawText(SMALL_FONT_ID, x + CARD_PAD, ly, line.c_str(), true);
    ly += lineHeight;
  }
  renderer.setClipRect(screenClip[0], screenClip[1], screenClip[2], screenClip[3]);
}

void CanvasActivity::draw(const bool firstFrame) {
  renderer.clearScreen();
  if (!loaded || model.nodes.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_FILE_OPEN_FAILED));
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  const auto savedClip = renderer.getClipRect();
  renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  // Groups behind everything, then lines, then cards on top.
  static const std::string noImage;
  for (const auto& n : model.nodes) {
    if (n.type == canvas::NodeType::Group) drawNode(n, noImage);
  }
  for (const auto& e : model.edges) drawEdge(e);
  for (size_t i = 0; i < model.nodes.size(); ++i) {
    if (model.nodes[i].type != canvas::NodeType::Group) drawNode(model.nodes[i], imagePathFor(i));
  }
  renderer.setClipRect(savedClip[0], savedClip[1], savedClip[2], savedClip[3]);
  drawControls();
  renderer.displayBuffer(firstFrame ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

namespace {
int controlTop(const GfxRenderer& r) { return r.getScreenHeight() - CTRL_BOTTOM - CTRL_ROWS * (CTRL_SIZE + CTRL_GAP); }
int controlLeft(const GfxRenderer& r) { return r.getScreenWidth() - CTRL_RIGHT - CTRL_SIZE; }
}  // namespace

void CanvasActivity::drawControls() const {
  const int left = controlLeft(renderer), top = controlTop(renderer);
  const float zoom = scale / fitScale;
  char level[8];
  if (zoom < 1.5f) {
    snprintf(level, sizeof(level), "%s", tr(STR_CANVAS_FIT));
  } else {
    snprintf(level, sizeof(level), "%d\xc3\x97", static_cast<int>(zoom + 0.5f));  // "2×"
  }
  const char* labels[CTRL_ROWS] = {"\xc2\xab", "+", level, "\xe2\x88\x92", tr(STR_CANVAS_FIT)};  // « + level − Fit
  for (int i = 0; i < CTRL_ROWS; ++i) {
    const int y = top + i * (CTRL_SIZE + CTRL_GAP);
    const bool button = i != 2;  // the level is a label, not a button
    renderer.fillRect(left, y, CTRL_SIZE, CTRL_SIZE, false);
    if (button) renderer.drawRect(left, y, CTRL_SIZE, CTRL_SIZE, 2, true);
    const int font = i == 2 || i == 4 ? SMALL_FONT_ID : UI_12_FONT_ID;
    const int tw = renderer.getTextWidth(font, labels[i]);
    const int lh = renderer.getLineHeight(font);
    renderer.drawText(font, left + (CTRL_SIZE - tw) / 2, y + (CTRL_SIZE - lh) / 2, labels[i], true);
  }
}

CanvasActivity::Control CanvasActivity::controlAt(const int screenX, const int screenY) const {
  const int left = controlLeft(renderer), top = controlTop(renderer);
  if (screenX < left - CTRL_GAP || screenX > left + CTRL_SIZE + CTRL_GAP) return Control::None;
  const int row = (screenY - top) / (CTRL_SIZE + CTRL_GAP);
  if (screenY < top || row >= CTRL_ROWS || (screenY - top) % (CTRL_SIZE + CTRL_GAP) >= CTRL_SIZE) return Control::None;
  static constexpr Control ROWS[CTRL_ROWS] = {Control::Back, Control::ZoomIn, Control::None, Control::ZoomOut,
                                              Control::Fit};
  return ROWS[row];
}

void CanvasActivity::goBack() {
  if (!vaultnav::goBack()) activityManager.goToFileBrowser(filePath);
}

void CanvasActivity::applyZoom(const Control control) {
  if (control == Control::Back) {
    goBack();
    return;
  }
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  if (control == Control::ZoomIn) zoomBy(2.0f, w / 2, h / 2);
  if (control == Control::ZoomOut) zoomBy(0.5f, w / 2, h / 2);
  if (control == Control::Fit) fit();
  // Fast refreshes leave ghosts: clean up every few steps and whenever the whole board is back.
  const bool cleanup = control == Control::Fit || ++zoomSteps >= FULL_REFRESH_EVERY;
  if (cleanup) zoomSteps = 0;
  draw(cleanup);
}

int CanvasActivity::nodeAt(const int screenX, const int screenY) const {
  const float cx = viewX + screenX / scale, cy = viewY + screenY / scale;
  int group = -1;
  for (int i = static_cast<int>(model.nodes.size()) - 1; i >= 0; --i) {
    const auto& n = model.nodes[i];
    if (cx < n.x || cy < n.y || cx > n.x + n.width || cy > n.y + n.height) continue;
    if (n.type != canvas::NodeType::Group) return i;
    if (group < 0) group = i;
  }
  return group;
}

void CanvasActivity::openNode(const int index) {
  if (index < 0) return;
  const auto& node = model.nodes[index];
  savedView = {filePath, viewX, viewY, scale};
  if (node.type == canvas::NodeType::File) {
    const auto fs = vaultsd::fs();
    vaultlink::Target target{node.label, node.subpath};
    const std::string root = vaultlink::vaultRoot(filePath, fs);
    const std::string path = vaultlink::resolve(root, folderOf(filePath), target, fs);
    if (path.empty()) {
      LOG_DBG("CNV", "Card file not found: %s", node.label.c_str());
      return;
    }
    vaultnav::openFrom(filePath, path);
    return;
  }
  if (node.type == canvas::NodeType::Text && node.textLength > 0) {
    // The whole card, read from the canvas file, opens as a note; Back comes back here.
    HalFile file;
    if (!Storage.openFileForRead("CNV", filePath, file)) return;
    std::string raw(std::min(node.textLength, MAX_CARD_BYTES), '\0');
    const bool ok = file.seek(node.textOffset) && file.read(raw.data(), raw.size()) == static_cast<int>(raw.size());
    file.close();
    if (!ok) return;
    Storage.ensureDirectoryExists("/.vaultink");
    if (!Storage.writeFile(CARD_NOTE, String(canvas::unescape(raw).c_str()))) return;
    // Every card reuses CARD_NOTE, and the reader's cache only checks the size: drop it.
    Storage.removeDir(("/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(CARD_NOTE))).c_str());
    vaultnav::openFrom(filePath, CARD_NOTE);
  }
}

void CanvasActivity::loop() {
  Activity::loop();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }
  if (!loaded) return;
  const int w = renderer.getScreenWidth(), h = renderer.getScreenHeight();
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    // A tap on the panel never opens the card under it (the level label is part of the panel).
    const bool onPanel = tx >= controlLeft(renderer) - CTRL_GAP && ty >= controlTop(renderer) &&
                         ty < controlTop(renderer) + CTRL_ROWS * (CTRL_SIZE + CTRL_GAP);
    if (!onPanel) {
      openNode(nodeAt(tx, ty));
    } else if (const Control control = controlAt(tx, ty); control != Control::None) {
      applyZoom(control);
    }
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
    pan(dx, dy);
    draw(false);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Fit, 2x, 4x, 8x, then back to fit, around the screen centre.
    applyZoom(scale >= fitScale * MAX_ZOOM * 0.99f ? Control::Fit : Control::ZoomIn);
  }
}
