#pragma once

#include <BlePageTurner.h>
#include <Gamepad.h>
#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Bluetooth page turner: switch it on, scan, pair a remote, forget one, and map page/chapter buttons.
// The radio and the remote belong to lib/BlePageTurner; this screen only asks it.
// Gamepad Buttons (lib/Gamepad) is one more view of this screen and not a screen of its own, because the radio
// stops on every screen change and the pad would lose its link: the buttons of the linked gamepad are learned
// for the reader's actions the same way ("press a button"), a hold on a row clears it, the last row clears all.
class BleSettingsActivity final : public UiListActivity {
 public:
  explicit BleSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("BleSettings", renderer, mappedInput, true) {}

  void onEnter() override;
  void loop() override;

 private:
  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  enum class View { Menu, Scan, Paired, Mapping, Gamepad };
  View view = View::Menu;
  void onBackButton() override;
  void showView(View target);
  uint32_t signature() const;
  void refresh();
  // Button mapping reads the linked remote's button edges and stores them in its table.
  void startLearning(int index);
  void stepLearning();
  void keepLearned(bool hold);
  void onRowLongPress(int index) override;
  void clearMapping(int index);
  // Gamepad buttons: the same listening, stored in SETTINGS.gamepad.
  void startPadLearning(int index);
  void stepPadLearning();
  void keepPadButton(uint32_t code);
  void clearPadButton(int index);
  bool learning() const { return learnAction != bleturner::Action::None || padLearn != 0; }

  // Written on the main loop under the render lock, read by buildScreen on the render task.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> labels;
  std::string status;
  uint32_t shownSignature = 0;
  unsigned long lastPollMs = 0;
  bleturner::Action learnAction = bleturner::Action::None;
  char learnAddr[18]{};
  char mappingValues[8][24]{};
  uint32_t learnCode = 0;     // the pressed button, kept until its release
  uint32_t learnPressMs = 0;  // when it went down (the remote's clock), to tell a hold from a tap
  unsigned long learnSinceMs = 0;
  const char* learnNote = nullptr;
  uint8_t padLearn = 0;  // the gestures::Action a gamepad button is being learned for; 0: none
  gamepad::Listener padListener;
  char padValues[gamepad::kLearnableCount][16]{};
};
