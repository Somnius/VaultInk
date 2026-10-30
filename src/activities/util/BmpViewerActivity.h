#pragma once

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  // `backToCaller`: Back finishes (the gallery opened it) instead of going to the file browser.
  // `askSleepCover`: open straight on the "Set as sleep cover?" question (the gallery's menu entry).
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                    bool backToCaller = false, bool askSleepCover = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  void askSetSleepCover();
  bool canSetSleepCover() const;
  bool renderImage();
  bool saveDecodedSleepCover();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  bool backToCaller = false;
  bool askSleepCover = false;
};
