#pragma once

#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, or TXT). Does nothing for other file types.
void clearBookCache(const std::string& path);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);

// Delete a local book and its local clipping state after the file is removed.
bool removeBookFile(const std::string& path);

// Renames or moves a book or a folder on the card. A book's cache, bookmarks and clippings move
// with it (rolled back on failure), and its Recent, Favourites and open-book entries follow. A
// folder moves as one rename with its books' clippings; then each book inside gets the same.
bool moveBookWithState(const std::string& oldPath, const std::string& newPath);
