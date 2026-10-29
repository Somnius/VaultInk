#pragma once

#include <array>
#include <string>
#include <vector>

#include "HomeCoverCache.h"
#include "RecentBooksStore.h"
#include "UiAppHost.h"
#include "activities/ActivityManager.h"
#include "components/lists/list.h"
#include "components/media/book-card.h"

// VaultInk home (docs/design/home.png): a Continue card for the last book, then one list of
// destinations with a status line each. Selection is flat: 0 is the card when a book is open,
// then the rows in Row order.
class VaultInkHomeUi final : public UiAppHost {
 public:
  // Values are stored (Settings -> Home Screen Rows, util/HomeRowOrder.h): append only.
  enum class Row : uint8_t { Books, Vaults, Search, Files, Gallery, Transfer, Library, Settings };
  static const char* rowLabel(Row row, bool plugins);
  static constexpr int MAX_ROWS = 8;

  explicit VaultInkHomeUi(GfxRenderer& renderer);
  // hasLibrarySlot adds the Plugins (or OPDS) row; usePlugins picks which.
  void begin(const std::vector<RecentBook>& books, bool hasContinueReading, bool hasLibrarySlot, bool usePlugins);
  void refreshCoverPath();
  void setSelection(int selection) { selected = selection; }
  // Counts shown on the Books and Vaults rows; 0 or less keeps the plain description.
  void setCounts(int books, int vaults);
  int itemCount() const { return cardCount() + rowCount; }
  // Flat index of the row for a home menu item (returning from that screen), else 0.
  int indexFor(HomeMenuItem item) const;
  // Row for a flat index, or nullptr for the card.
  const Row* rowAt(int index) const;
  int selectedAction(const MappedInputManager& input);
  // True once after a long press on the Continue card (book info).
  bool takeInfoRequest();
  int thumbHeightFor() const;
  bool takeThumbHeightChanged();

 private:
  int cardCount() const { return hasContinueReading ? 1 : 0; }
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  static void onCardInfo(const freeink::ui::ActionEvent& event, void* user);
  void draw(UiScreen& screen);
  void drawCard(UiScreen& screen, freeink::ui::Rect rect);
  void drawRows(UiScreen& screen);

  HomeCoverCache coverCache;
  GfxRenderer& renderer;
  const std::vector<RecentBook>* books = nullptr;
  std::string coverPath;
  int thumbHeight = 0;
  bool thumbHeightChanged = false;
  int selected = 0;
  int pending = -1;
  bool infoRequested = false;
  int progress = -1;
  bool hasContinueReading = false;
  char progressText[12]{};
  char booksText[64]{};
  char vaultsText[64]{};
  std::array<Row, MAX_ROWS> rows{};
  int rowCount = 0;
  bool usePlugins = false;
  freeink::ui::BookCardProps card;
  std::array<freeink::ui::ListItem, MAX_ROWS> items{};
};
