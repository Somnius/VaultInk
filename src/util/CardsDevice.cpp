#include "CardsDevice.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <ctime>
#include <set>

#include "CrossPointSettings.h"
#include "TrustedTime.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

namespace cardsdev {

namespace {
// Days since 1970-01-01 of a calendar date (Howard Hinnant's algorithm).
int64_t daysFromCivil(int y, const unsigned m, const unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}
}  // namespace

uint32_t today() {
  const int64_t now = trustedtime::trustedNow();
  if (now <= 0) return 0;
  const time_t t = static_cast<time_t>(now);
  struct tm local{};
  localtime_r(&t, &local);  // the clock's time zone, so a day ends at local midnight
  return static_cast<uint32_t>(daysFromCivil(local.tm_year + 1900, static_cast<unsigned>(local.tm_mon + 1),
                                             static_cast<unsigned>(local.tm_mday)));
}

cardstore::StoreFs storeFs() {
  const auto fs = vaultfiles::sdFs();
  cardstore::StoreFs out;
  out.lines = fs.lines;
  out.begin = fs.begin;
  out.append = fs.append;
  out.commit = fs.commit;
  out.appendTo = [](const std::string& path, std::string_view text) {
    HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (!file) return false;
    const bool ok = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
    file.close();
    return ok;
  };
  return out;
}

cardsched::Config config() {
  cardsched::Config c;
  c.maxInterval = static_cast<uint16_t>(std::max<int>(SETTINGS.cardsMaxIntervalMonths, 1) * 30);
  return c;
}

cardindex::SessionSpec sessionDefaults() {
  cardindex::SessionSpec spec;
  spec.newLimit = SETTINGS.cardsNewPerSession;
  spec.reviewLimit = SETTINGS.cardsMaxReviews;
  return spec;
}

void refresh(const GfxRenderer& renderer, const vaultfiles::Vault& vault) {
  const auto fs = vaultfiles::sdFs();
  const std::string index = vaultfiles::indexPathFor(vault.path);
  Rect popup{};
  bool shown = false;
  vaultindex::build(vault.path, index, fs, nullptr, [&](size_t done, size_t total) {
    delay(1);
    if (total == 0) return;
    if (!shown) {
      RenderLock lock;
      popup = GUI.drawPopup(renderer, tr(STR_UPDATING_SEARCH));
      shown = true;
    }
    if (done % 8 == 0 || done == total) {
      RenderLock lock;
      GUI.fillPopupProgress(renderer, popup, static_cast<int>(done * 100 / total));
    }
  });
  cardindex::BuildStats stats;
  const std::string cards = cardindex::cardIndexPathFor(vault.path);
  cardindex::build(vault.path, index, cards, TAG, fs, &stats);
  LOG_DBG("CRD", "%s: %u notes, %u read, %u cards", vault.name.c_str(), static_cast<unsigned>(stats.notes),
          static_cast<unsigned>(stats.rescanned), static_cast<unsigned>(stats.cards));

  cardstore::Store store(storeFs(), cardindex::storePathFor(vault.path));
  store.load();
  if (store.needsCompaction() && today() > 0) {
    std::set<uint64_t> present;
    cardindex::allIds(cards, fs, [&](uint64_t id) { present.insert(id); });
    store.compact(today(), [&](uint64_t id) { return present.count(id) > 0; });
  }
}

std::vector<cardindex::DeckInfo> decks(const vaultfiles::Vault& vault) {
  cardstore::Store store(storeFs(), cardindex::storePathFor(vault.path));
  store.load();
  return cardindex::listDecks(cardindex::cardIndexPathFor(vault.path), vaultfiles::sdFs(), store, today());
}

}  // namespace cardsdev
