#include "CardReviewActivity.h"

#include <Flashcards.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "fontIds.h"
#include "util/CardsDevice.h"

namespace {
constexpr int MARGIN = 24;
constexpr int FRONT_TOP = 80;
constexpr int FRONT_BOTTOM = 380;
constexpr int BACK_TOP = 420;
constexpr int BACK_BOTTOM = 680;
constexpr int BUTTON_TOP = 696;
constexpr int BUTTON_H = 76;
constexpr int BUTTON_GAP = 8;
constexpr int FULL_REFRESH_EVERY = 8;
}  // namespace

CardReviewActivity::CardReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, vaultfiles::Vault vault,
                                       cardindex::SessionSpec spec, std::string title)
    : Activity("CardReview", renderer, mappedInput),
      vault(std::move(vault)),
      spec(std::move(spec)),
      title(std::move(title)),
      store(cardsdev::storeFs(), cardindex::storePathFor(this->vault.path)) {}

void CardReviewActivity::onEnter() {
  Activity::onEnter();
  day = cardsdev::today();
  clockUnset = day == 0;
  if (!clockUnset) {
    store.load();
    cards = cardindex::collectSession(cardindex::cardIndexPathFor(vault.path), vaultfiles::sdFs(), store, day, spec);
    for (size_t i = 0; i < cards.size(); ++i) queue.push_back(i);
    total = cards.size();
    answeredOnce.assign(cards.size(), false);
  }
  finished = clockUnset || queue.empty();
  requestUpdate(true);
}

bool CardReviewActivity::answerRect(const int a, int& x, int& y, int& w, int& h) const {
  if (a < 0 || a >= ANSWERS) return false;
  const int width = renderer.getScreenWidth() - 2 * 16;
  w = (width - (ANSWERS - 1) * BUTTON_GAP) / ANSWERS;
  h = BUTTON_H;
  x = 16 + a * (w + BUTTON_GAP);
  y = BUTTON_TOP;
  return true;
}

void CardReviewActivity::intervalLabel(const cardsched::Answer a, char* out, const size_t size) const {
  const auto& card = cards[queue.front()];
  const uint32_t days = cardsched::daysUntilDue(card.state, a, day, cardsdev::config(), static_cast<uint32_t>(card.id));
  if (days == 0) {
    snprintf(out, size, "<1d");
  } else if (days < 30) {
    snprintf(out, size, "%ud", static_cast<unsigned>(days));
  } else if (days < 365) {
    snprintf(out, size, "%umo", static_cast<unsigned>(days / 30));
  } else {
    snprintf(out, size, "%uy", static_cast<unsigned>(days / 365));
  }
}

void CardReviewActivity::answer(const cardsched::Answer a) {
  if (queue.empty()) return;
  const size_t index = queue.front();
  queue.pop_front();
  auto& card = cards[index];
  const bool wasNew = card.state.isNew();
  card.state = cardsched::review(card.state, a, day, cardsdev::config(), static_cast<uint32_t>(card.id));
  if (!store.save(card.id, card.state)) LOG_ERR("CRD", "Could not save a card's state");
  ++reviewed;
  if (!answeredOnce[index]) {
    answeredOnce[index] = true;
    ++firstAnswers;
  }
  if (a == cardsched::Answer::Again) {
    ++again;
    queue.push_back(index);  // shows again before the session ends
  } else if (a == cardsched::Answer::Good || a == cardsched::Answer::Easy) {
    ++good;
  }
  (void)wasNew;
  showNext();
}

void CardReviewActivity::showNext() {
  revealed = false;
  finished = queue.empty();
  requestUpdate();
}

void CardReviewActivity::loop() {
  using cardsched::Answer;
  if (finished) {
    int x, y;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finished = true;  // the summary of what was done so far
    requestUpdate(true);
    return;
  }
  int x = 0;
  int y = 0;
  const bool tapped = mappedInput.wasScreenTapped(x, y);
  if (!revealed) {
    if (tapped || mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      revealed = true;
      requestUpdate();
    }
    return;
  }
  if (tapped) {
    for (int a = 0; a < ANSWERS; ++a) {
      int bx, by, bw, bh;
      if (answerRect(a, bx, by, bw, bh) && x >= bx && x < bx + bw && y >= by && y < by + bh) {
        answer(static_cast<Answer>(a));
        return;
      }
    }
    return;
  }
  using B = MappedInputManager::Button;
  if (mappedInput.wasPressed(B::Left)) {
    answer(Answer::Again);
  } else if (mappedInput.wasPressed(B::Up)) {
    answer(Answer::Hard);
  } else if (mappedInput.wasPressed(B::Down) || mappedInput.wasPressed(B::Confirm)) {
    answer(Answer::Good);
  } else if (mappedInput.wasPressed(B::Right)) {
    answer(Answer::Easy);
  }
}

// Wrapped, centred text between two heights; the smaller font when the first one doesn't fit.
void CardReviewActivity::drawText(const std::string& markdown, const int top, const int bottom) const {
  const std::string text = flashcards::plainText(markdown);
  const int width = renderer.getScreenWidth() - 2 * MARGIN;
  for (const int font : {UI_12_FONT_ID, UI_10_FONT_ID}) {
    const int lineH = renderer.getLineHeight(font) + 4;
    const int maxLines = (bottom - top) / lineH;
    const auto lines = renderer.wrappedText(font, text.c_str(), width, maxLines);
    const bool cut = static_cast<int>(lines.size()) >= maxLines && !lines.empty() && lines.back().size() >= 3 &&
                     lines.back().compare(lines.back().size() - 3, 3, "\xE2\x80\xA6") == 0;
    if (cut && font == UI_12_FONT_ID) continue;
    int y = top + std::max(0, (bottom - top - static_cast<int>(lines.size()) * lineH) / 2);
    for (const auto& line : lines) {
      renderer.drawCenteredText(font, y, line.c_str());
      y += lineH;
    }
    return;
  }
}

void CardReviewActivity::drawCard() {
  const auto& card = cards[queue.front()];
  char head[48];
  snprintf(head, sizeof(head), "%u/%u", static_cast<unsigned>(firstAnswers + 1 > total ? total : firstAnswers + 1),
           static_cast<unsigned>(total));
  renderer.drawText(SMALL_FONT_ID, MARGIN, 24, head);
  if (!title.empty()) {
    const int w = renderer.getTextWidth(SMALL_FONT_ID, title.c_str());
    renderer.drawText(SMALL_FONT_ID, renderer.getScreenWidth() - MARGIN - w, 24, title.c_str());
  }
  renderer.drawLine(MARGIN, 56, renderer.getScreenWidth() - MARGIN, 56);
  drawText(card.front, FRONT_TOP, FRONT_BOTTOM);
  if (!revealed) {
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - 60, tr(STR_CARD_REVEAL));
    return;
  }
  renderer.drawLine(MARGIN, BACK_TOP - 20, renderer.getScreenWidth() - MARGIN, BACK_TOP - 20);
  drawText(card.back, BACK_TOP, BACK_BOTTOM);
  static constexpr StrId NAMES[ANSWERS] = {StrId::STR_CARD_AGAIN, StrId::STR_CARD_HARD, StrId::STR_CARD_GOOD,
                                           StrId::STR_CARD_EASY};
  for (int a = 0; a < ANSWERS; ++a) {
    int x, y, w, h;
    answerRect(a, x, y, w, h);
    renderer.drawRoundedRect(x, y, w, h, 2, 12, true);
    const char* name = I18N.get(NAMES[a]);
    renderer.drawText(UI_10_FONT_ID, x + (w - renderer.getTextWidth(UI_10_FONT_ID, name, EpdFontFamily::BOLD)) / 2,
                      y + 10, name, true, EpdFontFamily::BOLD);
    char label[8];
    intervalLabel(static_cast<cardsched::Answer>(a), label, sizeof(label));
    renderer.drawText(SMALL_FONT_ID, x + (w - renderer.getTextWidth(SMALL_FONT_ID, label)) / 2, y + 44, label);
  }
}

void CardReviewActivity::drawSummary() const {
  const int h = renderer.getScreenHeight();
  if (clockUnset) {
    renderer.drawCenteredText(UI_10_FONT_ID, h / 2 - 20, tr(STR_CARDS_CLOCK_UNSET));
    return;
  }
  if (cards.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 20, tr(STR_CARDS_NOTHING_DUE), true, EpdFontFamily::BOLD);
    return;
  }
  renderer.drawCenteredText(UI_12_FONT_ID, h / 2 - 50, tr(STR_CARDS_DONE), true, EpdFontFamily::BOLD);
  char line[80];
  snprintf(line, sizeof(line), tr(STR_CARDS_SUMMARY), reviewed, good, again);
  renderer.drawCenteredText(UI_10_FONT_ID, h / 2, line);
}

void CardReviewActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (finished) {
    drawSummary();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  drawCard();
  ++shown;
  renderer.displayBuffer(shown % FULL_REFRESH_EVERY == 0 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
