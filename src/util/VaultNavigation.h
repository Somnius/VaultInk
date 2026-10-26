#pragma once

#include <string>

// Following [[links]] between vault notes in the reader: resolves a "vault:" href to a file on the
// SD card, remembers where it came from so Back returns there, and carries the heading to jump to
// into the next reader.
namespace vaultnav {

// SameNote: the link points into the note on screen; `sameNoteAnchor` says where.
enum class Follow { NotVaultLink, NotFound, Opened, SameNote };

// `currentPath` is the note on screen. Opens the target note in a new reader when it resolves.
Follow follow(const std::string& currentPath, const std::string& href, std::string* sameNoteAnchor = nullptr);

// Back in a note: true (and the previous note reopens) when there is a note to go back to.
bool goBack();

// The anchor id the newly opened reader should jump to (taken once), or "".
std::string takeAnchor();

// The reader calls this when it opens `path`: a note not opened by follow()/goBack() (picked in
// Files, Home, a book) starts a fresh history.
void opened(const std::string& path);

}  // namespace vaultnav
