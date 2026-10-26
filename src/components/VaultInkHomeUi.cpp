#include "VaultInkHomeUi.h"

#include <BoardConfig.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "UITheme.h"
#include "icons/customListIcons.h"
#include "icons/listIcons.h"
#include "util/BookProgress.h"

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId SELECT = 1;
constexpr int16_t CARD_COVER_HEIGHT = 132;
constexpr int16_t ROW_ICON_SIZE = 32;
// Focus outline for button navigation: thick enough to find at a glance, white inside so a
// move repaints only the two rows involved and leaves no large black area to ghost.
constexpr uint8_t FOCUS_BORDER = 4;
}  // namespace

VaultInkHomeUi::VaultInkHomeUi(GfxRenderer& renderer) : UiAppHost(renderer), coverCache(renderer), renderer(renderer) {}

void VaultInkHomeUi::begin(const std::vector<RecentBook>& recent, const bool continuing, const bool librarySlot,
                           const bool plugins) {
  books = &recent;
  hasContinueReading = continuing && !recent.empty();
  usePlugins = plugins;
  rowCount = 0;
  for (const Row row : {Row::Books, Row::Vaults, Row::Files, Row::Transfer, Row::Library, Row::Settings}) {
    if (row == Row::Library && !librarySlot) continue;
    rows[rowCount++] = row;
  }
  if (hasContinueReading) coverCache.begin();
  resetUi();
  app.on(SELECT, &VaultInkHomeUi::onAction, this);
  app.setScreen(&VaultInkHomeUi::screenFn, this);
  refreshCoverPath();
  progress = hasContinueReading ? loadBookProgress(books->front().path) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);
}

int VaultInkHomeUi::indexFor(const HomeMenuItem item) const {
  Row target;
  switch (item) {
    case HomeMenuItem::LIBRARY:
      target = Row::Books;
      break;
    case HomeMenuItem::FILE_BROWSER:
      target = Row::Files;
      break;
    case HomeMenuItem::FILE_TRANSFER:
      target = Row::Transfer;
      break;
    case HomeMenuItem::OPDS_BROWSER:
      target = Row::Library;
      break;
    case HomeMenuItem::SETTINGS_MENU:
      target = Row::Settings;
      break;
    default:
      return 0;
  }
  const auto* end = rows.begin() + rowCount;
  const auto* it = std::find(rows.begin(), end, target);
  return it != end ? cardCount() + static_cast<int>(it - rows.begin()) : 0;
}

const VaultInkHomeUi::Row* VaultInkHomeUi::rowAt(const int index) const {
  const int row = index - cardCount();
  return row >= 0 && row < rowCount ? &rows[row] : nullptr;
}

void VaultInkHomeUi::refreshCoverPath() {
  coverCache.invalidate();
  coverPath = hasContinueReading && thumbHeight > 0
                  ? UITheme::getCoverThumbPath(books->front().coverBmpPath, thumbHeight)
                  : std::string();
}

int VaultInkHomeUi::thumbHeightFor() const { return thumbHeight > 0 ? thumbHeight : CARD_COVER_HEIGHT; }

bool VaultInkHomeUi::takeThumbHeightChanged() { return std::exchange(thumbHeightChanged, false); }

void VaultInkHomeUi::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<VaultInkHomeUi*>(user);
  self.pending = event.value;
  self.app.clearTapFlash();
}

int VaultInkHomeUi::selectedAction(const MappedInputManager& input) {
  pending = -1;
  const auto touch = routeTouch(input);
  return touch.snap.touchReleased ? pending : -1;
}

void VaultInkHomeUi::screenFn(UiScreen& screen, void* user) { static_cast<VaultInkHomeUi*>(user)->draw(screen); }

void VaultInkHomeUi::draw(UiScreen& screen) {
  coverCache.prepare();
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Root screen: the header carries the clock and battery but no back button.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.batteryBarHeight}, nullptr,
                 nullptr, false);
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.batteryBarHeight), static_cast<int16_t>(safe.x),
                  static_cast<int16_t>(metrics.buttonHintsHeight), static_cast<int16_t>(safe.x)});
  screen.insetContent(fui::Insets{theme.spaceSm, theme.spaceLg, theme.spaceSm, theme.spaceLg});
  if (hasContinueReading) {
    drawCard(screen, screen.takeTop(static_cast<int16_t>(CARD_COVER_HEIGHT + 28), theme.spaceLg));
  }
  drawRows(screen);
}

void VaultInkHomeUi::drawCard(UiScreen& screen, fui::Rect rect) {
  const auto& theme = screen.theme();
  const auto& book = books->front();
  card.title = book.title.c_str();
  card.author = book.author.empty() ? nullptr : book.author.c_str();
  card.meta = tr(STR_CONTINUE_READING);
  card.progressLabel = progress >= 0 ? progressText : nullptr;
  card.progress = std::max(0, progress);
  card.progressMax = progress >= 0 ? 100 : 0;
  card.action = SELECT;
  card.value = 0;
  card.state = selected == 0 ? fui::StateSelected : fui::StateNormal;
  card.styles = fui::StyleSet{};
  card.styles.explicitlySet = true;
  card.styles.normal.border = fui::Paint::solid(fui::Color::Black);
  card.styles.normal.borderWidth = 2;
  card.styles.normal.radius = 10;
  card.styles.selected = card.styles.normal;
  card.styles.selected.borderWidth = FOCUS_BORDER;
  card.styles.active = card.styles.selected;
  card.titleText = theme.titleText;
  card.titleText.bold = true;
  card.titleText.maxLines = 2;
  card.authorText = theme.bodyText;
  card.metaText = theme.smallText;
  card.progressText = theme.smallText;
  card.progressHeight = 8;
  card.padding = fui::Insets{14, 14, 14, 14};
  card.gap = theme.spaceLg;
  card.coverSize.height = CARD_COVER_HEIGHT;
  card.coverSize.width = CARD_COVER_HEIGHT * 2 / 3;
  const int height = card.coverSize.height + 8;
  if (thumbHeight != height) {
    thumbHeight = height;
    thumbHeightChanged = true;
    refreshCoverPath();
  }
  card.coverPainterUserData = this;
  card.coverPainter = [](fui::DrawTarget&, fui::Rect cover, const fui::BookCardProps&, void* user) {
    auto& self = *static_cast<VaultInkHomeUi*>(user);
    return !self.coverPath.empty() && self.coverCache.paint(cover, 0, self.coverPath);
  };
  fui::bookCard(screen.frame(), rect, card);
}

void VaultInkHomeUi::drawRows(UiScreen& screen) {
  const auto& theme = screen.theme();
  for (int i = 0; i < rowCount; ++i) {
    fui::ListItem& item = items[i];
    item = fui::ListItem{};
    item.actionValue = static_cast<int16_t>(cardCount() + i);
    switch (rows[i]) {
      case Row::Books:
        item.label = tr(STR_HOME_BOOKS);
        item.subtitle = tr(STR_HOME_BOOKS_DESC);
        item.icon = fui::bitmapFromIcon(icon_book_open_32);
        break;
      case Row::Vaults:
        item.label = tr(STR_VAULTS);
        item.subtitle = tr(STR_HOME_VAULTS_DESC);
        item.icon = fui::bitmapFromIcon(icon_vault_32);
        break;
      case Row::Files:
        item.label = tr(STR_BROWSE_FILES);
        item.subtitle = tr(STR_HOME_FILES_DESC);
        item.icon = fui::bitmapFromIcon(icon_folder_32);
        break;
      case Row::Transfer:
        item.label = tr(STR_FILE_TRANSFER);
        item.subtitle = tr(STR_HOME_TRANSFER_DESC);
        item.icon = fui::bitmapFromIcon(icon_download_32);
        break;
      case Row::Library:
        item.label = usePlugins ? tr(STR_PLUGINS) : tr(STR_OPDS_BROWSER);
        item.subtitle = usePlugins ? tr(STR_HOME_PLUGINS_DESC) : tr(STR_HOME_OPDS_DESC);
        item.icon = fui::bitmapFromIcon(icon_blocks_32);
        break;
      case Row::Settings:
        item.label = tr(STR_SETTINGS_TITLE);
        item.subtitle = tr(STR_HOME_SETTINGS_DESC);
        item.icon = fui::bitmapFromIcon(icon_settings_32);
        break;
    }
  }
  // Books and Vaults (always rows 0 and 1) are the main destinations: a taller list with a
  // black outline. The rest share a lighter list below it. Same focus outline in both.
  const int primaryCount = std::min(rowCount, 2);
  const int full = primaryCount * (88 + 8) + (rowCount - primaryCount) * (68 + 8);
  const int scale = std::min(100, std::max<int>(1, screen.body().height) * 100 / std::max(1, full));
  const auto drawList = [&](const int first, const int count, const bool primary) {
    fui::ListProps props;
    props.items = items.data() + first;
    props.count = static_cast<uint16_t>(count);
    props.selectedIndex = static_cast<int16_t>(selected - cardCount() - first);
    props.action = SELECT;
    props.inputMask = fui::InputTouch;  // buttons are handled by HomeActivity::loop()
    props.labelText = primary ? theme.titleText : theme.bodyText;
    props.labelText.bold = true;
    props.subtitleText = theme.smallText;
    props.iconSize = ROW_ICON_SIZE;
    // Full size in portrait; on a short (landscape) screen the rows shrink to fit, down to the
    // touch minimum the list enforces itself.
    props.rowHeight = static_cast<int16_t>((primary ? 88 : 68) * scale / 100);
    props.rowGap = 8;
    props.rowRadius = 10;
    props.rowInset = 0;
    props.sidePadding = 16;
    props.textGap = 16;
    props.scrollIndicatorWidth = 0;
    props.rowStyles.explicitlySet = true;
    props.rowStyles.normal.border =
        primary ? fui::Paint::solid(fui::Color::Black) : fui::Paint::dither(fui::Color::LightGray);
    props.rowStyles.normal.borderWidth = 2;
    props.rowStyles.normal.radius = 10;
    props.rowStyles.selected = props.rowStyles.normal;
    props.rowStyles.selected.border = fui::Paint::solid(fui::Color::Black);
    props.rowStyles.selected.borderWidth = FOCUS_BORDER;
    props.rowStyles.active = props.rowStyles.selected;
    const int16_t height = static_cast<int16_t>(count * (props.rowHeight + props.rowGap));
    screen.list(props, height);
  };
  drawList(0, primaryCount, true);
  drawList(primaryCount, rowCount - primaryCount, false);
}
