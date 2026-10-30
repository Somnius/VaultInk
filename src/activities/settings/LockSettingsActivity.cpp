#include "LockSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/LockActivity.h"
#include "components/UITheme.h"
#include "util/ScreenLock.h"

namespace fui = freeink::ui;

namespace {
enum MenuItem { ITEM_TYPE = 0, ITEM_AFTER, ITEM_CORNER, ITEM_HOLD, ITEM_OWNER_NAME, ITEM_OWNER_PHONE };

const StrId menuNames[LockSettingsActivity::ITEM_COUNT] = {
    StrId::STR_LOCK_TYPE,         StrId::STR_LOCK_AFTER, StrId::STR_LOCK_CORNER,
    StrId::STR_LOCK_HOLD_SECONDS, StrId::STR_OWNER_NAME, StrId::STR_OWNER_PHONE,
};

constexpr int AFTER_COUNT = 8;
constexpr StrId afterNames[AFTER_COUNT] = {
    StrId::STR_LOCK_AFTER_0,  StrId::STR_LOCK_AFTER_5,  StrId::STR_LOCK_AFTER_10,  StrId::STR_LOCK_AFTER_15,
    StrId::STR_LOCK_AFTER_30, StrId::STR_LOCK_AFTER_60, StrId::STR_LOCK_AFTER_120, StrId::STR_LOCK_AFTER_180};
constexpr int CORNER_COUNT = 5;
constexpr StrId cornerNames[CORNER_COUNT] = {StrId::STR_LOCK_CORNER_ANY, StrId::STR_LOCK_CORNER_TL,
                                             StrId::STR_LOCK_CORNER_TR, StrId::STR_LOCK_CORNER_BL,
                                             StrId::STR_LOCK_CORNER_BR};
constexpr StrId typeNames[] = {StrId::STR_LOCK_MODE_OFF, StrId::STR_LOCK_MODE_PIN, StrId::STR_LOCK_MODE_CORNER,
                               StrId::STR_LOCK_CHANGE_PIN};
constexpr int HOLD_MAX = 10;
}  // namespace

LockSettingsActivity::LockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LockSettings", renderer, mappedInput) {}

void LockSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* LockSettingsActivity::headerTitle() const { return tr(STR_LOCK_SCREEN); }

bool LockSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void LockSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void LockSettingsActivity::editText(const StrId title, char* target, const size_t size) {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, I18N.get(title), std::string(target),
                                              static_cast<int>(size - 1), InputType::Text),
      [this, target, size](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& kb = std::get<KeyboardResult>(result.data);
          strncpy(target, kb.text.c_str(), size - 1);
          target[size - 1] = '\0';
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void LockSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  app.clearTapFlash();
  switch (index) {
    case ITEM_TYPE: {
      // Off, PIN or corner. Picking PIN asks for a new one; a PIN already set can be changed from
      // the fourth option.
      const int count = screenlock::hasPin() ? 4 : 3;
      optionPopup.show(StrId::STR_LOCK_TYPE, typeNames, count, SETTINGS.lockMode, [this](const int idx) {
        if (idx < 0) return;
        const bool needPin = (idx == 1 && !screenlock::hasPin()) || idx == 3;
        if (!needPin) {
          SETTINGS.lockMode = static_cast<uint8_t>(idx);
          if (idx == 0) screenlock::removePin();
          SETTINGS.saveToFile();
          screenlock::noteSleep();  // starts the wait from now, not from the last sleep
          requestUpdate();
          return;
        }
        auto setter = makeUniqueNoThrow<LockActivity>(renderer, mappedInput, LockActivity::Mode::SetPin);
        if (!setter) return;
        startActivityForResult(std::move(setter), [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            SETTINGS.lockMode = 1;
            SETTINGS.saveToFile();
            screenlock::noteSleep();
          }
          requestUpdate();
        });
      });
      requestUpdate();
      return;
    }
    case ITEM_AFTER:
      SETTINGS.lockAfter = static_cast<uint8_t>((SETTINGS.lockAfter + 1) % AFTER_COUNT);
      break;
    case ITEM_CORNER:
      SETTINGS.lockCorner = static_cast<uint8_t>((SETTINGS.lockCorner + 1) % CORNER_COUNT);
      break;
    case ITEM_HOLD:
      SETTINGS.lockHoldSeconds = static_cast<uint8_t>((SETTINGS.lockHoldSeconds + 1) % (HOLD_MAX + 1));
      break;
    case ITEM_OWNER_NAME:
      editText(StrId::STR_OWNER_NAME, SETTINGS.ownerName, sizeof(SETTINGS.ownerName));
      return;
    case ITEM_OWNER_PHONE:
      editText(StrId::STR_OWNER_PHONE, SETTINGS.ownerPhone, sizeof(SETTINGS.ownerPhone));
      return;
    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void LockSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  rowItems_[ITEM_TYPE].value = I18N.get(typeNames[SETTINGS.lockMode < 3 ? SETTINGS.lockMode : 0]);
  rowItems_[ITEM_AFTER].value = I18N.get(afterNames[SETTINGS.lockAfter < AFTER_COUNT ? SETTINGS.lockAfter : 0]);
  rowItems_[ITEM_CORNER].value = I18N.get(cornerNames[SETTINGS.lockCorner < CORNER_COUNT ? SETTINGS.lockCorner : 0]);
  snprintf(holdText_, sizeof(holdText_), "%u", static_cast<unsigned>(SETTINGS.lockHoldSeconds));
  rowItems_[ITEM_HOLD].value = holdText_;
  rowItems_[ITEM_OWNER_NAME].value = SETTINGS.ownerName[0] ? SETTINGS.ownerName : tr(STR_NOT_SET);
  rowItems_[ITEM_OWNER_PHONE].value = SETTINGS.ownerPhone[0] ? SETTINGS.ownerPhone : tr(STR_NOT_SET);

  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
