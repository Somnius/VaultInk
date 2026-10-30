#!/bin/bash

set -e

cd "$(dirname "$0")"

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
# Arrows and ballot boxes, for every built-in font (see the font stacks below).
SYMBOL_INTERVALS=(--additional-intervals 0x2190,0x2193 --additional-intervals 0x2610,0x2612)
NOTOSANS_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    # Greek, monotonic and polytonic, so Greek books and notes read without an SD-card font.
    # Arrows (U+2190-2193, Noto Sans Symbols) and ballot boxes (U+2610-2612, Noto Sans Symbols 2):
    # Noto Sans has neither; menu paths use the arrows, note tasks the boxes.
    case $style in Bold|BoldItalic) sym_style=Bold ;; *) sym_style=Regular ;; esac
    python fontconvert.py $font_name $size $font_path \
      ../builtinFonts/source/NotoSansSymbols/NotoSansSymbols-${sym_style}.ttf \
      ../builtinFonts/source/NotoSansSymbols2/NotoSansSymbols2-Regular.ttf \
      --2bit --compress --pnum --zopfli \
      --additional-intervals 0x0370,0x03FF --additional-intervals 0x1F00,0x1FFF \
      "${SYMBOL_INTERVALS[@]}" > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

# Arabic glyphs for UI text (menus, file browser titles). The built-in fonts
# must cover the *output* of MiniBidi's do_shape() — contextual presentation
# forms — not base letters, or shaped UI text silently drops glyphs.
# Curated for firmware-size budget: core Arabic (Presentation Forms-B,
# incl. the Lam-Alef ligature forms) plus the Farsi/Urdu extra letters'
# Presentation Forms-A blocks, the few characters shaping leaves at their
# base codepoint, Arabic punctuation, and both digit sets. No harakat and
# no Sindhi/Pashto/Kurdish forms — book text gets those from SD-card fonts.
ARABIC_INTERVALS=(
  --additional-intervals 0x060C,0x060C  # Arabic comma
  --additional-intervals 0x061B,0x061B  # Arabic semicolon
  --additional-intervals 0x061F,0x061F  # Arabic question mark
  --additional-intervals 0x0621,0x0621  # hamza (non-joining, never shaped)
  --additional-intervals 0x0640,0x0640  # tatweel
  --additional-intervals 0x0654,0x0654  # Persian/Urdu ezafe hamza, Arabic hamza carriers
  --additional-intervals 0x0660,0x0669  # Arabic-Indic digits
  --additional-intervals 0x06BA,0x06BA  # noon ghunna base (initial/medial keep base cp)
  --additional-intervals 0x06D4,0x06D4  # Urdu full stop
  --additional-intervals 0x06D5,0x06D5  # ae (isolated; Kurdish/Uyghur/Ottoman) — has no presentation form
  --additional-intervals 0x06F0,0x06F9  # extended Arabic-Indic digits (Farsi/Urdu)
  --additional-intervals 0xFB56,0xFB59  # peh (Farsi)
  --additional-intervals 0xFB66,0xFB69  # tteh (Urdu)
  --additional-intervals 0xFB7A,0xFB7D  # tcheh (Farsi)
  --additional-intervals 0xFB88,0xFB95  # ddal, jeh, rreh (Urdu), keheh, gaf (Farsi/Urdu)
  --additional-intervals 0xFB9E,0xFB9F  # noon ghunna isolated/final (Urdu)
  --additional-intervals 0xFBA6,0xFBB1  # heh goal, heh doachashmee, yeh barree(+hamza) (Urdu)
  --additional-intervals 0xFBFC,0xFBFF  # farsi yeh (Farsi/Urdu)
  --additional-intervals 0xFE80,0xFEFC  # Presentation Forms-B: core Arabic + Lam-Alef
)

# Greek, monotonic and polytonic, for UI text and Greek file and note names.
GREEK_INTERVALS=(--additional-intervals 0x0370,0x03FF --additional-intervals 0x1F00,0x1FFF)

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    latin_fallback_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    hebrew_path="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-${style}.ttf"
    arabic_path="../builtinFonts/source/NotoSansArabic/NotoSansArabic-${style}.ttf"
    # Ubuntu lacks the Latin Extended Additional block (U+1EA0-U+1EF9) used for
    # Vietnamese tone marks. Append a Vietnamese-only Ubuntu cut so those glyphs
    # are filled from it while every glyph Ubuntu already has stays unchanged
    # (fontstack is ordered by descending priority).
    viet_path="../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    symbols_path="../builtinFonts/source/NotoSansSymbols/NotoSansSymbols-${style}.ttf"
    symbols2_path="../builtinFonts/source/NotoSansSymbols2/NotoSansSymbols2-Regular.ttf"
    python fontconvert.py $font_name $size $font_path $latin_fallback_path $hebrew_path $arabic_path $viet_path \
      $symbols_path $symbols2_path \
      --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" "${GREEK_INTERVALS[@]}" \
      "${SYMBOL_INTERVALS[@]}" > $output_path
    echo "Generated $output_path"
  done
done

# The small UI face is Ubuntu too, so UI text stays in one family at every size.
python fontconvert.py ubuntu_8_regular 8 \
  ../builtinFonts/source/Ubuntu/Ubuntu-Regular.ttf \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf \
  ../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf \
  ../builtinFonts/source/NotoSansArabic/NotoSansArabic-Regular.ttf \
  ../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-Regular.ttf \
  ../builtinFonts/source/NotoSansSymbols/NotoSansSymbols-Regular.ttf \
  ../builtinFonts/source/NotoSansSymbols2/NotoSansSymbols2-Regular.ttf \
  --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" "${GREEK_INTERVALS[@]}" \
  "${SYMBOL_INTERVALS[@]}" > ../builtinFonts/ubuntu_8_regular.h

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
