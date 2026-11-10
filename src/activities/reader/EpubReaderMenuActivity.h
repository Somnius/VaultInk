#pragma once
#include <Epub.h>
#include <I18n.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class EpubReaderMenuActivity final : public UiListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    SEARCH_BOOK,  // books: search the text of the book
    FOOTNOTES,
    TEXT_SETTINGS,
    NIGHT_MODE,
    HIDE_STATUS_BAR,  // 0.66.0 (kos25k #12): hide or show the bottom status bar from the menu
    FRONTLIGHT,
    BLUETOOTH,
    GO_TO_PERCENT,
    GO_TO_PAGE,  // 0.68.0 (kos25k #29): by the page number in brackets in the status bar
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    SAVE_CLIPPING,
    VIEW_CLIPPINGS,
    TOGGLE_BOOKMARK,
    TOGGLE_FAVOURITE,  // books only: add to / remove from the Library's Favourites
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY,
    BACKLINKS,
    EDIT_NOTE,         // notes: the line editor
    NEW_NOTE,          // notes: a new note next to this one
    RENAME_NOTE,       // notes: links to it are not rewritten
    DELETE_NOTE,       // notes: to the vault's .trash
    FLASHCARDS,        // notes: review this note's cards
    EXPORT_CLIPPINGS,  // books with clippings: write them to a note in a vault
    BACK,              // a note: the note before it, else its folder (touch-only boards have no Back key)
    READING_PRESETS,   // 0.70.0 (kos25k #12): save / apply the Text panel's settings as one of ten presets
    BOOK_MAP           // EPUB only: the book as a strip of chapters, with bookmarks and clippings (see layoutId)
  };

  // The id an action has in the More panel's stored order and hidden mask (util/MenuLayout.h, ids 0-31). It is the
  // action's own number, but the 32 numbers are used up, so Book map takes the id of Select Chapter, which is never
  // in the More panel (Contents is a tool of its own) and so was never stored.
  static constexpr int layoutId(const MenuAction action) {
    return action == MenuAction::BOOK_MAP ? static_cast<int>(MenuAction::SELECT_CHAPTER) : static_cast<int>(action);
  }

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static void toggleBluetooth();
  // The toolbar's More panel from the classic list: Chapters, Text settings, Frontlight and Night mode have a tool of
  // their own; Search, Go to % and Go to page lead, and the favourite toggle to Reading presets close the list.
  static void arrangeMoreItems(std::vector<MenuItem>& items);
  static void buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks, bool hasClippings,
                             bool isVaultNote = false, bool isFavourite = false, bool hasBookMap = false);

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks,
                                  bool hasClippings, bool isVaultNote = false, bool isFavourite = false,
                                  bool hasBookMap = false);

  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool acceptsRemoteInput() const override { return routingReady(); }
  bool hasOpenMenu() const override { return true; }  // the reading menu itself

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are set once in the constructor (buildMenuRowItems()); buildScreen()
  // only refreshes rows whose values reflect live state.
  static constexpr size_t MAX_MENU_ITEMS = 32;  // more than a book or a note ever lists at once
  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
  void buildMenuRowItems();

  int listCount() const override { return static_cast<int>(menuItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; everything else
  // (row navigation, page jumps) falls through to the base handler.
  bool handleButtons() override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;

  void closeCancelled();

  // Fixed menu layout
  std::vector<MenuItem> menuItems;

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
