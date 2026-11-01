#pragma once

#include <Excalidraw.h>

#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

// Read-only view of an Excalidraw drawing (.excalidraw, or the Obsidian plugin's .excalidraw.md, 0.63.0): the whole
// drawing fitted to the screen, pan with swipes or buttons, zoom with the + / - / Fit panel at the bottom right
// (or Confirm), Back returns. The parser is lib/Vault/Excalidraw; colours are not shown (gray fill only), a
// rotated shape is drawn rotated, images are skipped.
class ExcalidrawActivity final : public Activity {
 public:
  ExcalidrawActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void loop() override;

 private:
  enum class Control { None, Back, ZoomIn, ZoomOut, Fit };

  void fit();
  void zoomBy(float factor, int screenX, int screenY);
  void draw(bool firstFrame);
  void drawShape(const excalidraw::Shape& shape) const;
  void drawPolyline(const excalidraw::Shape& shape) const;
  void line(float x1, float y1, float x2, float y2, int width, uint8_t style) const;
  void drawControls() const;
  Control controlAt(int screenX, int screenY) const;
  void applyZoom(Control control);
  int sx(float x) const;
  int sy(float y) const;

  std::string filePath;
  std::unique_ptr<excalidraw::Drawing> drawing;
  excalidraw::Error error = excalidraw::Error::Ok;
  bool loaded = false;
  float viewX = 0, viewY = 0, scale = 1, fitScale = 1;
  int zoomSteps = 0;
};
