#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Ticking a task checkbox in a note. The converter writes each task box as a link
// "task:<line>:<hash>": the 0-based source line and a hash of that whole line, so a tap on a note
// that changed since it was laid out (a line added above, the task edited) is refused instead of
// flipping another line.
namespace tasktoggle {

// FNV-1a 32-bit of a source line (without its line ending).
uint32_t lineHash(std::string_view line);

// "task:12:1a2b3c4d".
std::string href(uint32_t line, uint32_t hash);
bool parseHref(std::string_view href, uint32_t& line, uint32_t& hash);

// Offset of the character inside a task's brackets ("- [ ] x", "  * [x] y", "3. [ ] z"), or npos
// when the line is not a list task.
size_t markOffset(std::string_view line);

enum class Result : uint8_t { Ok, NotFound, Changed, NotATask };

// Flips the task on `line` of `text` in place: a space becomes 'x', any other mark a space. Line
// endings (LF or CRLF) are left as they are.
Result toggle(std::string& text, uint32_t line, uint32_t hash);

}  // namespace tasktoggle
