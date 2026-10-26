#pragma once

#include <string>

// Saved reading percentage, or -1 when unavailable. Never builds reading caches.
int loadBookProgress(const std::string& path);

// Percentage of the book on screen, published by the EPUB reader on every page draw so other
// screens (the quick-settings book card) can show it without loading the book again. -1 when no
// EPUB is open (the XTC and TXT readers don't publish yet).
void publishLiveBookProgress(int percent);
int liveBookProgress();
