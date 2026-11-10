#include "WikipediaLookupActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "network/WikipediaClient.h"

namespace {
constexpr unsigned long MESSAGE_MS = 3000;
}  // namespace

void WikipediaLookupActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // Past this point the radio may have been on, so the way out is the restart in onExit().
  wifiUsed = true;
  if (WiFi.status() == WL_CONNECTED) {
    onWifiDone(true);
    return;
  }
  // The usual join screen: it connects to a saved network by itself, and with none saved it
  // lists the networks to pick from. Backing out of it goes back to the book.
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& wifi) { onWifiDone(!wifi.isCancelled); });
}

void WikipediaLookupActivity::onWifiDone(const bool connected) {
  if (!connected) {
    LOG_DBG("WIKI", "No Wi-Fi, back to the book");
    returnToReader();
    return;
  }
  {
    RenderLock lock(*this);
    state = State::Busy;
    message = StrId::STR_WIKI_SEARCHING;
  }
  fetchPending = true;
  requestUpdate();
}

void WikipediaLookupActivity::fetch() {
  requestUpdateAndWait();  // paint "Asking Wikipedia..." before the request blocks

  wikipedia::Summary summary;
  const wikipedia_client::Result result = wikipedia_client::fetch(language, text, summary);
  // The radio goes down now, whatever came back; the rest is reading.
  esp_wifi_stop();

  switch (result) {
    case wikipedia_client::Result::Ok:
      break;
    case wikipedia_client::Result::NotFound:
    case wikipedia_client::Result::NoTitle:
      showMessage(StrId::STR_WIKI_NOT_FOUND);
      return;
    case wikipedia_client::Result::Disambiguation:
      showMessage(StrId::STR_WIKI_DISAMBIGUATION);
      return;
    case wikipedia_client::Result::LowMemory:
      showMessage(StrId::STR_DICT_LOW_MEMORY);
      return;
    case wikipedia_client::Result::NoClock:
      showMessage(StrId::STR_WIKI_NO_CLOCK);
      return;
    case wikipedia_client::Result::Network:
      showMessage(StrId::STR_WIKI_NO_NETWORK);
      return;
    case wikipedia_client::Result::BadAnswer:
    default:
      showMessage(StrId::STR_WIKI_BAD_ANSWER);
      return;
  }

  if (summary.title.empty()) summary.title = text;
  auto definition = makeUniqueNoThrow<DictionaryDefinitionActivity>(
      renderer, mappedInput, std::move(summary.title), std::move(summary.extract), /*htmlDefinition=*/false);
  if (!definition) {
    LOG_ERR("WIKI", "OOM: definition activity");
    showMessage(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  {
    RenderLock lock(*this);
    state = State::Showing;
  }
  startActivityForResult(std::move(definition), [this](const ActivityResult&) { returnToReader(); });
}

void WikipediaLookupActivity::showMessage(const StrId id) {
  {
    RenderLock lock(*this);
    state = State::Message;
    message = id;
  }
  messageTime = millis();
  requestUpdate();
}

void WikipediaLookupActivity::returnToReader() { activityManager.goToReader(bookPath); }

void WikipediaLookupActivity::onExit() {
  Activity::onExit();
  if (wifiUsed) {
    // As after KOReader sync: drop the connection and restart into the book on a clean heap.
    WiFi.disconnect(false);
    delay(30);
    silentRestartToReader();
  }
}

void WikipediaLookupActivity::loop() {
  if (fetchPending) {
    fetchPending = false;
    fetch();
    return;
  }
  if (state != State::Message) return;
  if (millis() - messageTime >= MESSAGE_MS || mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    returnToReader();
  }
}

void WikipediaLookupActivity::render(RenderLock&&) {
  if (state == State::Showing) return;  // the definition screen is on top
  renderer.clearScreen();
  GUI.drawPopup(renderer, I18N.get(message));
}
