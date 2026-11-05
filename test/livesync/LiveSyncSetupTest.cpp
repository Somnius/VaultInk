#include <gtest/gtest.h>

#include "LiveSyncSetup.h"

using livesync::checkSetupEncryption;
using livesync::SetupEncryption;

TEST(LiveSyncSetup, V2IsAcceptedWhenWrittenOut) {
  EXPECT_EQ(checkSetupEncryption(true, true, "v2"), SetupEncryption::Ok);
}

// A user reported: both flags true in the plugin's data.json, yet "Turn on end-to-end encryption (V2)". The
// plugin leaves a default-valued setting out of the Setup URI, and V2 is its default.
TEST(LiveSyncSetup, ALeftOutAlgorithmIsTheDefaultV2) {
  EXPECT_EQ(checkSetupEncryption(true, false, ""), SetupEncryption::Ok);
}

TEST(LiveSyncSetup, V1AndForceV1AreRefused) {
  EXPECT_EQ(checkSetupEncryption(true, true, ""), SetupEncryption::LegacyAlgorithm);
  EXPECT_EQ(checkSetupEncryption(true, true, "forceV1"), SetupEncryption::LegacyAlgorithm);
  EXPECT_EQ(checkSetupEncryption(true, true, "v3"), SetupEncryption::LegacyAlgorithm);
}

TEST(LiveSyncSetup, EncryptionOffIsRefusedWhateverTheAlgorithm) {
  EXPECT_EQ(checkSetupEncryption(false, true, "v2"), SetupEncryption::EncryptionOff);
  EXPECT_EQ(checkSetupEncryption(false, false, ""), SetupEncryption::EncryptionOff);
}
