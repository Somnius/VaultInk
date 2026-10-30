#pragma once

#include <string>

// The loaded book's clippings written to a note in a vault (Phase 4 follow-up, "highlights to a vault note"):
// <vault>/Highlights/<book title>.md, one block quote each with the chapter as a heading. Exporting again
// replaces the note this wrote; a note of the same name that wasn't an export is never touched (the new one
// gets "(highlights)" after its name). The note is recorded as changed on the device, so LiveSync sends it.
namespace clippingexport {

enum class Status { Ok, NoClippings, NoVault, SdError };

// Exports the clippings of the book loaded in CLIPPINGS into the vault read most recently (the only vault
// when there is one). `notePath` is the note written.
Status exportToVault(std::string& notePath);

}  // namespace clippingexport
