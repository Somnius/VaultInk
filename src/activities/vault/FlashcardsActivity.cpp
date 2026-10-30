#include "FlashcardsActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>

#include "CardReviewActivity.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/CardsDevice.h"

namespace fui = freeink::ui;

FlashcardsActivity::FlashcardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Flashcards", renderer, mappedInput) {}

const char* FlashcardsActivity::headerTitle() const {
  return openVault >= 0 ? vaults[static_cast<size_t>(openVault)].vault.name.c_str() : tr(STR_FLASHCARDS);
}

void FlashcardsActivity::onEnter() {
  UiListActivity::onEnter();
  clockUnset = cardsdev::today() == 0;
  load();
  openVault = vaults.size() == 1 ? 0 : -1;
  buildRows();
  requestUpdate();
}

// Updates every vault's indexes and counts the cards; only vaults that have any are kept.
void FlashcardsActivity::load() {
  vaults.clear();
  for (const auto& v : vaultfiles::findVaults()) {
    cardsdev::refresh(renderer, v);
    VaultCards entry;
    entry.vault = v;
    entry.decks = cardsdev::decks(v);
    for (const auto& d : entry.decks) {
      entry.cards += d.cards;
      entry.due += d.due;
      entry.fresh += d.fresh;
    }
    if (entry.cards > 0) vaults.push_back(std::move(entry));
  }
}

void FlashcardsActivity::buildRows() {
  labels.clear();
  values.clear();
  const auto due = [](size_t dueCount, size_t fresh, size_t cards) {
    char line[64];
    snprintf(line, sizeof(line), tr(STR_CARDS_DUE_LINE), static_cast<unsigned>(dueCount), static_cast<unsigned>(fresh),
             static_cast<unsigned>(cards));
    return std::string(line);
  };
  if (openVault < 0) {
    for (const auto& v : vaults) {
      labels.push_back(v.vault.name);
      values.push_back(due(v.due, v.fresh, v.cards));
    }
  } else {
    const auto& v = vaults[static_cast<size_t>(openVault)];
    if (v.decks.size() > 1) {
      labels.emplace_back(tr(STR_CARDS_STUDY_ALL));
      values.push_back(due(v.due, v.fresh, v.cards));
    }
    for (const auto& d : v.decks) {
      labels.push_back(d.name.empty() ? std::string(tr(STR_FLASHCARDS)) : d.name);
      values.push_back(due(d.due, d.fresh, d.cards));
    }
  }
  rowItems.assign(labels.size(), fui::ListItem{});
  for (size_t i = 0; i < rowItems.size(); ++i) {
    rowItems[i].label = labels[i].c_str();
    rowItems[i].subtitle = values[i].c_str();
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  nav.selected = 0;
}

void FlashcardsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (clockUnset || rowItems.empty()) {
    auto text = screen.theme().bodyText;
    text.align = fui::TextAlign::Center;
    text.maxLines = 6;
    const auto body = screen.body().inset(fui::Insets{40, 24, 0, 24});
    screen.target().text(body, clockUnset ? tr(STR_CARDS_CLOCK_UNSET) : tr(STR_CARDS_NONE), text);
    return;
  }
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

void FlashcardsActivity::startReview(const std::string& deck, const bool all) {
  const auto& v = vaults[static_cast<size_t>(openVault)];
  auto spec = cardsdev::sessionDefaults();
  spec.all = all;
  spec.deck = deck;
  auto review = makeUniqueNoThrow<CardReviewActivity>(renderer, mappedInput, v.vault, spec,
                                                      all ? std::string(tr(STR_CARDS_STUDY_ALL)) : deck);
  if (!review) {
    LOG_ERR("CRD", "OOM: CardReviewActivity");
    return;
  }
  startActivityForResult(std::move(review), [this](const ActivityResult&) {
    const int keep = openVault;
    load();  // the counts moved
    openVault = keep < static_cast<int>(vaults.size()) ? keep : -1;
    buildRows();
    requestUpdate();
  });
}

void FlashcardsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount() || clockUnset) return;
  app.clearTapFlash();
  if (openVault < 0) {
    openVault = index;
    buildRows();
    requestUpdate();
    return;
  }
  const auto& v = vaults[static_cast<size_t>(openVault)];
  const bool hasAll = v.decks.size() > 1;
  if (hasAll && index == 0) {
    startReview("", true);
    return;
  }
  startReview(v.decks[static_cast<size_t>(index - (hasAll ? 1 : 0))].name, false);
}

void FlashcardsActivity::onBackButton() {
  if (openVault >= 0 && vaults.size() > 1) {
    openVault = -1;
    buildRows();
    requestUpdate();
    return;
  }
  onGoHome();
}
