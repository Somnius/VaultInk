#pragma once

#include <string>
#include <string_view>

#include "VaultIndex.h"

// The note Home opens inside a vault. Pure (file access through IndexFs), so it tests on the host.
namespace vaulthome {

// The Homepage plugin's note from `.obsidian/plugins/homepage/data.json` ("homepages": {...: {"value": "Note",
// "kind": "File"}} or the older top-level "defaultNote"); "" when there is none or it isn't a note.
std::string homepageSetting(std::string_view json);

// False for a Homepage value that could leave the vault: empty, absolute, a backslash or a ".." part.
bool safeNotePath(std::string_view note);

// The vault's home note: the Homepage plugin's note (its path from the vault root, checked directly), else Home.md or
// index.md at the vault root (any case), else "".
std::string resolve(const std::string& root, const vaultindex::IndexFs& fs);

}  // namespace vaulthome
