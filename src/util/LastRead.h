#pragma once

#include <string>

// The path of the last thing opened to read, book or vault note (/.vaultink/last-read.txt). The Books list stays
// books only; this lets the Home card resume a note too.
namespace lastread {

void remember(const std::string& path);
// The vault note read last, if it was read after every book and still exists; else "".
std::string note();

}  // namespace lastread
