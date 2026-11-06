#pragma once

#include <string>

class GfxRenderer;

// A list row's name and file type as one label that uses both of its lines (kos25k #15): a short name keeps the
// type right after it, a long one is cut with two dots only where the second line really ends and keeps the type
// last, and a name that fills both lines on its own drops the type.
namespace rowfit {

// `name` without its extension, `ext` with its dot (may be empty), `star` a prefix such as "★ " (may be empty).
std::string nameAndType(GfxRenderer& renderer, int fontId, int width, int maxLines, const std::string& star,
                        const std::string& name, const std::string& ext);

}  // namespace rowfit
