#include "LastRead.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <VaultPaths.h>

namespace lastread {
namespace {
constexpr char FILE_PATH[] = "/.vaultink/last-read.txt";

std::string read() {
  std::string path;
  if (!Storage.exists(FILE_PATH) || !Storage.readFileToString("LAST", FILE_PATH, 1024, path)) return {};
  while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
  return path;
}
}  // namespace

void remember(const std::string& path) {
  if (path.empty() || read() == path) return;  // reopening the same thing writes nothing
  Storage.ensureDirectoryExists("/.vaultink");
  if (!Storage.writeFile(FILE_PATH, String(path.c_str()))) LOG_ERR("LAST", "Could not save the last read path");
}

std::string note() {
  const std::string path = read();
  if (!vaultpaths::isUnderVaults(path) || !FsHelpers::hasMarkdownExtension(path)) return {};
  return Storage.exists(path.c_str()) ? path : std::string();
}

}  // namespace lastread
