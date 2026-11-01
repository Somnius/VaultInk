#include "DiceActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/RandomPage.h"

namespace {
constexpr char SAVE_PATH[] = "/.vaultink/dice.sav";
constexpr uint8_t SAVE_VERSION = 1;

enum class Kind : uint8_t { Number, Toggle, Sides, Mode, Again, Trait };

struct Param {
  StrId label;
  int16_t lo;
  int16_t hi;
  int16_t def;
  Kind kind;
};

enum Extra : uint8_t { EXTRA_REROLL = 1, EXTRA_ROUSE = 2, EXTRA_PUSH = 4 };

struct SystemDef {
  const char* name;
  dice::System id;
  uint8_t count;
  uint8_t extras;
  Param params[6];
};

constexpr SystemDef SYSTEMS[] = {
    {"D&D / polyhedral",
     dice::System::Polyhedral,
     6,
     0,
     {{StrId::STR_DICE_COUNT, 1, 20, 1, Kind::Number},
      {StrId::STR_DICE_SIDES, 0, 6, 5, Kind::Sides},
      {StrId::STR_DICE_MODIFIER, -20, 20, 0, Kind::Number},
      {StrId::STR_DICE_KEEP, 0, 20, 0, Kind::Number},
      {StrId::STR_DICE_MODE, 0, 2, 0, Kind::Mode},
      {StrId::STR_DICE_EXPLODE, 0, 1, 0, Kind::Toggle}}},
    {"World of Darkness (classic)",
     dice::System::Wod,
     5,
     0,
     {{StrId::STR_DICE_POOL, 1, 30, 5, Kind::Number},
      {StrId::STR_DICE_DIFFICULTY, 2, 10, 6, Kind::Number},
      {StrId::STR_DICE_SPECIALTY, 0, 1, 0, Kind::Toggle},
      {StrId::STR_DICE_WILLPOWER, 0, 1, 0, Kind::Toggle},
      {StrId::STR_DICE_MAGE, 0, 1, 0, Kind::Toggle}}},
    {"Vampire V5",
     dice::System::V5,
     3,
     EXTRA_REROLL | EXTRA_ROUSE,
     {{StrId::STR_DICE_POOL, 1, 30, 5, Kind::Number},
      {StrId::STR_DICE_HUNGER, 0, 5, 1, Kind::Number},
      {StrId::STR_DICE_DIFFICULTY, 0, 10, 1, Kind::Number}}},
    {"Chronicles of Darkness",
     dice::System::Cod,
     3,
     0,
     {{StrId::STR_DICE_POOL, 0, 30, 5, Kind::Number},
      {StrId::STR_DICE_AGAIN, 0, 3, 0, Kind::Again},
      {StrId::STR_DICE_ROTE, 0, 1, 0, Kind::Toggle}}},
    {"Fate / Fudge", dice::System::Fate, 1, 0, {{StrId::STR_DICE_MODIFIER, -8, 8, 0, Kind::Number}}},
    {"Powered by the Apocalypse", dice::System::Pbta, 1, 0, {{StrId::STR_DICE_STAT, -3, 4, 0, Kind::Number}}},
    {"Blades in the Dark", dice::System::Blades, 1, 0, {{StrId::STR_DICE_POOL, 0, 10, 2, Kind::Number}}},
    {"Call of Cthulhu",
     dice::System::Coc,
     3,
     0,
     {{StrId::STR_DICE_SKILL, 1, 100, 50, Kind::Number},
      {StrId::STR_DICE_BONUS, 0, 2, 0, Kind::Number},
      {StrId::STR_DICE_PENALTY, 0, 2, 0, Kind::Number}}},
    {"Shadowrun", dice::System::Shadowrun, 1, 0, {{StrId::STR_DICE_POOL, 1, 30, 8, Kind::Number}}},
    {"Savage Worlds",
     dice::System::Savage,
     4,
     0,
     {{StrId::STR_DICE_TRAIT, 0, 4, 2, Kind::Trait},
      {StrId::STR_DICE_MODIFIER, -4, 4, 0, Kind::Number},
      {StrId::STR_DICE_WILD, 0, 1, 1, Kind::Toggle},
      {StrId::STR_DICE_TARGET, 4, 8, 4, Kind::Number}}},
    {"Year Zero", dice::System::YearZero, 1, EXTRA_PUSH, {{StrId::STR_DICE_POOL, 1, 20, 5, Kind::Number}}},
    {"Cyberpunk Red",
     dice::System::Cyberpunk,
     2,
     0,
     {{StrId::STR_DICE_MODIFIER, 0, 20, 8, Kind::Number}, {StrId::STR_DICE_DV, 0, 30, 0, Kind::Number}}},
};
constexpr int SYSTEM_COUNT = static_cast<int>(sizeof(SYSTEMS) / sizeof(SYSTEMS[0]));

constexpr int16_t SIDES[] = {4, 6, 8, 10, 12, 20, 100};
constexpr int16_t TRAITS[] = {4, 6, 8, 10, 12};
constexpr int16_t AGAIN[] = {10, 9, 8, 0};

constexpr int ROW_H = 54;
constexpr int PM_W = 64;  // the -/+ boxes

const char* outcomeLabel(const dice::Outcome o) {
  using O = dice::Outcome;
  switch (o) {
    case O::Success:
      return tr(STR_DICE_O_SUCCESS);
    case O::Failure:
      return tr(STR_DICE_O_FAILURE);
    case O::Botch:
      return tr(STR_DICE_O_BOTCH);
    case O::ExceptionalSuccess:
      return tr(STR_DICE_O_EXCEPTIONAL);
    case O::PhenomenalSuccess:
      return tr(STR_DICE_O_PHENOMENAL);
    case O::CriticalSuccess:
      return tr(STR_DICE_O_CRITICAL);
    case O::MessyCritical:
      return tr(STR_DICE_O_MESSY);
    case O::BestialFailure:
      return tr(STR_DICE_O_BESTIAL);
    case O::TotalFailure:
      return tr(STR_DICE_O_TOTAL_FAILURE);
    case O::DramaticFailure:
      return tr(STR_DICE_O_DRAMATIC);
    case O::CriticalFailure:
      return tr(STR_DICE_O_CRIT_FAIL);
    case O::StrongHit:
      return tr(STR_DICE_O_STRONG);
    case O::WeakHit:
      return tr(STR_DICE_O_WEAK);
    case O::Miss:
      return tr(STR_DICE_O_MISS);
    case O::Full:
      return tr(STR_DICE_O_FULL);
    case O::Partial:
      return tr(STR_DICE_O_PARTIAL);
    case O::Bad:
      return tr(STR_DICE_O_BAD);
    case O::Critical:
      return tr(STR_DICE_O_COC_CRITICAL);
    case O::Extreme:
      return tr(STR_DICE_O_EXTREME);
    case O::Hard:
      return tr(STR_DICE_O_HARD);
    case O::Regular:
      return tr(STR_DICE_O_REGULAR);
    case O::Fumble:
      return tr(STR_DICE_O_FUMBLE);
    case O::Glitch:
      return tr(STR_DICE_O_GLITCH);
    case O::CriticalGlitch:
      return tr(STR_DICE_O_CRIT_GLITCH);
    case O::Raise:
      return tr(STR_DICE_O_RAISE);
    case O::None:
      break;
  }
  return "";
}

// The value of a parameter as shown: a number, d20, Advantage, 10-again, ...
void formatValue(const Param& p, const int16_t v, char* out, const size_t size) {
  switch (p.kind) {
    case Kind::Sides:
      snprintf(out, size, "d%d", SIDES[std::clamp<int>(v, 0, 6)]);
      break;
    case Kind::Trait:
      snprintf(out, size, "d%d", TRAITS[std::clamp<int>(v, 0, 4)]);
      break;
    case Kind::Mode:
      snprintf(out, size, "%s",
               v == 1   ? tr(STR_DICE_ADVANTAGE)
               : v == 2 ? tr(STR_DICE_DISADVANTAGE)
                        : tr(STR_DICE_NORMAL));
      break;
    case Kind::Again: {
      const int again = AGAIN[std::clamp<int>(v, 0, 3)];
      if (again == 0)
        snprintf(out, size, "%s", tr(STR_DICE_NONE));
      else
        snprintf(out, size, "%d", again);
      break;
    }
    case Kind::Toggle:
      snprintf(out, size, "%s", v ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
      break;
    case Kind::Number:
      snprintf(out, size, v > 0 && p.lo < 0 ? "+%d" : "%d", v);
      break;
  }
}
}  // namespace

int DiceActivity::paramCount() const { return SYSTEMS[sys].count; }

int DiceActivity::extraCount() const {
  const uint8_t extras = SYSTEMS[sys].extras;
  const bool sameSystem = hasLast && !extraUsed && last.system == SYSTEMS[sys].id;
  int n = 0;
  if ((extras & EXTRA_REROLL) && sameSystem) n++;
  if (extras & EXTRA_ROUSE) n++;
  if ((extras & EXTRA_PUSH) && sameSystem) n++;
  return n;
}

int DiceActivity::rowY(const int row) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + 8;
  // The Roll row is taller than the others.
  return top + row * ROW_H + (row > rollRow() ? 28 : 0);
}

void DiceActivity::onEnter() {
  Activity::onEnter();
  load();
  requestUpdate();
}

void DiceActivity::onExit() {
  save();
  Activity::onExit();
}

void DiceActivity::load() {
  for (int s = 0; s < SYSTEM_COUNT; ++s) {
    for (int p = 0; p < SYSTEMS[s].count; ++p) values[s][p] = SYSTEMS[s].params[p].def;
  }
  HalFile file;
  if (Storage.exists(SAVE_PATH) && Storage.openFileForRead("DIC", SAVE_PATH, file)) {
    uint8_t head[4]{};
    if (file.read(head, 4) == 4 && head[0] == 'D' && head[1] == 'I' && head[2] == SAVE_VERSION) {
      sys = std::clamp<int>(head[3], 0, SYSTEM_COUNT - 1);
      for (int s = 0; s < SYSTEM_COUNT; ++s) {
        for (int p = 0; p < SYSTEMS[s].count; ++p) {
          uint8_t two[2];
          if (file.read(two, 2) != 2) break;
          const auto& def = SYSTEMS[s].params[p];
          values[s][p] =
              static_cast<int16_t>(std::clamp<int>(static_cast<int16_t>(two[0] | (two[1] << 8)), def.lo, def.hi));
        }
      }
    }
    file.close();
  }
}

void DiceActivity::save() const {
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file;
  if (!Storage.openFileForWrite("DIC", SAVE_PATH, file)) {
    LOG_ERR("DIC", "Could not save the dice settings");
    return;
  }
  const uint8_t head[4] = {'D', 'I', SAVE_VERSION, static_cast<uint8_t>(sys)};
  file.write(head, 4);
  for (int s = 0; s < SYSTEM_COUNT; ++s) {
    for (int p = 0; p < SYSTEMS[s].count; ++p) {
      const uint8_t two[2] = {static_cast<uint8_t>(values[s][p] & 0xFF),
                              static_cast<uint8_t>((values[s][p] >> 8) & 0xFF)};
      file.write(two, 2);
    }
  }
  file.close();
}

void DiceActivity::changeSystem(const int delta) {
  sys = (sys + delta + SYSTEM_COUNT) % SYSTEM_COUNT;
  focus = std::min(focus, rowCount() - 1);
  hasLast = false;
}

void DiceActivity::adjust(const int row, const int delta) {
  if (row == 0) {
    changeSystem(delta);
    return;
  }
  const int p = row - 1;
  if (p < 0 || p >= paramCount()) return;
  const Param& def = SYSTEMS[sys].params[p];
  if (def.kind == Kind::Toggle) {
    values[sys][p] = values[sys][p] ? 0 : 1;
  } else {
    values[sys][p] = static_cast<int16_t>(std::clamp<int>(values[sys][p] + delta, def.lo, def.hi));
  }
}

void DiceActivity::remember() {
  if (historyCount < HISTORY) historyCount++;
  for (int i = HISTORY - 1; i > 0; --i) std::memcpy(history[i], history[i - 1], sizeof(history[i]));
  char info[40];
  describe(last, history[0], sizeof(history[0]), info, sizeof(info));
}

void DiceActivity::roll() {
  dice::Xorshift32 rng(randompage::random32());
  const int16_t* v = values[sys];
  switch (SYSTEMS[sys].id) {
    case dice::System::Polyhedral: {
      const int sides = SIDES[std::clamp<int>(v[1], 0, 6)];
      if (sides == 20 && v[0] == 1 && v[3] == 0 && v[4] == 1) {
        last = dice::rollAdvantage(rng, v[2]);
      } else if (sides == 20 && v[0] == 1 && v[3] == 0 && v[4] == 2) {
        last = dice::rollDisadvantage(rng, v[2]);
      } else {
        dice::PolyParams p;
        p.count = v[0];
        p.sides = sides;
        p.keep = std::min<int>(v[3], v[0]);
        p.modifier = v[2];
        p.explode = v[5] != 0;
        last = dice::rollPolyhedral(rng, p);
      }
      break;
    }
    case dice::System::Wod: {
      dice::WodParams p;
      p.pool = v[0];
      p.difficulty = v[1];
      p.specialty = v[2] != 0;
      p.willpower = v[3] != 0;
      p.rerollOnesCancel = v[4] != 0;
      last = dice::rollWod(rng, p);
      break;
    }
    case dice::System::V5:
    case dice::System::V5Rouse: {
      dice::V5Params p;
      p.pool = v[0];
      p.hunger = v[1];
      p.difficulty = v[2];
      last = dice::rollV5(rng, p);
      break;
    }
    case dice::System::Cod: {
      dice::CodParams p;
      p.pool = v[0];
      p.again = AGAIN[std::clamp<int>(v[1], 0, 3)];
      p.rote = v[2] != 0;
      last = dice::rollCod(rng, p);
      break;
    }
    case dice::System::Fate:
      last = dice::rollFate(rng, v[0]);
      break;
    case dice::System::Pbta:
      last = dice::rollPbta(rng, v[0]);
      break;
    case dice::System::Blades:
      last = dice::rollBlades(rng, v[0]);
      break;
    case dice::System::Coc: {
      dice::CocParams p;
      p.skill = v[0];
      p.bonusDice = v[1];
      p.penaltyDice = v[2];
      last = dice::rollCoc(rng, p);
      break;
    }
    case dice::System::Shadowrun:
      last = dice::rollShadowrun(rng, v[0]);
      break;
    case dice::System::Savage: {
      dice::SavageParams p;
      p.traitSides = TRAITS[std::clamp<int>(v[0], 0, 4)];
      p.modifier = v[1];
      p.wildDie = v[2] != 0;
      p.target = v[3];
      last = dice::rollSavage(rng, p);
      break;
    }
    case dice::System::YearZero:
      last = dice::rollYearZero(rng, v[0]);
      break;
    case dice::System::Cyberpunk: {
      dice::CyberpunkParams p;
      p.modifier = v[0];
      p.dv = v[1];
      last = dice::rollCyberpunk(rng, p);
      break;
    }
  }
  hasLast = true;
  extraUsed = false;
  remember();
}

// The extra buttons under Roll: the V5 Willpower reroll and Rouse check, the Year Zero push.
void DiceActivity::runExtra(const int extra) {
  dice::Xorshift32 rng(randompage::random32());
  if (extra == EXTRA_REROLL && hasLast && last.system == dice::System::V5) {
    last = dice::rerollV5Willpower(rng, last);
  } else if (extra == EXTRA_ROUSE) {
    last = dice::rollRouse(rng, values[sys][1]);
    // A failed Rouse check raises the Hunger the next roll uses.
    values[sys][1] = static_cast<int16_t>(std::clamp<int>(last.total, 0, 5));
  } else if (extra == EXTRA_PUSH && hasLast && last.system == dice::System::YearZero) {
    last = dice::pushYearZero(rng, last);
  } else {
    return;
  }
  hasLast = true;
  // The Willpower reroll and the push are once per roll; the Rouse check rolls on its own.
  if (extra != EXTRA_ROUSE) extraUsed = true;
  remember();
}

void DiceActivity::describe(const dice::Result& r, char* head, const size_t headSize, char* info,
                            const size_t infoSize) const {
  head[0] = info[0] = '\0';
  if (!r.ok) {
    snprintf(head, headSize, "%s", tr(STR_DICE_ERR));
    return;
  }
  if (r.system == dice::System::V5Rouse) {
    if (r.outcome == dice::Outcome::Success) {
      snprintf(head, headSize, "%s", tr(STR_DICE_ROUSE_OK));
    } else {
      snprintf(head, headSize, "%s %d", tr(STR_DICE_ROUSE_FAIL), static_cast<int>(r.total));
      if (r.margin == 1) snprintf(info, infoSize, "%s", tr(STR_DICE_FRENZY));
    }
    return;
  }
  const char* label = outcomeLabel(r.outcome);
  const bool pool = r.system == dice::System::Wod || r.system == dice::System::V5 || r.system == dice::System::Cod ||
                    r.system == dice::System::Shadowrun || r.system == dice::System::YearZero;
  if (label[0] != '\0') {
    snprintf(head, headSize, "%s", label);
  } else {
    snprintf(head, headSize, "%d", static_cast<int>(r.total));
  }
  if (pool) {
    snprintf(info, infoSize, "%s: %d", tr(STR_DICE_SUCCESSES), static_cast<int>(r.successes));
  } else if (label[0] != '\0') {
    snprintf(info, infoSize, "%s: %d", tr(STR_DICE_TOTAL), static_cast<int>(r.total));
  }
}

bool DiceActivity::handleTap(const int x, const int y) {
  const int width = renderer.getScreenWidth();
  const int rows = rowCount();
  for (int row = 0; row < rows; ++row) {
    const int top = rowY(row);
    const int h = row == rollRow() ? 84 : ROW_H;
    if (y < top || y >= top + h) continue;
    focus = row;
    if (row == 0) {
      changeSystem(x < width / 2 ? -1 : 1);
    } else if (row == rollRow()) {
      roll();
    } else if (row > rollRow()) {
      // Extras in the order they are drawn.
      const uint8_t extras = SYSTEMS[sys].extras;
      const bool sameSystem = hasLast && !extraUsed && last.system == SYSTEMS[sys].id;
      int index = row - rollRow() - 1;
      for (const uint8_t e : {EXTRA_REROLL, EXTRA_ROUSE, EXTRA_PUSH}) {
        if (!(extras & e) || (e != EXTRA_ROUSE && !sameSystem)) continue;
        if (index-- == 0) {
          runExtra(e);
          break;
        }
      }
    } else {
      const Param& def = SYSTEMS[sys].params[row - 1];
      if (def.kind == Kind::Toggle) {
        adjust(row, 1);
      } else {
        adjust(row, x < width - 2 * PM_W - 12 + PM_W ? -1 : 1);
      }
    }
    return true;
  }
  return false;
}

void DiceActivity::handleButtons() {
  using Button = MappedInputManager::Button;
  const int rows = rowCount();
  if (mappedInput.wasPressed(Button::Up)) focus = (focus + rows - 1) % rows;
  if (mappedInput.wasPressed(Button::Down)) focus = (focus + 1) % rows;
  if (mappedInput.wasPressed(Button::Left)) adjust(focus, -1);
  if (mappedInput.wasPressed(Button::Right)) adjust(focus, 1);
  if (mappedInput.wasReleased(Button::Confirm) || mappedInput.wasPressed(Button::PageForward)) {
    if (focus > rollRow()) {
      handleTap(0, rowY(focus) + 1);
    } else {
      roll();
    }
  }
}

void DiceActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  bool dirty = false;
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) dirty = handleTap(x, y);
  const int before = focus;
  const bool hadLast = hasLast;
  const auto beforeSys = sys;
  const int16_t beforeValue = sys < SYSTEM_COUNT && focus >= 1 && focus <= paramCount() ? values[sys][focus - 1] : 0;
  handleButtons();
  dirty = dirty || before != focus || hadLast != hasLast || beforeSys != sys ||
          (focus >= 1 && focus <= paramCount() && beforeValue != values[sys][focus - 1]) ||
          mappedInput.wasReleased(Button::Confirm) || mappedInput.wasPressed(Button::PageForward);
  if (dirty) {
    focus = std::min(focus, rowCount() - 1);
    requestUpdate();
  }
}

// One die: a box with its face. Hunger dice are black, a critical has a double border, a die the rule
// added or replaced has a corner dot, one that was dropped is crossed out.
void DiceActivity::drawDie(const dice::Die& die, const int x, const int y, const int size) const {
  const bool hunger = die.has(dice::FLAG_HUNGER);
  if (hunger) renderer.fillRect(x, y, size, size, true);
  renderer.drawRect(x, y, size, size, die.has(dice::FLAG_CRIT) ? 4 : 2, true);
  char text[8];
  if (die.sides == 3) {
    snprintf(text, sizeof(text), "%s", die.value > 0 ? "+" : die.value < 0 ? "-" : "0");
  } else {
    snprintf(text, sizeof(text), "%d", die.value);
  }
  const int tw = renderer.getTextWidth(UI_12_FONT_ID, text, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x + (size - tw) / 2, y + (size - renderer.getLineHeight(UI_12_FONT_ID)) / 2, text,
                    !hunger, EpdFontFamily::BOLD);
  if (die.has(dice::FLAG_EXTRA) || die.has(dice::FLAG_REROLLED)) renderer.fillRect(x + size - 10, y + 4, 6, 6, !hunger);
  if (!die.has(dice::FLAG_KEPT) && last.system == dice::System::Polyhedral && last.count > 1) {
    renderer.drawLine(x, y + size, x + size, y, 2, true);
  }
}

void DiceActivity::drawRow(const int row, const int y) const {
  const int width = renderer.getScreenWidth();
  const bool focused = row == focus;
  if (focused) renderer.drawRect(4, y, width - 8, ROW_H - 4, 3, true);
  if (row == 0) {
    const char* name = SYSTEMS[sys].name;
    const int w = renderer.getTextWidth(UI_12_FONT_ID, name, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (width - w) / 2, y + (ROW_H - 4 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, name,
                      true, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, 18, y + (ROW_H - 4 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, "<", true);
    renderer.drawText(UI_12_FONT_ID, width - 30, y + (ROW_H - 4 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, ">",
                      true);
    return;
  }
  const Param& def = SYSTEMS[sys].params[row - 1];
  const int textY = y + (ROW_H - 4 - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, 18, textY, I18N.get(def.label), true);
  char value[24];
  formatValue(def, values[sys][row - 1], value, sizeof(value));
  if (def.kind == Kind::Toggle) {
    const int w = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, width - 24 - w, y + (ROW_H - 4 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, value,
                      true, EpdFontFamily::BOLD);
    return;
  }
  const int minusX = width - 2 * PM_W - 12;
  const int plusX = width - PM_W - 6;
  renderer.drawRect(minusX, y + 2, PM_W - 6, ROW_H - 8, 2, true);
  renderer.drawRect(plusX, y + 2, PM_W - 6, ROW_H - 8, 2, true);
  renderer.drawText(UI_12_FONT_ID, minusX + (PM_W - 6 - renderer.getTextWidth(UI_12_FONT_ID, "-")) / 2, textY - 2, "-",
                    true);
  renderer.drawText(UI_12_FONT_ID, plusX + (PM_W - 6 - renderer.getTextWidth(UI_12_FONT_ID, "+")) / 2, textY - 2, "+",
                    true);
  const int vw = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, minusX - 12 - vw, y + (ROW_H - 4 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, value,
                    true, EpdFontFamily::BOLD);
}

void DiceActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_DICE), nullptr);

  const int rows = rowCount();
  for (int row = 0; row < rollRow(); ++row) drawRow(row, rowY(row));

  // The Roll button.
  {
    const int y = rowY(rollRow());
    const bool focused = focus == rollRow();
    renderer.fillRect(8, y + 4, width - 16, 76, true);
    if (focused) renderer.drawRect(4, y, width - 8, 84, 3, true);
    const char* label = tr(STR_DICE_ROLL);
    const int w = renderer.getTextWidth(NOTOSANS_18_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(NOTOSANS_18_FONT_ID, (width - w) / 2,
                      y + 4 + (76 - renderer.getLineHeight(NOTOSANS_18_FONT_ID)) / 2, label, false,
                      EpdFontFamily::BOLD);
  }
  // Extra buttons.
  {
    const uint8_t extras = SYSTEMS[sys].extras;
    const bool sameSystem = hasLast && !extraUsed && last.system == SYSTEMS[sys].id;
    int row = rollRow() + 1;
    for (const uint8_t e : {EXTRA_REROLL, EXTRA_ROUSE, EXTRA_PUSH}) {
      if (!(extras & e) || (e != EXTRA_ROUSE && !sameSystem)) continue;
      const int y = rowY(row);
      const char* label = e == EXTRA_REROLL  ? tr(STR_DICE_REROLL)
                          : e == EXTRA_ROUSE ? tr(STR_DICE_ROUSE)
                                             : tr(STR_DICE_PUSH);
      renderer.drawRect(8, y + 2, width - 16, ROW_H - 8, focus == row ? 4 : 2, true);
      const int w = renderer.getTextWidth(UI_10_FONT_ID, label);
      renderer.drawText(UI_10_FONT_ID, (width - w) / 2, y + (ROW_H - 4 - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                        label, true);
      ++row;
    }
  }

  // The result: the outcome large, a line under it, then the dice.
  int y = (rows - 1 == rollRow() ? rowY(rollRow()) + 84 : rowY(rows - 1) + ROW_H) + 8;
  if (hasLast) {
    struct {
      char head[40];
      char info[48];
    } text{};
    describe(last, text.head, sizeof(text.head), text.info, sizeof(text.info));
    const int hw = renderer.getTextWidth(NOTOSANS_18_FONT_ID, text.head, EpdFontFamily::BOLD);
    renderer.drawText(NOTOSANS_18_FONT_ID, std::max(8, (width - hw) / 2), y, text.head, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 2;
    const int iw = renderer.getTextWidth(UI_10_FONT_ID, text.info);
    renderer.drawText(UI_10_FONT_ID, std::max(8, (width - iw) / 2), y, text.info, true);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 8;
    constexpr int TILE = 48;
    constexpr int GAP = 6;
    const int perRow = (width - 16 + GAP) / (TILE + GAP);
    const int shown = std::min<int>(last.count, perRow * 3);
    const int lines = (shown + perRow - 1) / perRow;
    for (int i = 0; i < shown; ++i) {
      const int col = i % perRow;
      const int inLine = std::min(perRow, shown - (i / perRow) * perRow);
      const int lineW = inLine * (TILE + GAP) - GAP;
      drawDie(last.dice[i], (width - lineW) / 2 + col * (TILE + GAP), y + (i / perRow) * (TILE + GAP), TILE);
    }
    y += lines * (TILE + GAP);
  }
  if (historyCount > 1) {
    char line[120] = "";
    size_t used = static_cast<size_t>(snprintf(line, sizeof(line), "%s: ", tr(STR_DICE_HISTORY)));
    for (int i = 1; i < historyCount && used < sizeof(line); ++i) {
      used += static_cast<size_t>(snprintf(line + used, sizeof(line) - used, i > 1 ? " | %s" : "%s", history[i]));
    }
    const int bottom =
        renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID) - 6;
    renderer.drawText(SMALL_FONT_ID, 10, bottom, line, true);
  }

  const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DICE_ROLL), "-", "+");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
