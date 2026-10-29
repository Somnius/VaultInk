#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "SettingsSection.h"
#include "activities/UiListActivity.h"

// Settings -> Search (0.33.0, #13): finds settings by name, Greek accents and case folded. A
// result opens its section with that row selected (Text Settings rows open the Reader section on
// its Text Settings row). The last row searches the vault notes for the same words.
class SettingsSearchActivity final : public UiListActivity {
 public:
  explicit SettingsSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("SettingsSearch", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;

 private:
  struct Result {
    StrId name;
    SettingsSection section;
    bool textSetting;  // lives in Text Settings, opened through the Reader section
  };
  static constexpr size_t MAX_RESULTS = 40;

  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void askForQuery();
  void runSearch(const std::string& text);
  void rebuildRows();

  bool askPending = true;
  std::string query;
  std::string queryLabel;
  std::string notesLabel;
  std::vector<std::string> subtitles;
  std::vector<Result> results;
  std::vector<freeink::ui::ListItem> rows;
};
