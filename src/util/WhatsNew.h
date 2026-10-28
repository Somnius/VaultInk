#pragma once

#include <string>

// The changelog on the device (kos25k #9): after an update, the first boot opens a note with what
// changed since the version run before, and Settings → System → What's New shows the latest
// releases any time. The text is CHANGELOG.md, carried in the firmware (scripts/embed_changelog.py).
namespace whatsnew {

// Called once at boot. True when this boot follows an update and the note was written to `path`.
// Remembers the running version either way; a first install shows the welcome guide instead.
bool afterUpdate(std::string& path);

// Writes every release the firmware carries to the note; false when it couldn't be written.
bool writeAll(std::string& path);

}  // namespace whatsnew
