#pragma once

#include <FreeInkUIIcon.h>

#include <string>
#include <string_view>

// List icons with the file type written inside (kos25k #15): a page for a book, a folder for a folder.
namespace filetypeicon {

// 44 px icon for a book or text file, by its extension; false for anything else (pictures, zips...).
bool forFile(std::string_view name, freeink::ui::BitmapRef& out);

// 40 px folder icon with the type most of the files in `path` have; false when the folder holds none of the
// known types. Counted once per folder and kept (the first look reads the folder, up to 300 entries).
bool forFolder(const std::string& path, freeink::ui::BitmapRef& out);

// Drop what was counted: a folder was reloaded, so its files may have changed.
void forget();

}  // namespace filetypeicon
