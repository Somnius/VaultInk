#include "BleSettingsActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/GestureLabels.h"

namespace fui = freeink::ui;

namespace {
enum class MenuAction : int16_t { Enable, KeyboardTyping, Scan, Paired, Mapping, Gamepad };
// The row after the gamepad's actions: clear every button.
constexpr int16_t PAD_RESET_ROW = gamepad::kLearnableCount;
constexpr bleturner::Action MAP_ACTIONS[] = {bleturner::Action::NextPage,    bleturner::Action::PrevPage,
                                             bleturner::Action::NextChapter, bleturner::Action::PrevChapter,
                                             bleturner::Action::Confirm,     bleturner::Action::Back,
                                             bleturner::Action::ReaderMenu,  bleturner::Action::SaveQuote};
constexpr StrId MAP_LABELS[] = {StrId::STR_NEXT_PAGE,       StrId::STR_PREV_PAGE,    StrId::STR_BT_NEXT_CHAPTER,
                                StrId::STR_BT_PREV_CHAPTER, StrId::STR_CONFIRM,      StrId::STR_BACK,
                                StrId::STR_READER_MENU,     StrId::STR_SAVE_CLIPPING};
static_assert(std::size(MAP_ACTIONS) == std::size(MAP_LABELS));
constexpr uint32_t SCAN_MS = 15000;
constexpr unsigned long CLEAR_HOLD_MS = 700;
}  // namespace

void BleSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  // A saved opt-in brings the radio up here, so scanning and pairing work at once.
  if (SETTINGS.ble.enabled) bleturner::switchOn();
  refresh();
  requestUpdate();
}

void BleSettingsActivity::loop() {
  // Holding Confirm on an action clears it (touch: a long press on the row, onRowLongPress).
  if (view == View::Mapping && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, CLEAR_HOLD_MS)) {
    clearMapping(nav.selected);
  } else if (view == View::Gamepad && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, CLEAR_HOLD_MS)) {
    clearPadButton(nav.selected);
  }
  UiListActivity::loop();
  if (millis() - lastPollMs < 250) return;
  lastPollMs = millis();
  // A remote being paired that links becomes the chosen one: save it.
  if (SETTINGS.ble.enabled && bleturner::service()) SETTINGS.saveToFile();
  if (learnAction != bleturner::Action::None) stepLearning();
  if (padLearn != 0) stepPadLearning();
  if (signature() == shownSignature) return;  // an e-ink repaint only when something changed
  refresh();
  requestUpdate();
}

const char* BleSettingsActivity::headerTitle() const {
  switch (view) {
    case View::Menu:
      return tr(STR_BT_PAGE_TURNER);
    case View::Scan:
      return tr(STR_BT_SCAN);
    case View::Paired:
      return tr(STR_BT_PAIRED_REMOTES);
    case View::Mapping:
      return tr(STR_BT_MAP_BUTTONS);
    case View::Gamepad:
      return tr(STR_GAMEPAD_BUTTONS);
  }
  return tr(STR_BT_PAGE_TURNER);
}

void BleSettingsActivity::showView(const View target) {
  {
    RenderLock lock(*this);
    view = target;
    learnAction = bleturner::Action::None;
    padLearn = 0;
    learnNote = nullptr;
    nav.reset();
  }
  refresh();
  requestUpdate();
}

void BleSettingsActivity::onBackButton() {
  if (learning()) {
    learnAction = bleturner::Action::None;
    padLearn = 0;
    learnNote = nullptr;
    refresh();
    requestUpdate();
    return;
  }
  if (view == View::Menu) {
    finish();
    return;
  }
  if (view == View::Scan) bleturner::scan(0);
  showView(View::Menu);
}

uint32_t BleSettingsActivity::signature() const {
  const auto st = bleturner::status();
  return (st.running ? 1u : 0u) | (st.scanning ? 2u : 0u) | (st.connected ? 4u : 0u) | (st.connecting ? 8u : 0u) |
         (st.stopping ? 16u : 0u) | (SETTINGS.ble.enabled ? 32u : 0u) |
         static_cast<uint32_t>(bleturner::bondCount()) << 8 | static_cast<uint32_t>(bleturner::foundCount()) << 16;
}

void BleSettingsActivity::refresh() {
  RenderLock lock(*this);
  shownSignature = signature();
  const auto st = bleturner::status();
  char failure[48];
  if (!SETTINGS.ble.enabled || st.stopping) {
    status = tr(STR_STATE_OFF);
  } else if (bleturner::takeConnectFailure(failure, sizeof(failure))) {
    status = failure;
  } else if (!st.running) {
    status = tr(STR_BT_START_FAILED);
  } else if (st.connected) {
    status = bleturner::linked().name;
  } else if (st.connecting) {
    status = tr(STR_CONNECTING);
  } else if (st.scanning) {
    status = tr(STR_SCANNING);
  } else {
    status = tr(STR_STATE_ON);
  }

  // Labels first: the rows point into them.
  const uint8_t found = st.running ? bleturner::foundCount() : 0;
  const uint8_t bonds = st.running ? bleturner::bondCount() : 0;
  labels.clear();
  for (uint8_t i = 0; i < found; ++i) labels.emplace_back(bleturner::found(i).name);
  for (uint8_t i = 0; i < bonds; ++i) {
    const auto peer = bleturner::bond(i);
    labels.emplace_back(peer.name[0] != '\0' ? peer.name : peer.addr);
  }

  rows.clear();
  const auto add = [this](const char* label, const int16_t code, const bool enabled = true) -> fui::ListItem& {
    fui::ListItem row;
    row.label = label;
    row.actionValue = code;
    row.enabled = enabled;
    rows.push_back(row);
    return rows.back();
  };
  switch (view) {
    case View::Menu: {
      auto& enable = add(tr(STR_BT_PAGE_TURNER), static_cast<int16_t>(MenuAction::Enable));
      enable.subtitle = status.c_str();
      enable.toggle = true;
      enable.toggleChecked = SETTINGS.ble.enabled != 0;
      // A paired keyboard types into the text-entry screens (note editor line, search). Off by default: the
      // page turner then behaves as before.
      auto& typing =
          add(tr(STR_BLE_KEYBOARD_TYPING), static_cast<int16_t>(MenuAction::KeyboardTyping), SETTINGS.ble.enabled != 0);
      typing.toggle = true;
      typing.toggleChecked = SETTINGS.ble.keyboardTyping != 0;
      add(tr(STR_BT_SCAN), static_cast<int16_t>(MenuAction::Scan), st.running);
      add(tr(STR_BT_PAIRED_REMOTES), static_cast<int16_t>(MenuAction::Paired), st.running);
      auto& mapping = add(tr(STR_BT_MAP_BUTTONS), static_cast<int16_t>(MenuAction::Mapping), st.connected);
      if (!st.connected) mapping.subtitle = tr(STR_BT_CONNECT_REMOTE);
      // Gamepad scaffold: buttons only, never tried with a real pad (docs/gamepad.md).
      auto& pad = add(tr(STR_GAMEPAD_BUTTONS), static_cast<int16_t>(MenuAction::Gamepad), st.connected);
      pad.subtitle = st.connected ? tr(STR_GAMEPAD_EXPERIMENTAL) : tr(STR_BT_CONNECT_REMOTE);
      break;
    }
    case View::Scan:
      add(st.scanning ? tr(STR_BT_STOP_SCAN) : tr(STR_BT_SCAN), -1, st.running);
      for (uint8_t i = 0; i < found; ++i) add(labels[i].c_str(), i);
      if (found == 0) add(st.scanning ? tr(STR_SCANNING) : tr(STR_BT_NO_DEVICES), -1, false);
      break;
    case View::Paired:
      for (uint8_t i = 0; i < bonds; ++i) {
        auto& row = add(labels[found + i].c_str(), i);
        row.value = tr(STR_FORGET_BUTTON);
      }
      if (bonds == 0) add(tr(STR_BT_NO_REMOTES), -1, false);
      break;
    case View::Mapping: {
      const auto peer = bleturner::linked();
      const auto* table = bleturner::tableFor(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
      for (uint8_t i = 0; i < std::size(MAP_ACTIONS); ++i) {
        auto& row = add(I18N.get(MAP_LABELS[i]), i, st.connected && learnAction == bleturner::Action::None);
        bleturner::Binding binding;
        if (table && bleturner::find(*table, MAP_ACTIONS[i], binding)) {
          bleturner::formatCode(mappingValues[i], sizeof(mappingValues[i]), binding);
          if (binding & bleturner::kHoldBit) {
            const size_t used = strlen(mappingValues[i]);
            snprintf(mappingValues[i] + used, sizeof(mappingValues[i]) - used, " %s", tr(STR_BT_HOLD));
          }
          row.value = mappingValues[i];
        } else {
          row.value = tr(STR_NOT_SET);
        }
        if (!st.connected)
          row.subtitle = tr(STR_BT_CONNECT_REMOTE);
        else if (MAP_ACTIONS[i] == learnAction)
          row.subtitle = tr(STR_BT_PRESS_REMOTE);
        else if (i == 0)
          row.subtitle = learnNote;
      }
      break;
    }
    case View::Gamepad: {
      // The map belongs to one pad: another pad's buttons are not shown as this one's, and learning a button
      // of this pad starts a new map (the first row says so).
      const bool mine = gamepad::sameAddr(SETTINGS.gamepad.addr, bleturner::linked().addr);
      const bool others = !mine && SETTINGS.gamepad.count > 0;
      for (int i = 0; i < gamepad::kLearnableCount; ++i) {
        const uint8_t action = gamepad::kLearnable[i];
        auto& row = add(I18N.get(gesturelabels::action(action)), static_cast<int16_t>(i), st.connected && !learning());
        const uint32_t button = mine ? gamepad::buttonFor(SETTINGS.gamepad, action) : 0;
        if (button != 0) {
          gamepad::formatCode(padValues[i], sizeof(padValues[i]), button);
          row.value = padValues[i];
        } else {
          row.value = tr(STR_NOT_SET);
        }
        if (!st.connected)
          row.subtitle = tr(STR_BT_CONNECT_REMOTE);
        else if (action == padLearn)
          row.subtitle = tr(STR_BT_PRESS_REMOTE);
        else if (i == 0)
          row.subtitle = learnNote != nullptr ? learnNote : others ? tr(STR_GAMEPAD_OTHER_PAD) : nullptr;
      }
      add(tr(STR_GAMEPAD_RESET), PAD_RESET_ROW, SETTINGS.gamepad.count > 0 && !learning());
      break;
    }
  }
  if (nav.selected >= static_cast<int>(rows.size())) nav.selected = static_cast<int>(rows.size()) - 1;
}

void BleSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[index].enabled) return;
  nav.selected = index;
  app.clearTapFlash();
  const int16_t code = rows[index].actionValue;
  switch (view) {
    case View::Menu:
      switch (static_cast<MenuAction>(code)) {
        case MenuAction::Enable:
          SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
          if (SETTINGS.ble.enabled)
            bleturner::switchOn();
          else
            bleturner::switchOff();
          SETTINGS.saveToFile();
          break;
        case MenuAction::KeyboardTyping:
          SETTINGS.ble.keyboardTyping = SETTINGS.ble.keyboardTyping ? 0 : 1;
          SETTINGS.saveToFile();
          break;
        case MenuAction::Scan:
          bleturner::scan(SCAN_MS);
          showView(View::Scan);
          return;
        case MenuAction::Paired:
          showView(View::Paired);
          return;
        case MenuAction::Mapping:
          showView(View::Mapping);
          return;
        case MenuAction::Gamepad:
          showView(View::Gamepad);
          return;
      }
      break;
    case View::Scan:
      if (code < 0) {
        bleturner::scan(bleturner::status().scanning ? 0 : SCAN_MS);
      } else {
        const auto peer = bleturner::found(static_cast<uint8_t>(code));
        const std::string addr = peer.addr;
        const std::string name = peer.name;
        bleturner::scan(0);
        bleturner::pair(addr.c_str(), name.c_str());
        showView(View::Menu);
        return;
      }
      break;
    case View::Paired: {
      const std::string addr = bleturner::bond(static_cast<uint8_t>(code)).addr;
      bool changed = bleturner::forget(addr.c_str());
      // A forgotten gamepad takes its button map with it.
      if (gamepad::sameAddr(SETTINGS.gamepad.addr, addr.c_str())) {
        gamepad::reset(SETTINGS.gamepad);
        changed = true;
      }
      if (changed) SETTINGS.saveToFile();
      break;
    }
    case View::Mapping:
      startLearning(code);
      break;
    case View::Gamepad:
      if (code == PAD_RESET_ROW) {
        gamepad::reset(SETTINGS.gamepad);
        SETTINGS.saveToFile();
        learnNote = tr(STR_BT_MAPPING_CLEARED);
      } else {
        startPadLearning(code);
      }
      break;
  }
  refresh();
  requestUpdate();
}

void BleSettingsActivity::startLearning(const int index) {
  if (index < 0 || index >= static_cast<int>(std::size(MAP_ACTIONS)) || !bleturner::status().connected) return;
  bleturner::Event ev;
  while (bleturner::pollEvent(ev)) {
  }  // presses from before the row was chosen are not the answer
  learnAction = MAP_ACTIONS[index];
  const auto peer = bleturner::linked();
  strncpy(learnAddr, peer.addr, sizeof(learnAddr) - 1);
  learnCode = 0;
  learnSinceMs = millis();
  learnNote = nullptr;
}

void BleSettingsActivity::stepLearning() {
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_CONNECT_REMOTE);
    refresh();
    requestUpdate();
    return;
  }
  const auto action = learnAction;
  bleturner::Event ev;
  while (learnAction == action && bleturner::pollEvent(ev)) {
    if (ev.pressed) {
      learnCode = ev.code;
      learnPressMs = ev.atMs;
      learnSinceMs = millis();
    } else if (learnCode != 0 && ev.code == learnCode) {
      if (ev.wasRest) {
        learnCode = 0;  // the remote idling on a non-zero byte, not a button
      } else {
        // A button held past the reader's hold threshold is mapped as a hold, apart from its tap.
        keepLearned(ev.atMs - learnPressMs >= bleturner::kHoldMs);
      }
    }
  }
  if (learnAction == action) {
    // Allow the full learning window for a hold before falling back to a no-release tap.
    if (learnCode != 0 && millis() - learnSinceMs >= bleturner::kWaitMs) {
      keepLearned(false);
    } else if (learnCode == 0 && millis() - learnSinceMs >= bleturner::kWaitMs) {
      learnAction = bleturner::Action::None;
      learnNote = tr(STR_BT_NO_KEY);
    }
  }
  if (learnAction != action) {
    refresh();
    requestUpdate();
  }
}

void BleSettingsActivity::keepLearned(const bool hold) {
  // Preserve the remote's other bindings when changing the selected action.
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_CONNECT_REMOTE);
    return;
  }
  bleturner::RemoteTable* table =
      bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
  if (table == nullptr || !bleturner::learn(*table, learnAction, learnCode, hold)) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_KEYS_NOT_SAVED);
    return;
  }
  SETTINGS.saveToFile();
  learnCode = 0;
  learnSinceMs = millis();
  learnAction = bleturner::Action::None;
  learnNote = tr(STR_BT_MAPPING_SAVED);
}

void BleSettingsActivity::startPadLearning(const int index) {
  if (index < 0 || index >= gamepad::kLearnableCount || !bleturner::status().connected) return;
  bleturner::Event ev;
  while (bleturner::pollEvent(ev)) {
  }  // presses from before the row was chosen are not the answer
  padLearn = gamepad::kLearnable[index];
  padListener = gamepad::Listener{};
  const auto peer = bleturner::linked();
  strncpy(learnAddr, peer.addr, sizeof(learnAddr) - 1);
  learnSinceMs = millis();
  learnNote = nullptr;
}

void BleSettingsActivity::stepPadLearning() {
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    padLearn = 0;
    learnNote = tr(STR_BT_CONNECT_REMOTE);
    refresh();
    requestUpdate();
    return;
  }
  const uint8_t action = padLearn;
  bleturner::Event ev;
  while (padLearn == action && bleturner::pollEvent(ev)) {
    // A press restarts the wait, so a button held for a while is not cut off by it.
    if (ev.pressed) learnSinceMs = millis();
    const uint32_t button = gamepad::hear(padListener, ev.code, ev.pressed, ev.wasRest);
    if (button != 0) keepPadButton(button);
  }
  if (padLearn == action && millis() - learnSinceMs >= gamepad::kLearnWaitMs) {
    // A pad that never reports the release still gets its button; nothing heard at all ends the wait.
    const uint32_t button = gamepad::heardAtTimeout(padListener);
    if (button != 0) {
      keepPadButton(button);
    } else {
      padLearn = 0;
      learnNote = tr(STR_BT_NO_KEY);
    }
  }
  if (padLearn != action) {
    refresh();
    requestUpdate();
  }
}

void BleSettingsActivity::keepPadButton(const uint32_t code) {
  const uint8_t action = padLearn;
  padLearn = 0;
  switch (gamepad::learn(SETTINGS.gamepad, learnAddr, code, action)) {
    case gamepad::Learned::Saved:
    case gamepad::Learned::NewPad:
      learnNote = tr(STR_BT_MAPPING_SAVED);
      break;
    case gamepad::Learned::Moved:
      // The button did something else before: say that the other action lost it.
      learnNote = tr(STR_GAMEPAD_MOVED);
      break;
    case gamepad::Learned::Refused:
    default:
      learnNote = tr(STR_BT_KEYS_NOT_SAVED);
      return;
  }
  SETTINGS.saveToFile();
}

void BleSettingsActivity::clearPadButton(const int index) {
  if (index < 0 || index >= gamepad::kLearnableCount || learning()) return;
  if (!gamepad::sameAddr(SETTINGS.gamepad.addr, bleturner::linked().addr)) return;  // another pad's map: not shown
  if (!gamepad::clear(SETTINGS.gamepad, gamepad::kLearnable[index])) return;
  SETTINGS.saveToFile();
  learnNote = tr(STR_BT_MAPPING_CLEARED);
  refresh();
  requestUpdate();
}

void BleSettingsActivity::onRowLongPress(const int index) {
  if (view == View::Mapping) clearMapping(index);
  if (view == View::Gamepad) clearPadButton(index);
}

void BleSettingsActivity::clearMapping(const int index) {
  if (index < 0 || index >= static_cast<int>(std::size(MAP_ACTIONS)) || learnAction != bleturner::Action::None) return;
  const auto action = MAP_ACTIONS[index];
  // The linked remote's slot for this action goes; a built-in default is copied out first, so it
  // does not come back. The page rows also drop the usage code learned before per-remote tables.
  if (bleturner::status().connected) {
    const auto peer = bleturner::linked();
    auto* table = bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
    if (table != nullptr) bleturner::clearAction(*table, action);
  }
  if (action == bleturner::Action::NextPage) SETTINGS.ble.nextKeyUsage = bleturner::kUsageNone;
  if (action == bleturner::Action::PrevPage) SETTINGS.ble.prevKeyUsage = bleturner::kUsageNone;
  SETTINGS.saveToFile();
  learnNote = tr(STR_BT_MAPPING_CLEARED);
  refresh();
  requestUpdate();
}

void BleSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.rowProvider = [](void* ctx, const uint16_t index, fui::ListItem& item) {
    item = static_cast<BleSettingsActivity*>(ctx)->rows[index];
    // UiListActivity dispatches row indices; keep Bluetooth action codes in rows.
    item.actionValue = static_cast<int16_t>(index);
  };
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
