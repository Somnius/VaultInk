#pragma once

#include <I18nKeys.h>

#include "CrossPointSettings.h"

// Labels for the line spacing levels, "0" to "10".
inline constexpr StrId LINE_SPACING_IDS[CrossPointSettings::LINE_COMPRESSION_COUNT] = {
    StrId::STR_SPACING_0, StrId::STR_SPACING_1, StrId::STR_SPACING_2, StrId::STR_SPACING_3,
    StrId::STR_SPACING_4, StrId::STR_SPACING_5, StrId::STR_SPACING_6, StrId::STR_SPACING_7,
    StrId::STR_SPACING_8, StrId::STR_SPACING_9, StrId::STR_SPACING_10};
