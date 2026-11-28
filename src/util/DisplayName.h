#pragma once

#include <GfxRenderer.h>
#include <Utf8.h>

#include <string>

#include "components/UIScale.h"

// A file, folder or note name as a UI row shows it: without emoji and without characters the UI font can't draw (a
// Hangul "ㅡ" in a folder name would be the missing-glyph diamond). For display only; paths keep the stored name.
inline std::string displayName(const GfxRenderer& renderer, const std::string& name,
                               const int fontId = uiScaleSpec().smallFontId) {
  struct Ctx {
    const GfxRenderer& renderer;
    int fontId;
  } ctx{renderer, fontId};
  return utf8DisplayName(
      name,
      [](const uint32_t cp, const void* c) {
        const auto* x = static_cast<const Ctx*>(c);
        return x->renderer.canDrawCodepoint(x->fontId, cp);
      },
      &ctx);
}
