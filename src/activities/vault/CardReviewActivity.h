#pragma once

#include <CardIndex.h>
#include <CardStore.h>

#include <deque>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/VaultFiles.h"

// Reviews flashcards (Phase 10): the front, a tap or Confirm shows the back, then four answers
// (Again, Hard, Good, Easy) by touch or by the buttons: Left = Again, Up = Hard, Down/Confirm =
// Good, Right = Easy. Each answer is saved straight away, so leaving early loses nothing.
class CardReviewActivity final : public Activity {
 public:
  CardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, vaultfiles::Vault vault,
                     cardindex::SessionSpec spec, std::string title);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int ANSWERS = 4;

  bool answerRect(int answer, int& x, int& y, int& w, int& h) const;
  void answer(cardsched::Answer a);
  void showNext();
  void drawCard();
  void drawSummary() const;
  void drawText(const std::string& markdown, int top, int bottom) const;
  void intervalLabel(cardsched::Answer a, char* out, size_t size) const;

  vaultfiles::Vault vault;
  cardindex::SessionSpec spec;
  std::string title;
  std::vector<cardindex::SessionCard> cards;
  std::deque<size_t> queue;
  cardstore::Store store;
  uint32_t day = 0;
  bool revealed = false;
  bool finished = false;
  bool clockUnset = false;
  unsigned shown = 0;  // cards drawn, for the refresh policy
  unsigned reviewed = 0;
  unsigned good = 0;
  unsigned again = 0;
  size_t total = 0;
  size_t firstAnswers = 0;         // cards answered at least once, for the "3/24" counter
  std::vector<bool> answeredOnce;  // by index into `cards`
};
