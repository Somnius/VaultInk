#pragma once

#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

// First-run welcome guide: five pages, each a title, a picture drawn in code and two or three
// sentences. Shown on the first start and once after an update that changes it (VERSION),
// unless Settings -> System turns that off; Settings -> System -> Welcome guide opens it any time.
class WelcomeGuideActivity final : public Activity, private UiAppHost {
 public:
  // Bump when the guide's content changes, so it shows once more after the update.
  static constexpr uint8_t VERSION = 2;  // 2: vaults are no longer "soon"
  static constexpr int PAGE_COUNT = 5;

  // True when the guide should open at boot instead of the home screen.
  static bool shouldShowAtBoot();

  explicit WelcomeGuideActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int page = 0;
  // Every other UI language by its own name, for the first page's small print.
  std::string otherLanguages;
  bool confirmSkip = false;

  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void build(UiScreen& screen);
  void drawPicture(UiScreen& screen, freeink::ui::Rect area);
  void drawFooter(UiScreen& screen, freeink::ui::Rect area);
  void drawSkipConfirm(UiScreen& screen);
  void next();
  void previous();
  void finishGuide();
};
