#pragma once

#include <CbzPages.h>
#include <ZipFile.h>

#include <functional>
#include <string>

#include "ReaderActivity.h"

// CBZ comics (Phase 1c.3): each JPEG/PNG page fitted to the screen in grayscale, one page at a
// time. A page is copied out of the zip, decoded once into a pixel cache (<cache>/p<N>.pxc) and
// drawn from it like the full-screen picture viewer; after each turn the next page is prepared the
// same way. Progress is the page number in <cache>/progress.bin.
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
  std::string getBookThumbBmpPath() const override;
  void renderBook() override;
  void applyInitialOrientation() override;
  bool handleFormatInput() override;  // Confirm or the menu gesture: the chapter list

  bool extractPage(size_t page, std::string& outPath) const;
  void dropStalePageCaches() const;
  std::string pagePxcPath(size_t page) const;
  bool preparePage(size_t page, int& width, int& height);
  bool drawPage(size_t page);
  bool drawDecoded(const std::string& imagePath);
  bool drawPasses(const std::function<bool()>& draw);
  void prefetchNext();
  void saveProgress() const;
  void loadProgress();
  void loadDirection(ZipFile& zip);

  cbz::Book book;
  std::string cachePath;
  uint32_t currentPage = 0;
  bool rightToLeft = false;
};
