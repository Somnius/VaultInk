#pragma once

#include <VaultIndex.h>

#include <string>
#include <vector>

// The vaults on the SD card and SD-backed file access for lib/Vault.
namespace vaultfiles {

constexpr const char* VAULTS_ROOT = "/Vaults";
constexpr size_t MAX_VAULTS = 32;

struct Vault {
  std::string name;
  std::string path;
};

// Every folder in /Vaults, and top-level folders that hold .obsidian, sorted by name.
std::vector<Vault> findVaults();

// The search index of a vault: /.vaultink/<id>/index.tsv, outside the vault.
std::string indexPathFor(const std::string& vaultPath);

// lib/Vault's file callbacks on the SD card.
vaultindex::IndexFs sdFs();

}  // namespace vaultfiles
