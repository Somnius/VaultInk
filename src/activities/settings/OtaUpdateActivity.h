#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    WAITING_CONFIRMATION,
    UPDATE_IN_PROGRESS,
    NO_UPDATE,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  // Optional detail line shown under the generic "Update failed" heading.
  // Points into the i18n string table (flash-resident, so no lifetime concern);
  // nullptr means no extra detail.
  const char* failedDetail = nullptr;
  // Release notes, wrapped to the screen on the first confirmation render, and
  // the first line shown. Cancel/Update stay fixed at the bottom (issue #9).
  std::vector<std::string> noteLines;
  int noteScroll = 0;
  int notesPerPage = 1;
  Rect cancelRect{};
  Rect updateRect{};

  void onWifiSelectionComplete(bool success);
  void runUpdateInstall();
  void wrapNotes(int width);

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
