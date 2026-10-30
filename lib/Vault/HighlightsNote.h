#pragma once

#include <string>
#include <vector>

// A book's highlights as an Obsidian note (clippings exported from the reader). Pure text, no SD access.
namespace highlightsnote {

struct Highlight {
  std::string chapter;  // "" when the highlight has none
  std::string text;     // may hold several paragraphs
};

// First line after the title block; a later export replaces a note that has it and never one that doesn't.
constexpr const char* MARKER = "%% VaultInk highlights %%";

// "# Title", the author in italics, the marker, then each highlight as a block quote, with a "## Chapter" heading
// whenever the chapter changes. Line endings are LF; runs of blank lines in a highlight collapse to one.
std::string build(const std::string& title, const std::string& author, const std::vector<Highlight>& highlights);

// True when `existing` was written by build() (starts with a title line and the marker).
bool isExport(const std::string& existing);

// A file name for a title: forbidden characters become '_', leading dots go, at most 100 bytes without cutting a
// UTF-8 character; "Highlights" when nothing is left.
std::string fileNameFor(const std::string& title);

}  // namespace highlightsnote
