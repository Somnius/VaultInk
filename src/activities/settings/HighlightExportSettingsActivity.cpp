#include "HighlightExportSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>
#include <string>

#include "HighlightExportStore.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr int IDX_READWISE_TOKEN = 0;
constexpr int IDX_MEMOS_URL = 1;
constexpr int IDX_MEMOS_TOKEN = 2;
constexpr int IDX_MEMOS_VERIFY_CERT = 3;

constexpr StrId menuNames[HighlightExportSettingsActivity::MENU_ITEMS] = {
    StrId::STR_READWISE_TOKEN, StrId::STR_MEMOS_SERVER_URL, StrId::STR_MEMOS_TOKEN, StrId::STR_MEMOS_CERT_CHECK};
}  // namespace

HighlightExportSettingsActivity::HighlightExportSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("HighlightExportSettings", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int HighlightExportSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* HighlightExportSettingsActivity::headerTitle() const { return tr(STR_HIGHLIGHT_EXPORTS); }

void HighlightExportSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == IDX_READWISE_TOKEN || index == IDX_MEMOS_TOKEN) {
    const bool readwise = index == IDX_READWISE_TOKEN;
    // The keyboard starts empty: the stored token is never shown again.
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(menuNames[index]), "",
                                                HighlightExportStore::MAX_TOKEN_CHARS, InputType::Password),
        [readwise](const ActivityResult& result) {
          if (result.isCancelled) return;
          const auto& kb = std::get<KeyboardResult>(result.data);
          if (readwise) {
            EXPORT_STORE.setReadwiseToken(kb.text);
          } else {
            EXPORT_STORE.setMemosToken(kb.text);
          }
          EXPORT_STORE.saveToFile();
        });
  } else if (index == IDX_MEMOS_URL) {
    const std::string& current = EXPORT_STORE.memosUrl();
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_MEMOS_SERVER_URL),
                                                                   current.empty() ? "https://" : current,
                                                                   HighlightExportStore::MAX_URL_CHARS, InputType::Url),
                           [](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto& kb = std::get<KeyboardResult>(result.data);
                             EXPORT_STORE.setMemosUrl((kb.text == "https://" || kb.text == "http://") ? "" : kb.text);
                             EXPORT_STORE.saveToFile();
                           });
  } else if (index == IDX_MEMOS_VERIFY_CERT) {
    EXPORT_STORE.setMemosVerifyCert(!EXPORT_STORE.memosVerifyCert());
    EXPORT_STORE.saveToFile();
    requestUpdate();
  }
}

void HighlightExportSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  rowValues_[IDX_READWISE_TOKEN] = EXPORT_STORE.readwiseToken().empty() ? tr(STR_NOT_SET) : tr(STR_VALUE_SET);
  rowValues_[IDX_MEMOS_URL] = EXPORT_STORE.memosUrl().empty() ? tr(STR_NOT_SET) : EXPORT_STORE.memosUrl();
  rowValues_[IDX_MEMOS_TOKEN] = EXPORT_STORE.memosToken().empty() ? tr(STR_NOT_SET) : tr(STR_VALUE_SET);
  rowValues_[IDX_MEMOS_VERIFY_CERT].clear();
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }
  GUI.setCheckboxRow(rowItems_[IDX_MEMOS_VERIFY_CERT], EXPORT_STORE.memosVerifyCert());

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
