#pragma once

#include <string>

#include "activities/Activity.h"

// The book info (kos25k #15) as a small screen, for places without a popup (the home's Continue
// card): title, author, file, size, progress, favourite. Any tap or Back closes it.
class BookInfoActivity final : public Activity {
 public:
  BookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
      : Activity("BookInfo", renderer, mappedInput), path(std::move(path)) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string path;
  std::string text;
};
