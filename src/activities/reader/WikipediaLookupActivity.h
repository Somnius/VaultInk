#pragma once

#include <I18n.h>

#include <string>

#include "activities/Activity.h"

// Looks a selection up on Wikipedia and shows the summary as plain text on the dictionary's
// definition screen. It takes the reader's place (like KOReader sync) so the Wi-Fi and TLS session
// have the reader's memory, and goes back to the book through the same silent restart.
//
// Wi-Fi comes up only here, on the user's tap, and the radio is stopped as soon as the answer is
// in (or has failed): no timer, nothing left running for sleep to find.
class WikipediaLookupActivity final : public Activity {
 public:
  explicit WikipediaLookupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                   std::string language, std::string text)
      : Activity("WikipediaLookup", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        language(std::move(language)),
        text(std::move(text)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Busy; }

 private:
  enum class State : uint8_t { Busy, Showing, Message };

  void onWifiDone(bool connected);
  void fetch();
  void showMessage(StrId id);
  void returnToReader();

  const std::string bookPath;
  const std::string language;
  std::string text;
  State state = State::Busy;
  StrId message = StrId::STR_WIKI_SEARCHING;
  unsigned long messageTime = 0;
  bool fetchPending = false;
  bool wifiUsed = false;
};
