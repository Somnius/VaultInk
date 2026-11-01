#pragma once

#include <FileSearch.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Search files by name from a Browse Files folder down (kos25k #13). Asks with the keyboard, then walks
// the card (FileSearch::Walker, one folder per step) in slices of about 50 ms per loop() so Back is
// seen at once. Hits appear in the list as they are found, file name over folder path. Back while
// searching stops the walk and keeps what was found; Back again leaves. A tap, or Show in folder,
// finishes with a FilePathResult (the hit's path, a trailing slash on a folder) for Browse Files to
// show it; a long press offers Info, Copy, Move and Show in folder.
class FileSearchActivity final : public UiListActivity {
 public:
  explicit FileSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string rootPath);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  // The card as a DirectorySource for the walker.
  class StorageSource final : public filesearch::DirectorySource {
   public:
    bool ok() const { return static_cast<bool>(nameBuffer); }
    bool list(const std::string& path, std::vector<filesearch::Entry>& out) override;
    StorageSource();

   private:
    static constexpr size_t NAME_BUFFER_SIZE = 500;
    std::unique_ptr<char[]> nameBuffer;
  };
  struct Shown {
    std::string label;   // the name, composed for display
    std::string folder;  // the folder path, clipped per screen build into `fitted`
    bool isDirectory;
  };
  static constexpr unsigned long STEP_MS = 50;      // work per loop() before input is looked at again
  static constexpr unsigned long REDRAW_MS = 1000;  // e-ink: at most one refresh per this while searching

  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;
  void onBackButton() override;

  void askForQuery();
  void startSearch(const std::string& text);
  void stepSearch();
  bool backRequested();
  void finishSearch();
  void refreshShown();  // copies the walker's hits into the rows, under the render lock
  void rebuildRows();
  void showHitActions(int hit);
  void showInfo(int hit);
  void startTransfer(int hit, bool copy);
  void showInFolder(int hit);
  void showError(StrId heading, const std::string& name);
  std::string hitPath(const filesearch::Hit& hit) const;

  std::string rootPath;
  std::string query;
  StorageSource source;
  std::unique_ptr<filesearch::Walker> walker;
  bool askPending = true;
  bool searching = false;
  bool swallowBackRelease = false;  // Back stopped the search; its release must not also leave
  unsigned long lastRedrawMs = 0;
  std::string queryLabel;
  std::string progressText;
  std::vector<Shown> shown;
  std::vector<std::string> fitted;  // the folder lines clipped to the row width, rebuilt per screen
  std::vector<freeink::ui::ListItem> rows;
  OptionPopup optionPopup;
  std::string infoText;      // the info popup shows it, so it outlives the call
  unsigned long infoAt = 0;  // when to open the info popup (0 = none pending)
};
