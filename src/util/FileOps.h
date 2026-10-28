#pragma once

#include <string>

// Copying on the SD card, shared by the file browser and the gallery.
namespace fileops {

// Streams one file to "<to>.part", then renames it; a short write (card full) or a read error
// removes the part.
bool copyFile(const std::string& from, const std::string& to);
// Copies a file or a whole folder tree; stops at the first failure (the caller cleans up).
bool copyTree(const std::string& from, const std::string& to);

}  // namespace fileops
