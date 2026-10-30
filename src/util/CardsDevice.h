#pragma once

#include <CardIndex.h>
#include <CardStore.h>

#include <string>

#include "util/VaultFiles.h"

class GfxRenderer;  // forward-declared: callers pass theirs

// The flashcards on the device (Phase 10): the SD-card side of lib/Vault's card store and index.
namespace cardsdev {

constexpr const char* TAG = "flashcards";

// Today's day number in local time, or 0 when the clock was never set.
uint32_t today();

cardstore::StoreFs storeFs();
cardsched::Config config();
cardindex::SessionSpec sessionDefaults();

// Brings the vault's search index and card index up to date (with the "Updating" popup when notes
// changed) and compacts the journal when it grew past 1 MB.
void refresh(const GfxRenderer& renderer, const vaultfiles::Vault& vault);

// The decks of a vault with their counts (after refresh).
std::vector<cardindex::DeckInfo> decks(const vaultfiles::Vault& vault);

}  // namespace cardsdev
