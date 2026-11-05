#pragma once

#include <string_view>

// What a LiveSync Setup URI must say about end-to-end encryption before the device uses it.
//
// The plugin's "Copy settings as a new setup URI" leaves out every setting that equals its default (it
// encodes with skipDefaultValue), and the plugin's default for E2EEAlgorithm is V2 ("v2"). So a vault set to V2
// has no E2EEAlgorithm in its Setup URI at all, and that is V2, not "unknown" (0.68.1: the device used to refuse
// exactly these URIs). A V1 vault writes "" and the Force-V1 choice "forceV1"; both are refused. `encrypt`
// defaults to false, so it is in the URI whenever it is on.
namespace livesync {

enum class SetupEncryption {
  Ok,
  EncryptionOff,    // end-to-end encryption is switched off in the vault
  LegacyAlgorithm,  // V1 (empty) or Force-V1: not supported, only V2
};

// `algorithmPresent` says whether the Setup URI carried an E2EEAlgorithm at all.
inline SetupEncryption checkSetupEncryption(const bool encrypt, const bool algorithmPresent,
                                            const std::string_view algorithm) {
  if (!encrypt) return SetupEncryption::EncryptionOff;
  if (!algorithmPresent) return SetupEncryption::Ok;  // left out because it is the default, V2
  return algorithm == "v2" ? SetupEncryption::Ok : SetupEncryption::LegacyAlgorithm;
}

}  // namespace livesync
