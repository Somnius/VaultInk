#pragma once

#include <Canvas.h>

#include <string>

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
  void drawNode(const canvas::Node& node) const;
  int toScreenX(int32_t x) const;
  int toScreenY(int32_t y) const;
  // The node under a screen point (topmost card first, groups last), or -1.
  int nodeAt(int screenX, int screenY) const;
  void openNode(int index);

  std::string filePath;
  canvas::Canvas model;
  bool loaded = false;
  // View: canvas point shown at the screen's top-left, and pixels per canvas unit.
  float viewX = 0, viewY = 0, scale = 1, fitScale = 1;
};
