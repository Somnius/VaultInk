#pragma once

#include <CardIndex.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/VaultFiles.h"

// The flashcard decks (Phase 10): the vaults that have cards, then a vault's decks with how many
// are due and new. Picking a deck (or All Cards) starts a review.
class FlashcardsActivity final : public UiListActivity {
 public:
  explicit FlashcardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;

 private:
  struct VaultCards {
    vaultfiles::Vault vault;
    std::vector<cardindex::DeckInfo> decks;
    size_t cards = 0;
    size_t due = 0;
    size_t fresh = 0;
  };

  int listCount() const override { return static_cast<int>(rowItems.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  void load();
  void buildRows();
  void startReview(const std::string& deck, bool all);

  std::vector<VaultCards> vaults;
  int openVault = -1;  // the vault whose decks are listed; -1 = the vault list
  bool clockUnset = false;
  std::vector<std::string> labels;
  std::vector<std::string> values;
  std::vector<freeink::ui::ListItem> rowItems;
};
