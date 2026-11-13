// The read-only link of the user-installable developer build (env x4pro-dev-user:
// VAULTINK_DEVLINK + VAULTINK_DEVLINK_USER, no VAULTINK_DEVLINK_FULL). The commands that act on
// the reader (keys, touches, SD files, flashing) must not be in its name table or its HELP text.
#include <gtest/gtest.h>

#include <string>

#include "DevLink.h"

#if !defined(VAULTINK_DEVLINK) || !defined(VAULTINK_DEVLINK_USER) || defined(VAULTINK_DEVLINK_FULL)
#error "This test is built as the read-only link: VAULTINK_DEVLINK and VAULTINK_DEVLINK_USER, not _FULL"
#endif

namespace {

using devlink::Command;

Command command(const std::string& line) { return devlink::parse(line.data(), line.size()).command; }

}  // namespace

TEST(UserLink, KnowsTheReadOnlyCommands) {
  EXPECT_EQ(command("CMD:PING"), Command::Ping);
  EXPECT_EQ(command("CMD:HELP"), Command::Help);
  EXPECT_EQ(command("CMD:STATUS"), Command::Status);
  EXPECT_EQ(command("CMD:LOG 2"), Command::Log);
  EXPECT_EQ(command("CMD:BENCH"), Command::Bench);
  EXPECT_EQ(command("CMD:REBOOT"), Command::Reboot);
  EXPECT_EQ(command("CMD:SCREENSHOT"), Command::Screenshot);
}

TEST(UserLink, TheCommandsThatActAreUnknown) {
  EXPECT_EQ(command("CMD:KEY BACK"), Command::Unknown);
  EXPECT_EQ(command("CMD:TAP 10 20"), Command::Unknown);
  EXPECT_EQ(command("CMD:SWIPE 1 2 3 4"), Command::Unknown);
  EXPECT_EQ(command("CMD:FLASH /firmware.bin"), Command::Unknown);
  EXPECT_EQ(command("CMD:LS /debug"), Command::Unknown);
  EXPECT_EQ(command("CMD:GET /debug/a.txt 0 100"), Command::Unknown);
  EXPECT_EQ(command("CMD:PUT /debug/a.txt 0 YQ=="), Command::Unknown);
}

TEST(UserLink, HelpListsOnlyWhatIsAnswered) {
  const std::string list = devlink::commandList();
  EXPECT_EQ(list, "PING HELP STATUS LOG BENCH REBOOT SCREENSHOT");
  const std::string padded = " " + list + " ";
  for (const char* name : {"BOOTLOADER", "KEY", "TAP", "SWIPE", "FLASH", "LS", "GET", "PUT"}) {
    EXPECT_EQ(padded.find(std::string(" ") + name + " "), std::string::npos) << name;
  }
  size_t start = 0;
  while (start < list.size()) {
    size_t end = list.find(' ', start);
    if (end == std::string::npos) end = list.size();
    EXPECT_NE(command("CMD:" + list.substr(start, end - start)), Command::Unknown);
    start = end + 1;
  }
}

// BOOTLOADER is in the name table every build shares; src/util/DevLinkDevice.cpp refuses it
// without VAULTINK_DEVLINK_FULL (that file needs the device and is not built here).
TEST(UserLink, BootloaderStillParsesSoTheDeviceCanRefuseIt) {
  EXPECT_EQ(command("CMD:BOOTLOADER"), Command::Bootloader);
}
