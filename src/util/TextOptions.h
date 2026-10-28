#pragma once

#include <I18nKeys.h>

#include <cstdint>
#include <iterator>

#include "CrossPointSettings.h"

// The choices for the text layout settings, shared by Settings → Reader → Text Settings and the
// reader's Text panel (kos25k #12), so both offer the same steps.
namespace textoptions {

inline constexpr StrId WORD_SPACING_IDS[] = {StrId::STR_SPACING_50_PERCENT,  StrId::STR_SPACING_75_PERCENT,
                                             StrId::STR_SPACING_100_PERCENT, StrId::STR_SPACING_125_PERCENT,
                                             StrId::STR_SPACING_150_PERCENT, StrId::STR_SPACING_175_PERCENT,
                                             StrId::STR_SPACING_200_PERCENT};
inline constexpr StrId CHARACTER_SPACING_IDS[] = {StrId::STR_SPACING_MINUS_2, StrId::STR_SPACING_MINUS_1,
                                                  StrId::STR_SPACING_ZERO, StrId::STR_SPACING_PLUS_1,
                                                  StrId::STR_SPACING_PLUS_2};
// The popup holds 16 rows and doesn't scroll: fine steps near the edge, wider ones after.
inline constexpr uint8_t MARGINS[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 20, 25, 30, 35, 40};
static_assert(MARGINS[std::size(MARGINS) - 1] == CrossPointSettings::SCREEN_MARGIN_MAX,
              "margin choices end at the maximum");
static_assert(std::size(WORD_SPACING_IDS) ==
              (CrossPointSettings::WORD_SPACING_MAX - CrossPointSettings::WORD_SPACING_MIN) /
                      CrossPointSettings::WORD_SPACING_STEP +
                  1);
inline constexpr int MAX_INDENT = 5;

}  // namespace textoptions
