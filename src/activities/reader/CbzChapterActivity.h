#pragma once

#include <CbzPages.h>

#include <vector>

#include "activities/UiListActivity.h"

// The chapters of a CBZ (its folders); picking one returns its first page as a PageResult.
class CbzChapterActivity final : public UiListActivity {
 public:
  CbzChapterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<cbz::Chapter> chapters,
                     uint32_t currentPage)
      : UiListActivity("CbzChapters", renderer, mappedInput), chapters(std::move(chapters)), currentPage(currentPage) {}
  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::vector<cbz::Chapter> chapters;
  uint32_t currentPage;
  std::vector<freeink::ui::ListItem> rows;
};
