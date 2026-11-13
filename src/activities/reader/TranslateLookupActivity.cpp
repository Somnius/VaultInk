#include "TranslateLookupActivity.h"

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
#include "TranslateStore.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "network/TranslateClient.h"

namespace {
constexpr unsigned long MESSAGE_MS = 3000;
}  // namespace

void TranslateLookupActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // What can be told without the network is told without it.
  if (translate::cleanQuery(text).empty()) {
    showMessage(text.empty() ? StrId::STR_TRANSLATE_NOT_FOUND : StrId::STR_TRANSLATE_TOO_LONG);
    return;
  }
  if (!pair.ok() || (!translate_client::usesOwnServer() && pair.source.empty())) {
    showMessage(StrId::STR_TRANSLATE_NO_PAIR);
    return;
  }
  if (TRANSLATE_STORE.noticeSeen()) {
    startWifi();
    return;
  }
  // Once per service: what is sent and where. Nothing has touched the radio yet.
  const bool ownServer = translate_client::usesOwnServer();
  startActivityForResult(std::make_unique<ConfirmationActivity>(
                             renderer, mappedInput, tr(STR_TRANSLATE_NOTICE_TITLE),
                             ownServer ? tr(STR_TRANSLATE_NOTICE_SERVER) : tr(STR_TRANSLATE_NOTICE_MYMEMORY)),
                         [this](const ActivityResult& answer) {
                           if (answer.isCancelled) {
                             returnToReader();
                             return;
                           }
                           TRANSLATE_STORE.setNoticeSeen();
                           TRANSLATE_STORE.saveToFile();
                           startWifi();
                         });
}

void TranslateLookupActivity::startWifi() {
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

void TranslateLookupActivity::onWifiDone(const bool connected) {
  if (!connected) {
    LOG_DBG("TRN", "No Wi-Fi, back to the book");
    returnToReader();
    return;
  }
  {
    RenderLock lock(*this);
    state = State::Busy;
    message = StrId::STR_TRANSLATE_ASKING;
  }
  fetchPending = true;
  requestUpdate();
}

void TranslateLookupActivity::fetch() {
  requestUpdateAndWait();  // paint "Translating..." before the request blocks

  const bool ownServer = translate_client::usesOwnServer();
  translate::Result result;
  const translate_client::Result outcome = translate_client::fetch(pair, text, result);
  // The radio goes down now, whatever came back; the rest is reading.
  esp_wifi_stop();

  switch (outcome) {
    case translate_client::Result::Ok:
      break;
    case translate_client::Result::NotFound:
      showMessage(StrId::STR_TRANSLATE_NOT_FOUND);
      return;
    case translate_client::Result::NoQuery:
      showMessage(StrId::STR_TRANSLATE_TOO_LONG);
      return;
    case translate_client::Result::NoPair:
      showMessage(StrId::STR_TRANSLATE_NO_PAIR);
      return;
    case translate_client::Result::BadLanguage:
      showMessage(StrId::STR_TRANSLATE_BAD_LANGUAGE);
      return;
    case translate_client::Result::Quota:
      showMessage(StrId::STR_TRANSLATE_QUOTA);
      return;
    case translate_client::Result::Refused:
      showMessage(StrId::STR_TRANSLATE_REFUSED);
      return;
    case translate_client::Result::BadServer:
      showMessage(StrId::STR_TRANSLATE_BAD_SERVER);
      return;
    case translate_client::Result::ServerError:
      showMessage(StrId::STR_SEND_HL_SERVER_ERROR);
      return;
    case translate_client::Result::LowMemory:
      showMessage(StrId::STR_DICT_LOW_MEMORY);
      return;
    case translate_client::Result::NoClock:
      showMessage(StrId::STR_REMOTE_ERR_CLOCK);
      return;
    case translate_client::Result::NoCa:
      showMessage(StrId::STR_REMOTE_ERR_CERT);
      return;
    case translate_client::Result::Network:
      showMessage(StrId::STR_TRANSLATE_NO_NETWORK);
      return;
    case translate_client::Result::BadAnswer:
    default:
      showMessage(StrId::STR_WIKI_BAD_ANSWER);
      return;
  }

  // The translation, then who made it and between which languages: "MyMemory (en > el)".
  std::string body = std::move(result.text);
  body += "\n\n";
  body += ownServer ? "LibreTranslate" : "MyMemory";
  body += " (";
  body += pair.source.empty() ? "auto" : pair.source;
  body += " > ";
  body += pair.target;
  body += ")";
  auto definition = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, translate::cleanQuery(text),
                                                                    std::move(body), /*htmlDefinition=*/false);
  if (!definition) {
    LOG_ERR("TRN", "OOM: definition activity");
    showMessage(StrId::STR_DICT_LOW_MEMORY);
    return;
  }
  {
    RenderLock lock(*this);
    state = State::Showing;
  }
  startActivityForResult(std::move(definition), [this](const ActivityResult&) { returnToReader(); });
}

void TranslateLookupActivity::showMessage(const StrId id) {
  {
    RenderLock lock(*this);
    state = State::Message;
    message = id;
  }
  messageTime = millis();
  requestUpdate();
}

void TranslateLookupActivity::returnToReader() { activityManager.goToReader(bookPath); }

void TranslateLookupActivity::onExit() {
  Activity::onExit();
  if (wifiUsed) {
    // As after a Wikipedia lookup: drop the connection and restart into the book on a clean heap.
    WiFi.disconnect(false);
    delay(30);
    silentRestartToReader();
  }
}

void TranslateLookupActivity::loop() {
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

void TranslateLookupActivity::render(RenderLock&&) {
  if (state == State::Showing || state == State::Asking) return;  // another screen is on top
  renderer.clearScreen();
  GUI.drawPopup(renderer, I18N.get(message));
}
