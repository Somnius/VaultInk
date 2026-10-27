#pragma once

#include <Canvas.h>

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"

// Read-only view of an Obsidian .canvas: fit to screen, pan and zoom, tap a card to open it.
class CanvasActivity final : public Activity {
 public:
  CanvasActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void loop() override;

 private:
  void fit();
  void zoomBy(float factor, int screenX, int screenY);
  void pan(int dxScreen, int dyScreen);
  void draw(bool firstFrame);
  void drawEdge(const canvas::Edge& edge) const;
  void drawNode(const canvas::Node& node, const std::string& imagePath) const;
  // The picture of an image card fitted in its frame; false (draw the name) when it can't be shown.
  bool drawImage(const canvas::Node& node, const std::string& imagePath, int x, int y, int w, int h) const;
  int toScreenX(int32_t x) const;
  int toScreenY(int32_t y) const;
  // On-screen zoom controls, bottom right: +, the zoom level, −, Fit.
  enum class Control { None, ZoomIn, ZoomOut, Fit };
  void drawControls() const;
  Control controlAt(int screenX, int screenY) const;
  void applyZoom(Control control);
  int zoomSteps = 0;  // zoom taps since the last full refresh
  // The node under a screen point (topmost card first, groups last), or -1.
  int nodeAt(int screenX, int screenY) const;
  void openNode(int index);

  // SD path of a file card's image ("" when the card isn't an image or it can't be found).
  const std::string& imagePathFor(size_t index);

  std::string filePath;
  canvas::Canvas model;
  std::vector<std::string> imagePaths;  // per node, resolved on first draw
  std::vector<uint8_t> imageResolved;
  bool loaded = false;
  // View: canvas point shown at the screen's top-left, and pixels per canvas unit.
  float viewX = 0, viewY = 0, scale = 1, fitScale = 1;
};
