#pragma once

#include <I18n.h>

#include <iterator>

#include "FormatFeatures.h"

// The rows of the reader toolbar's Text panel, in their default order (kos25k #12 and #30). A row's number is its
// place here: the panel code and Settings > Reader > Book menu options (kos25k #15) both go by it.
namespace textpanel {
inline constexpr StrId ROWS[] = {StrId::STR_FONT,
                                 StrId::STR_FONT_SIZE,
                                 StrId::STR_LINE_SPACING,
                                 StrId::STR_PARA_ALIGNMENT,
                                 StrId::STR_HYPHENATION,
                                 StrId::STR_FOCUS_READING,
                                 StrId::STR_EVEN_WORD_GAPS,
                                 StrId::STR_WORD_SPACING,
                                 StrId::STR_CHARACTER_SPACING,
                                 StrId::STR_SCREEN_MARGIN,
                                 StrId::STR_PARAGRAPH_INDENTATION,
                                 StrId::STR_EXPERIMENTAL,
                                 StrId::STR_EMBEDDED_STYLE,      // 0.73.0 (kos25k #29): a checkbox, PDFs too
                                 StrId::STR_SCROLL_MODE,         // kos25k #30: a checkbox
                                 StrId::STR_TYPOGRAPHY_ENGINE};  // kos25k #30: a list of its seven settings
inline constexpr int COUNT = static_cast<int>(std::size(ROWS));
// FormatFeatures.h goes by the same row numbers to leave out what a book's format can't use.
static_assert(COUNT == formatfeatures::TEXT_ROW_COUNT, "a new row: give it a bit in formatfeatures::textRowMask");
static_assert(ROWS[formatfeatures::TEXT_ROW_ALIGNMENT] == StrId::STR_PARA_ALIGNMENT, "the alignment row's number");
static_assert(ROWS[formatfeatures::TEXT_ROW_EMBEDDED_STYLE] == StrId::STR_EMBEDDED_STYLE, "the Style row's number");
}  // namespace textpanel
