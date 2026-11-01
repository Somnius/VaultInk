#pragma once

#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
int loadBookProgress(const std::string& path);

// The same, but a reflowable book that was never opened costs one file check instead of loading its
// metadata: returns 0 for it. Used by the Library, which asks for many books at once.
int loadBookProgressQuick(const std::string& path);

// Percentage of the book on screen, published by the EPUB reader on every page draw so other
// screens (the quick-settings book card) can show it without loading the book again. -1 when no
// EPUB is open (the XTC and TXT readers don't publish yet).
void publishLiveBookProgress(int percent);
int liveBookProgress();
