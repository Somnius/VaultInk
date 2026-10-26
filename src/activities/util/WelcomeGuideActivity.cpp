#include "WelcomeGuideActivity.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/customListIcons.h"
#include "components/icons/listIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_NEXT = 1;
constexpr fui::ActionId ACTION_PREV = 2;
constexpr fui::ActionId ACTION_SKIP = 3;
constexpr fui::ActionId ACTION_SKIP_YES = 4;
constexpr fui::ActionId ACTION_SKIP_NO = 5;

constexpr int16_t PICTURE_HEIGHT = 300;
constexpr int16_t FOOTER_HEIGHT = 64;
constexpr int16_t LINE = 2;  // outline width of every drawing

const fui::Paint ink() { return fui::Paint::solid(fui::Color::Black); }
const fui::Paint grey() { return fui::Paint::dither(fui::Color::LightGray); }

// Dashed outline: the drawings use it for tap zones, so they read as areas, not buttons.
void dashedRect(fui::DrawTarget& t, const fui::Rect r) {
  constexpr int16_t DASH = 8;
  for (int16_t x = r.x; x < r.right(); x += 2 * DASH) {
    const auto w = static_cast<int16_t>(std::min<int>(DASH, r.right() - x));
    t.fill(fui::Rect{x, r.y, w, LINE}, ink());
    t.fill(fui::Rect{x, static_cast<int16_t>(r.bottom() - LINE), w, LINE}, ink());
  }
  for (int16_t y = r.y; y < r.bottom(); y += 2 * DASH) {
    const auto h = static_cast<int16_t>(std::min<int>(DASH, r.bottom() - y));
    t.fill(fui::Rect{r.x, y, LINE, h}, ink());
    t.fill(fui::Rect{static_cast<int16_t>(r.right() - LINE), y, LINE, h}, ink());
  }
}

// Horizontal arrow from x0 to x1 (either direction) at height y, with a stepped head.
void hArrow(fui::DrawTarget& t, const int16_t x0, const int16_t x1, const int16_t y) {
  const int16_t left = std::min(x0, x1);
  const int16_t right = std::max(x0, x1);
  t.fill(fui::Rect{left, static_cast<int16_t>(y - 2), static_cast<int16_t>(right - left), 4}, ink());
  const int dir = x1 > x0 ? -1 : 1;  // head points at x1, steps grow back towards x0
  for (int16_t i = 0; i < 10; ++i) {
    const auto x = static_cast<int16_t>(x1 + dir * i);
    t.fill(fui::Rect{x, static_cast<int16_t>(y - i), 1, static_cast<int16_t>(2 * i + 1)}, ink());
  }
}

// Vertical arrow pointing down from y0 to y1 at x.
void downArrow(fui::DrawTarget& t, const int16_t x, const int16_t y0, const int16_t y1) {
  t.fill(fui::Rect{static_cast<int16_t>(x - 2), y0, 4, static_cast<int16_t>(y1 - y0)}, ink());
  for (int16_t i = 0; i < 10; ++i) {
    t.fill(fui::Rect{static_cast<int16_t>(x - i), static_cast<int16_t>(y1 - i), static_cast<int16_t>(2 * i + 1), 1},
           ink());
  }
}

// Grey lines standing in for text.
void textLines(fui::DrawTarget& t, const fui::Rect r, const int count) {
  for (int i = 0; i < count; ++i) {
    const auto w = static_cast<int16_t>(r.width - (i % 3 == 2 ? r.width / 3 : 0));
    t.fill(fui::Rect{r.x, static_cast<int16_t>(r.y + i * 14), w, 5}, grey());
  }
}

// A rounded card with a 32px icon and a label under it.
void iconCard(UiAppHost::UiScreen& screen, const fui::Rect r, const freeink::Icon& icon, const char* label) {
  auto& t = screen.target();
  t.stroke(r, ink(), LINE, 10);
  t.bitmap(fui::Rect{static_cast<int16_t>(r.x + (r.width - 32) / 2), static_cast<int16_t>(r.y + 16), 32, 32},
           fui::bitmapFromIcon(icon), fui::BitmapMode::Center, ink(), fui::Rotation::None);
  auto text = screen.theme().smallText;
  text.align = fui::TextAlign::Center;
  text.maxLines = 2;
  t.text(fui::Rect{static_cast<int16_t>(r.x + 4), static_cast<int16_t>(r.y + 56), static_cast<int16_t>(r.width - 8),
                   static_cast<int16_t>(r.height - 60)},
         label, text);
}

const StrId TITLES[WelcomeGuideActivity::PAGE_COUNT] = {StrId::STR_GUIDE_WELCOME_TITLE, StrId::STR_GUIDE_READING_TITLE,
                                                        StrId::STR_GUIDE_QUICK_TITLE, StrId::STR_GUIDE_BOOKS_TITLE,
                                                        StrId::STR_GUIDE_DONE_TITLE};
const StrId BODIES[WelcomeGuideActivity::PAGE_COUNT] = {StrId::STR_GUIDE_WELCOME_BODY, StrId::STR_GUIDE_READING_BODY,
                                                        StrId::STR_GUIDE_QUICK_BODY, StrId::STR_GUIDE_BOOKS_BODY,
                                                        StrId::STR_GUIDE_DONE_BODY};
}  // namespace

bool WelcomeGuideActivity::shouldShowAtBoot() {
  if (APP_STATE.welcomeGuideSeen >= VERSION) return false;
  // Never seen: always. Seen an older guide: only if the user keeps "show after updates" on.
  return APP_STATE.welcomeGuideSeen == 0 || SETTINGS.welcomeGuideAfterUpdates != 0;
}

WelcomeGuideActivity::WelcomeGuideActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("WelcomeGuide", renderer, mappedInput), UiAppHost(renderer) {}

void WelcomeGuideActivity::onEnter() {
  Activity::onEnter();
  page = 0;
  confirmSkip = false;
  resetUi();
  app.on(ACTION_NEXT, &WelcomeGuideActivity::onAction, this);
  app.on(ACTION_PREV, &WelcomeGuideActivity::onAction, this);
  app.on(ACTION_SKIP, &WelcomeGuideActivity::onAction, this);
  app.on(ACTION_SKIP_YES, &WelcomeGuideActivity::onAction, this);
  app.on(ACTION_SKIP_NO, &WelcomeGuideActivity::onAction, this);
  app.setScreen(&WelcomeGuideActivity::screenFn, this);
  requestUpdate();
}

void WelcomeGuideActivity::onAction(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<WelcomeGuideActivity*>(user);
  self.pendingAction = event.action;
  self.app.clearTapFlash();
}

void WelcomeGuideActivity::next() {
  if (page + 1 >= PAGE_COUNT) {
    finishGuide();
    return;
  }
  ++page;
  requestUpdate();
}

void WelcomeGuideActivity::previous() {
  if (page == 0) {
    confirmSkip = true;  // Back on the first page offers to skip instead of leaving silently
  } else {
    --page;
  }
  requestUpdate();
}

void WelcomeGuideActivity::finishGuide() {
  // Finishing and skipping both count as seen.
  APP_STATE.welcomeGuideSeen = VERSION;
  APP_STATE.saveToFile();
  finish();
}

void WelcomeGuideActivity::loop() {
  pendingAction = 0;
  const auto touch = routeTouch(mappedInput);
  int action = touch.snap.touchReleased ? pendingAction : 0;

  if (confirmSkip) {
    if (action == ACTION_SKIP_YES || mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finishGuide();
    } else if (action == ACTION_SKIP_NO || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      confirmSkip = false;
      requestUpdate();
    }
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) action = ACTION_NEXT;
  if (swipe == MappedInputManager::SwipeDir::Right) action = ACTION_PREV;
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Confirm) || mappedInput.wasReleased(B::Right) || mappedInput.wasReleased(B::Down) ||
      mappedInput.wasReleased(B::PageForward)) {
    action = ACTION_NEXT;
  } else if (mappedInput.wasReleased(B::Back) || mappedInput.wasReleased(B::Left) || mappedInput.wasReleased(B::Up) ||
             mappedInput.wasReleased(B::PageBack)) {
    action = ACTION_PREV;
  }

  if (action == ACTION_NEXT) {
    next();
  } else if (action == ACTION_PREV) {
    previous();
  } else if (action == ACTION_SKIP) {
    confirmSkip = true;
    requestUpdate();
  }
}

void WelcomeGuideActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  // Same frame on every page: fast refreshes, with a full one on the last page to clear ghosts.
  renderer.displayBuffer(page == PAGE_COUNT - 1 && !confirmSkip ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

void WelcomeGuideActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<WelcomeGuideActivity*>(user)->build(screen);
}

void WelcomeGuideActivity::build(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.batteryBarHeight}, nullptr,
                 nullptr, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.batteryBarHeight), 0, 0, 0});
  screen.insetContent(fui::Insets{theme.spaceMd, theme.spaceLg, theme.spaceMd, theme.spaceLg});

  auto title = theme.titleText;
  title.bold = true;
  screen.target().text(screen.takeTop(screen.target().lineHeight(title.font), theme.spaceMd), I18N.get(TITLES[page]),
                       title);
  drawPicture(screen, screen.takeTop(PICTURE_HEIGHT, theme.spaceLg));
  const fui::Rect footer = screen.takeBottom(FOOTER_HEIGHT, 0);
  auto body = theme.bodyText;
  body.maxLines = 6;
  screen.target().text(screen.body(), I18N.get(BODIES[page]), body);
  drawFooter(screen, footer);
  if (confirmSkip) drawSkipConfirm(screen);
}

void WelcomeGuideActivity::drawPicture(UiScreen& screen, const fui::Rect a) {
  auto& t = screen.target();
  auto small = screen.theme().smallText;
  small.align = fui::TextAlign::Center;
  const int16_t line = t.lineHeight(small.font);
  const auto cx = static_cast<int16_t>(a.x + a.width / 2);

  switch (page) {
    case 0: {  // What VaultInk does: three cards
      const int16_t w = static_cast<int16_t>((a.width - 2 * 12) / 3);
      const int16_t y = static_cast<int16_t>(a.y + (a.height - 140) / 2);
      iconCard(screen, fui::Rect{a.x, y, w, 140}, icon_book_open_32, tr(STR_HOME_BOOKS));
      iconCard(screen, fui::Rect{static_cast<int16_t>(a.x + w + 12), y, w, 140}, icon_vault_32,
               tr(STR_GUIDE_VAULTS_SOON));
      iconCard(screen, fui::Rect{static_cast<int16_t>(a.x + 2 * (w + 12)), y, w, 140}, icon_file_text_32,
               "English · Ελληνικά");
      break;
    }
    case 1: {  // Reading: a page, the swipe arrows and the menu tap zone
      const fui::Rect device{static_cast<int16_t>(cx - 80), a.y, 160, a.height};
      t.stroke(device, ink(), LINE, 12);
      textLines(t,
                fui::Rect{static_cast<int16_t>(device.x + 16), static_cast<int16_t>(device.y + 20),
                          static_cast<int16_t>(device.width - 32), 0},
                18);
      const fui::Rect menu{static_cast<int16_t>(device.x + 8), static_cast<int16_t>(device.y + device.height / 3),
                           static_cast<int16_t>(device.width - 16), static_cast<int16_t>(device.height / 3)};
      dashedRect(t, menu);
      t.fill(menu.inset(fui::Insets{30, 40, 30, 40}), fui::Paint::solid(fui::Color::White), 8);
      t.text(menu.inset(fui::Insets{static_cast<int16_t>((menu.height - line) / 2), 4, 0, 4}), tr(STR_GUIDE_TAP_MENU),
             small);
      // Swipe left: next page. Swipe right: previous page.
      const auto yNext = static_cast<int16_t>(a.y + 40);
      const auto yPrev = static_cast<int16_t>(a.bottom() - 40);
      hArrow(t, static_cast<int16_t>(device.right() + 70), static_cast<int16_t>(device.right() + 8), yNext);
      hArrow(t, static_cast<int16_t>(device.x - 70), static_cast<int16_t>(device.x - 8), yPrev);
      small.align = fui::TextAlign::Left;
      small.maxLines = 3;
      t.text(fui::Rect{static_cast<int16_t>(device.right() + 8), static_cast<int16_t>(yNext + 10),
                       static_cast<int16_t>(a.right() - device.right() - 8), static_cast<int16_t>(3 * line)},
             tr(STR_GUIDE_NEXT_PAGE), small);
      small.align = fui::TextAlign::Right;
      t.text(fui::Rect{a.x, static_cast<int16_t>(yPrev - 3 * line - 10), static_cast<int16_t>(device.x - a.x - 8),
                       static_cast<int16_t>(3 * line)},
             tr(STR_GUIDE_PREV_PAGE), small);
      break;
    }
    case 2: {  // Quick settings: status bar, arrow down, the panel's tiles
      const fui::Rect device{static_cast<int16_t>(cx - 120), a.y, 240, a.height};
      t.stroke(device, ink(), LINE, 12);
      const fui::Rect bar{static_cast<int16_t>(device.x + 8), static_cast<int16_t>(device.y + 8),
                          static_cast<int16_t>(device.width - 16), 22};
      dashedRect(t, bar);
      t.text(bar, "14:05            86%", small);
      downArrow(t, cx, static_cast<int16_t>(bar.bottom() + 4), static_cast<int16_t>(bar.bottom() + 50));
      const fui::Rect sheet{static_cast<int16_t>(device.x + 8), static_cast<int16_t>(bar.bottom() + 60),
                            static_cast<int16_t>(device.width - 16), 150};
      t.stroke(sheet, ink(), LINE, 10);
      const freeink::Icon* icons[] = {&icon_sun_32, &icon_moon_32, &icon_refresh_cw_32, &icon_power_32};
      for (int i = 0; i < 4; ++i) {
        const auto x = static_cast<int16_t>(sheet.x + 10 + i * (sheet.width - 20) / 4);
        const fui::Rect c{x, static_cast<int16_t>(sheet.y + 20), 44, 44};
        t.stroke(c, ink(), LINE, 22);
        t.bitmap(c.inset(fui::Insets{6, 6, 6, 6}), fui::bitmapFromIcon(*icons[i]), fui::BitmapMode::Center, ink(),
                 fui::Rotation::None);
      }
      textLines(t,
                fui::Rect{static_cast<int16_t>(sheet.x + 12), static_cast<int16_t>(sheet.y + 90),
                          static_cast<int16_t>(sheet.width - 24), 0},
                3);
      break;
    }
    case 3: {  // Adding books: SD card with /books, and File Transfer
      const fui::Rect card{static_cast<int16_t>(a.x + 10), static_cast<int16_t>(a.y + 30), 150, 200};
      t.stroke(card, ink(), LINE, 10);
      t.fill(fui::Rect{static_cast<int16_t>(card.right() - 40), card.y, 40, 40}, fui::Paint::solid(fui::Color::White));
      t.fill(fui::Rect{static_cast<int16_t>(card.right() - 40), card.y, LINE, 40}, ink());
      t.bitmap(fui::Rect{static_cast<int16_t>(card.x + 59), static_cast<int16_t>(card.y + 70), 32, 32},
               fui::bitmapFromIcon(icon_folder_32), fui::BitmapMode::Center, ink(), fui::Rotation::None);
      t.text(fui::Rect{card.x, static_cast<int16_t>(card.y + 112), card.width, line}, "/books", small);
      t.text(fui::Rect{card.x, static_cast<int16_t>(card.bottom() + 8), card.width, line}, tr(STR_GUIDE_SD_CARD),
             small);
      const fui::Rect transfer{static_cast<int16_t>(a.right() - 170), static_cast<int16_t>(a.y + 30), 160, 200};
      iconCard(screen, transfer, icon_download_32, tr(STR_HOME_TRANSFER_DESC));
      t.text(fui::Rect{transfer.x, static_cast<int16_t>(transfer.bottom() + 8), transfer.width, line},
             tr(STR_FILE_TRANSFER), small);
      break;
    }
    default: {  // Done: where to find the guide again
      t.bitmap(fui::Rect{static_cast<int16_t>(cx - 16), static_cast<int16_t>(a.y + 40), 32, 32},
               fui::bitmapFromIcon(icon_settings_32), fui::BitmapMode::Center, ink(), fui::Rotation::None);
      auto path = screen.theme().bodyText;
      path.bold = true;
      path.align = fui::TextAlign::Center;
      char text[96];
      snprintf(text, sizeof(text), "%s  ›  %s", tr(STR_SETTINGS_TITLE), tr(STR_CAT_SYSTEM));
      const int16_t pathLine = t.lineHeight(path.font);
      const fui::Rect pill{a.x, static_cast<int16_t>(a.y + 96), a.width, static_cast<int16_t>(2 * pathLine + 24)};
      t.stroke(pill, ink(), LINE, 16);
      t.text(fui::Rect{static_cast<int16_t>(pill.x + 8), static_cast<int16_t>(pill.y + 12),
                       static_cast<int16_t>(pill.width - 16), pathLine},
             text, path);
      t.text(fui::Rect{static_cast<int16_t>(pill.x + 8), static_cast<int16_t>(pill.y + 12 + pathLine),
                       static_cast<int16_t>(pill.width - 16), pathLine},
             tr(STR_WELCOME_GUIDE), path);
      small.maxLines = 3;
      t.text(fui::Rect{a.x, static_cast<int16_t>(pill.bottom() + 16), a.width, static_cast<int16_t>(3 * line)},
             tr(STR_WELCOME_GUIDE_AFTER_UPDATES), small);
      break;
    }
  }
}

void WelcomeGuideActivity::drawFooter(UiScreen& screen, const fui::Rect f) {
  auto& t = screen.target();
  const auto& theme = screen.theme();
  const bool last = page == PAGE_COUNT - 1;
  const fui::Rect nextButton{static_cast<int16_t>(f.right() - 150), f.y, 150, static_cast<int16_t>(f.height - 8)};
  t.stroke(nextButton, ink(), 3, 12);
  auto label = theme.bodyText;
  label.bold = true;
  label.align = fui::TextAlign::Center;
  const auto labelY = static_cast<int16_t>((nextButton.height - t.lineHeight(label.font)) / 2);
  t.text(nextButton.inset(fui::Insets{labelY, 4, 0, 4}), last ? tr(STR_GUIDE_START) : tr(STR_GUIDE_NEXT), label);
  screen.frame().hit(nextButton, ACTION_NEXT, 0, fui::InputTouch);

  auto small = theme.smallText;
  small.align = fui::TextAlign::Center;
  char counter[8];
  snprintf(counter, sizeof(counter), "%d / %d", page + 1, PAGE_COUNT);
  const fui::Rect middle{static_cast<int16_t>(f.x + 120), f.y, static_cast<int16_t>(f.width - 270), nextButton.height};
  t.text(middle.inset(fui::Insets{labelY, 0, 0, 0}), counter, small);

  if (!last) {
    const fui::Rect skip{f.x, f.y, 110, nextButton.height};
    t.text(skip.inset(fui::Insets{labelY, 0, 0, 0}), tr(STR_GUIDE_SKIP), label);
    screen.frame().hit(skip, ACTION_SKIP, 0, fui::InputTouch);
  }
}

void WelcomeGuideActivity::drawSkipConfirm(UiScreen& screen) {
  auto& t = screen.target();
  const auto& theme = screen.theme();
  const fui::Rect box{40, static_cast<int16_t>(renderer.getScreenHeight() / 2 - 100),
                      static_cast<int16_t>(renderer.getScreenWidth() - 80), 200};
  t.fill(box, fui::Paint::solid(fui::Color::White), 14);
  t.stroke(box, ink(), 3, 14);
  auto text = theme.bodyText;
  text.align = fui::TextAlign::Center;
  text.maxLines = 3;
  t.text(fui::Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 24),
                   static_cast<int16_t>(box.width - 32), 90},
         tr(STR_GUIDE_SKIP_CONFIRM), text);
  const auto w = static_cast<int16_t>((box.width - 48) / 2);
  const fui::Rect no{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.bottom() - 64), w, 48};
  const fui::Rect yes{static_cast<int16_t>(no.right() + 16), no.y, w, 48};
  t.stroke(no, ink(), LINE, 10);
  t.stroke(yes, ink(), 3, 10);
  auto label = text;
  label.bold = true;
  label.maxLines = 1;
  const auto y = static_cast<int16_t>((48 - t.lineHeight(label.font)) / 2);
  t.text(no.inset(fui::Insets{y, 4, 0, 4}), tr(STR_GUIDE_KEEP_GOING), label);
  t.text(yes.inset(fui::Insets{y, 4, 0, 4}), tr(STR_GUIDE_SKIP), label);
  screen.frame().hit(no, ACTION_SKIP_NO, 0, fui::InputTouch);
  screen.frame().hit(yes, ACTION_SKIP_YES, 0, fui::InputTouch);
}
