#pragma once

#include <CbzPages.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// The reading direction and the chapters of a CBZ (its folders); picking a chapter returns its
// first page as a PageResult. A bar above the list shows how far the book is read, with a tick at
// each chapter; it runs from the right in a right-to-left book.
class CbzChapterActivity final : public UiListActivity {
 public:
  // The first row flips the reading direction; it returns TOGGLE_RIGHT_TO_LEFT as the page.
  static constexpr uint32_t TOGGLE_RIGHT_TO_LEFT = UINT32_MAX;
  CbzChapterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<cbz::Chapter> chapters,
                     uint32_t currentPage, size_t pageCount, bool rightToLeft)
      : UiListActivity("CbzChapters", renderer, mappedInput),
        chapters(std::move(chapters)),
        currentPage(currentPage),
        pageCount(pageCount),
        rightToLeft(rightToLeft) {}
  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  std::vector<cbz::Chapter> chapters;
  uint32_t currentPage;
  size_t pageCount;
  freeink::ui::Rect bar{};
  bool rightToLeft;
  std::string directionLabel;
  std::vector<freeink::ui::ListItem> rows;
};
