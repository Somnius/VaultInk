#pragma once

#include <CbzPages.h>

#include <string>

#include "ReaderActivity.h"

// CBZ comics (Phase 1c.3), v1: each JPEG/PNG page fitted to the screen in grayscale, one page at a
// time. The page is copied out of the zip into the book's cache folder, then drawn like the full-
// screen picture viewer. Progress is the page number in <cache>/progress.bin.
class CbzReaderActivity final : public ReaderActivity {
 public:
  explicit CbzReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                             bool allowFastInitialRefresh)
      : ReaderActivity("CbzReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;
  ScreenshotInfo getScreenshotInfo() const override;

 private:
  bool loadBook() override;
  std::string getBookTitle() const override;
  void renderBook() override;
  void applyInitialOrientation() override;

  bool extractPage(size_t page, std::string& outPath) const;
  bool drawPage(const std::string& imagePath);
  void saveProgress() const;
  void loadProgress();

  cbz::Book book;
  std::string cachePath;
  uint32_t currentPage = 0;
};
