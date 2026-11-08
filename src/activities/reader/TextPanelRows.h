#pragma once

#include <I18n.h>

#include <iterator>

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
                                 StrId::STR_EMBEDDED_STYLE};  // 0.73.0 (kos25k #29): a checkbox, PDFs too
inline constexpr int COUNT = static_cast<int>(std::size(ROWS));
}  // namespace textpanel
