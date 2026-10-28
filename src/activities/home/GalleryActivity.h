#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// A grid of the pictures (PNG, JPEG, BMP, GIF) in a folder and its subfolders (kos25k #8).
// Swipe or the page buttons turn pages; tap a picture for View, Delete, Move or Copy. Thumbnails
// are decoded once and kept as pixel caches in /.crosspoint/thumbs, so later pages draw fast.
class GalleryActivity final : public Activity {
 public:
  GalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string folder);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int COLS = 3;
  static constexpr int ROWS = 3;
  static constexpr int PER_PAGE = COLS * ROWS;
  static constexpr size_t MAX_PICTURES = 2000;

  void scan();
  int pageCount() const;
  bool cellAt(int x, int y, int& index) const;
  void cellRect(int slot, int& x, int& y, int& w, int& h) const;
  void drawThumb(const std::string& path, int x, int y, int w, int h);
  void pictureActions(int index);
  void transfer(const std::string& path, bool copy);

  std::string root;
  std::vector<std::string> pictures;  // full paths, sorted
  int page = 0;
  bool truncated = false;
  OptionPopup optionPopup;
};
