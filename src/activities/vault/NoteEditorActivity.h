#pragma once

#include <LineBuffer.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// The note's source lines as a list (Phase 4b): tap a line to edit it on the keyboard, insert a
// line above or below, delete it or move it; Undo; Save writes the note through a temp file and
// records it as changed. Opened from the reading menu; the reader shows the note again after a
// save. A note changed on the card while it was open (LiveSync) is not overwritten.
class NoteEditorActivity final : public UiListActivity {
 public:
  NoteEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string notePath);
  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  // Rows: Save, Undo, then one per line (or "Add a line" for an empty note).
  static constexpr int ACTION_ROWS = 2;

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override { activateIndex(index); }
  const char* headerTitle() const override { return title.c_str(); }
  bool handleCustomInput() override;
  void onBackButton() override;

  void rebuildRows();
  void lineActions(size_t line);
  void editLine(size_t line, bool insertAt, size_t at);
  void save();
  void showMessage(const char* text);

  std::string path;
  std::string title;
  LineBuffer buffer;
  uint32_t loadedHash = 0;
  size_t loadedSize = 0;
  bool readOnly = false;
  bool loaded = false;
  OptionPopup optionPopup;
  std::vector<freeink::ui::ListItem> rowItems;
  std::vector<std::string> numbers;  // the line numbers shown at the right
  std::string undoLabel;
};
