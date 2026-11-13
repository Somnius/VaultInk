#include "TranslateSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "TranslateStore.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr int IDX_SERVER_URL = 0;
constexpr int IDX_API_KEY = 1;
constexpr int IDX_VERIFY_CERT = 2;

constexpr StrId menuNames[TranslateSettingsActivity::MENU_ITEMS] = {
    StrId::STR_TRANSLATE_SERVER_URL, StrId::STR_TRANSLATE_API_KEY, StrId::STR_TRANSLATE_CERT_CHECK};
}  // namespace

TranslateSettingsActivity::TranslateSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TranslateSettings", renderer, mappedInput) {
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int TranslateSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* TranslateSettingsActivity::headerTitle() const { return tr(STR_TRANSLATION); }

void TranslateSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == IDX_API_KEY) {
    // The keyboard starts empty: the stored key is never shown again.
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRANSLATE_API_KEY), "",
                                                                   TranslateStore::MAX_KEY_CHARS, InputType::Password),
                           [](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto& kb = std::get<KeyboardResult>(result.data);
                             TRANSLATE_STORE.setApiKey(kb.text);
                             TRANSLATE_STORE.saveToFile();
                           });
  } else if (index == IDX_SERVER_URL) {
    const std::string& current = TRANSLATE_STORE.serverUrl();
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRANSLATE_SERVER_URL),
                                                current.empty() ? "https://" : current, TranslateStore::MAX_URL_CHARS,
                                                InputType::Url),
        [](const ActivityResult& result) {
          if (result.isCancelled) return;
          const auto& kb = std::get<KeyboardResult>(result.data);
          // The bare prefix means "no server": MyMemory is asked again.
          TRANSLATE_STORE.setServerUrl((kb.text == "https://" || kb.text == "http://") ? "" : kb.text);
          TRANSLATE_STORE.saveToFile();
        });
  } else if (index == IDX_VERIFY_CERT) {
    TRANSLATE_STORE.setVerifyCert(!TRANSLATE_STORE.verifyCert());
    TRANSLATE_STORE.saveToFile();
    requestUpdate();
  }
}

void TranslateSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // No server of your own: the service that is asked is named where the address would be.
  rowValues_[IDX_SERVER_URL] = TRANSLATE_STORE.hasServer() ? TRANSLATE_STORE.serverUrl() : "MyMemory";
  rowValues_[IDX_API_KEY] = TRANSLATE_STORE.apiKey().empty() ? tr(STR_NOT_SET) : tr(STR_VALUE_SET);
  rowValues_[IDX_VERIFY_CERT].clear();
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }
  GUI.setCheckboxRow(rowItems_[IDX_VERIFY_CERT], TRANSLATE_STORE.verifyCert());

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
