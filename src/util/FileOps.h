#pragma once

#include <string>

// Copying, moving and deleting on the SD card, shared by the file browser, the file search and the gallery.
namespace fileops {

// Streams one file to "<to>.part", then renames it; a short write (card full) or a read error
// removes the part.
bool copyFile(const std::string& from, const std::string& to);
// Copies a file or a whole folder tree; stops at the first failure (the caller cleans up).
bool copyTree(const std::string& from, const std::string& to);
// Deletes a file or a whole folder tree, clearing each book's saved state on the way (one pass).
bool removeTree(const std::string& path);

enum class Transfer { Ok, Refused, Failed };
// Moves or copies `srcPath` into the folder `destDir` under its own name. Refused: onto itself, a folder
// into itself, or over an existing name (nothing is overwritten). On a failed copy (card full) nothing
// half-copied stays behind. `isDirectory` tells what was transferred; Ok on a move onto itself does nothing.
Transfer transferInto(const std::string& srcPath, const std::string& destDir, bool copy, bool& isDirectory);

}  // namespace fileops
