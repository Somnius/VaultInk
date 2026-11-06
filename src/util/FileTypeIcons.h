#pragma once

#include <FreeInkUIIcon.h>

#include <string>
#include <string_view>

// List icons with the file type written inside (kos25k #15): a page for a book.
namespace filetypeicon {

// 44 px icon for a book or text file, by its extension; false for anything else (pictures, zips...).
bool forFile(std::string_view name, freeink::ui::BitmapRef& out);

}  // namespace filetypeicon
