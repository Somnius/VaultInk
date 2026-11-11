#pragma once

#include <HighlightExport.h>

#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Sends the highlights (clippings) of one book to Readwise or to the user's Memos server.
//
// It takes the reader's place (like KOReader sync and the Wikipedia lookup) so Wi-Fi and TLS have
// the reader's memory, and goes back to the book through the same silent restart once the radio
// was used. Wi-Fi comes up only after the user picked what to send, and the radio is stopped as
// soon as the job ends or fails: no timer, nothing left running for sleep to find.
//
// What went out is remembered per book in /.vaultink/exports/<hash>.sent (lib/HighlightExport,
// SentMarkers), so sending again only sends what is new unless "Send all again" is picked.
class SendHighlightsActivity final : public Activity {
 public:
  explicit SendHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                  std::string title, std::string author, highlightexport::Destination destination)
      : Activity("SendHighlights", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        title(std::move(title)),
        author(std::move(author)),
        destination(destination) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Busy; }

 private:
  enum class State : uint8_t { Choosing, Wifi, Busy, Done };

  void startWifi();
  void onWifiDone(bool connected);
  void runJob();
  void showResult(const std::string& text);
  void returnToReader();
  bool loadMarkers();
  bool saveMarkers();
  std::string markerPath() const;
  uint64_t keyFor(size_t index) const;

  const std::string bookPath;
  const std::string title;
  const std::string author;
  const highlightexport::Destination destination;

  OptionPopup popup;
  highlightexport::SentMarkers markers;
  State state = State::Choosing;
  bool sendAll = false;
  bool jobPending = false;
  bool wifiUsed = false;
  bool markersDirty = false;
  bool leaving = false;
};
