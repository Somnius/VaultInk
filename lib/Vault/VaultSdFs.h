#pragma once

#include "VaultIndex.h"

// lib/Vault's file callbacks on the SD card (device only; host tests bring their own).
namespace vaultsd {

vaultindex::IndexFs fs();

}  // namespace vaultsd
