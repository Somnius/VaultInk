#pragma once

// The book page number the reader shows in brackets in the status bar, "(37)" (kos25k #12). The reader sets it
// before it draws the bar; 0 means there is none (the bar then leaves it out).
namespace bookpage {
void set(int page);
int number();
}  // namespace bookpage
