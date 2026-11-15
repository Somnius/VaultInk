#pragma once

#include <I18n.h>

#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"

// The rows of Typography Engine (kos25k #30), shared by Settings > Reader > Typography Engine and the book's Text
// panel: their names, the choices of each, and reading and writing the setting behind a row as a choice number.
namespace typooptions {

enum Row : int {
  CONDENSE = 0,
  LETTER_SPACING,
  LOCKED_SPACE,
  PUNCT_SPACE,
  OPTICAL,
  HYPHEN_MODE,
  MIN_LEFT,
  MIN_RIGHT,
  LANGUAGE,
  COUNT
};

inline constexpr StrId NAMES[COUNT] = {
    StrId::STR_TYPO_CONDENSE,    StrId::STR_TYPO_LETTER_SPACING, StrId::STR_TYPO_LOCKED_SPACE,
    StrId::STR_TYPO_PUNCT_SPACE, StrId::STR_TYPO_OPTICAL,        StrId::STR_TYPO_HYPHEN_MODE,
    StrId::STR_TYPO_MIN_LEFT,    StrId::STR_TYPO_MIN_RIGHT,      StrId::STR_TYPO_HYPHEN_LANG};

inline uint8_t* field(const int row) {
  switch (row) {
    case CONDENSE:
      return &SETTINGS.typoCondense;
    case LETTER_SPACING:
      return &SETTINGS.typoLetterSpacing;
    case LOCKED_SPACE:
      return &SETTINGS.typoLockedSpace;
    case PUNCT_SPACE:
      return &SETTINGS.typoPunctSpace;
    case OPTICAL:
      return &SETTINGS.typoOptical;
    case HYPHEN_MODE:
      return &SETTINGS.typoHyphenMode;
    case MIN_LEFT:
      return &SETTINGS.typoMinLeft;
    case MIN_RIGHT:
      return &SETTINGS.typoMinRight;
    case LANGUAGE:
      return &SETTINGS.typoHyphenLang;
    default:
      return nullptr;
  }
}

inline std::vector<std::string> choices(const int row) {
  std::vector<std::string> out;
  switch (row) {
    case CONDENSE:
      out.emplace_back(tr(STR_STATE_OFF));
      for (int percent = 90; percent >= typoengine::CONDENSE_MIN_PERCENT;
           percent -= typoengine::CONDENSE_STEP_PERCENT) {
        out.push_back(std::to_string(percent) + "%");
      }
      break;
    case LETTER_SPACING:
      out = {tr(STR_STATE_OFF), tr(STR_TYPO_SMALL), tr(STR_TYPO_MEDIUM)};
      break;
    case LOCKED_SPACE:
    case OPTICAL:
      out = {tr(STR_STATE_OFF), tr(STR_STATE_ON)};
      break;
    case PUNCT_SPACE:
      out = {tr(STR_NORMAL), tr(STR_TYPO_PUNCT_MINIMAL), tr(STR_NONE_OPT)};
      break;
    case HYPHEN_MODE:
      out = {tr(STR_TYPO_HYPHEN_PATTERNS), tr(STR_TYPO_HYPHEN_SOFT), tr(STR_STATE_OFF)};
      break;
    case MIN_LEFT:
    case MIN_RIGHT:
      out.emplace_back(tr(STR_DEFAULT_VALUE));
      for (int letters = 1; letters <= typoengine::MIN_LETTERS_MAX; ++letters) out.push_back(std::to_string(letters));
      break;
    case LANGUAGE:
      out.emplace_back(tr(STR_TYPO_BOOK_LANGUAGE));
      out.insert(out.end(), std::begin(typoengine::LANGUAGE_NAMES), std::end(typoengine::LANGUAGE_NAMES));
      break;
    default:
      break;
  }
  return out;
}

// The choice a row is on. Space condensing is stored as its percentage (100 = off), the rest as the choice number.
inline int choice(const int row) {
  const uint8_t* value = field(row);
  if (value == nullptr) return 0;
  const int count = static_cast<int>(choices(row).size());
  const int index = row == CONDENSE
                        ? (100 - typoengine::sanitizeCondensePercent(*value)) / typoengine::CONDENSE_STEP_PERCENT
                        : *value;
  return index < count ? index : 0;
}

inline void setChoice(const int row, const int index) {
  uint8_t* value = field(row);
  const int count = static_cast<int>(choices(row).size());
  if (value == nullptr || index < 0 || index >= count) return;
  *value = static_cast<uint8_t>(row == CONDENSE ? 100 - index * typoengine::CONDENSE_STEP_PERCENT : index);
  // Choosing Patterns or Soft hyphens only is asking for hyphenation: the Hyphenation switch goes on with it, else the
  // pick would change nothing visible while the switch is off.
  if (row == HYPHEN_MODE && *value != typoengine::HYPHEN_OFF) SETTINGS.hyphenationEnabled = 1;
}

inline std::string valueText(const int row) {
  const auto all = choices(row);
  const int index = choice(row);
  return index < static_cast<int>(all.size()) ? all[static_cast<size_t>(index)] : std::string();
}

// True for a row with two choices (a switch): a pick flips it. The others open the list of their choices.
inline bool isSwitch(const int row) { return choices(row).size() <= 2; }

// The next choice, back to the first after the last: what a pick of a switch row does in the book's Text panel.
inline void step(const int row) {
  const int count = static_cast<int>(choices(row).size());
  if (count > 0) setChoice(row, (choice(row) + 1) % count);
}

}  // namespace typooptions
