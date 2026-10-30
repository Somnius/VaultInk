#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

// Rewriting wiki-links after a note is renamed (Phase 4). Pure text work, no SD access: the
// caller says which link targets lead to the renamed note (resolving them before the rename).
namespace linkrewrite {

// Changes every [[target]], [[target|alias]], [[target#heading]], [[target#^block]] and ![[...]]
// in `text` whose target (as written, before '|' or '#') `points` accepts, so it names `newName`
// instead. Markdown links to the note, `[text](Folder/Old%20Name.md#anchor)`, change too (`points`
// gets the path with %20 turned into spaces). A folder prefix ("People/Old") and a ".md" ending are kept; the alias and
// anchor are kept byte for byte. Fenced code blocks (``` or ~~~) and `inline code` are left alone, and so is everything
// else in the text. Returns how many links changed.
size_t rewrite(std::string& text, const std::function<bool(std::string_view target)>& points, std::string_view newName);

}  // namespace linkrewrite
