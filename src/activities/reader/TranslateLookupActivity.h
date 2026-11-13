#pragma once

#include <I18n.h>
#include <Translate.h>

#include <string>

#include "activities/Activity.h"

// Translates a selection online and shows the result as plain text on the dictionary's definition
// screen, with the name of the service under it. Built like WikipediaLookupActivity: it takes the
// reader's place so the Wi-Fi and TLS session have the reader's memory, and goes back to the book
// through the same silent restart.
//
// Before the first use of a service a question says what leaves the device (only the selected
// text and the two language codes) and where it goes; No goes back to the book with nothing sent
// and the radio never started. Wi-Fi comes up only after that, and it is stopped as soon as the
// answer is in (or has failed): no timer, nothing left running for sleep to find.
class TranslateLookupActivity final : public Activity {
 public:
  explicit TranslateLookupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                   translate::Pair pair, std::string text)
      : Activity("TranslateLookup", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        pair(std::move(pair)),
        text(std::move(text)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Busy; }

 private:
  enum class State : uint8_t { Asking, Busy, Showing, Message };

  void startWifi();
  void onWifiDone(bool connected);
  void fetch();
  void showMessage(StrId id);
  void returnToReader();

  const std::string bookPath;
  const translate::Pair pair;
  std::string text;
  State state = State::Asking;
  StrId message = StrId::STR_TRANSLATE_ASKING;
  unsigned long messageTime = 0;
  bool fetchPending = false;
  bool wifiUsed = false;
};
