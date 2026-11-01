#include "FileOps.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <utility>
#include <vector>

#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace fileops {
namespace {
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

// Streams one file to a ".part" name, then renames it; a short write (card full) removes the part.
bool copyFile(const std::string& from, const std::string& to) {
  HalFile in;
  HalFile out;
  const std::string part = to + ".part";
  auto buf = makeUniqueNoThrow<uint8_t[]>(4096);
  if (!buf || !Storage.openFileForRead("FileBrowser", from, in)) return false;
  if (!Storage.openFileForWrite("FileBrowser", part, out)) {
    in.close();
    return false;
  }
  bool ok = true;
  for (;;) {
    const int n = in.read(buf.get(), 4096);
    if (n <= 0) {
      ok = n == 0;  // a read error is not the end of the file
      break;
    }
    if (out.write(buf.get(), static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
    resetTaskWatchdogIfSubscribed();
  }
  in.close();
  out.close();
  if (ok) ok = Storage.rename(part.c_str(), to.c_str());
  if (!ok) Storage.remove(part.c_str());
  return ok;
}

// Copies a file or a whole folder tree; stops at the first failure. The name buffer is on the
// heap: one recursion level per folder depth.
bool copyTree(const std::string& from, const std::string& to) {
  auto dir = Storage.open(from.c_str());
  if (!dir) return false;
  if (!dir.isDirectory()) {
    dir.close();
    return copyFile(from, to);
  }
  auto name = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!name || !Storage.mkdir(to.c_str())) {
    dir.close();
    return false;
  }
  bool ok = true;
  for (auto entry = dir.openNextFile(); ok && entry; entry = dir.openNextFile()) {
    entry.getName(name.get(), NAME_BUFFER_SIZE);
    entry.close();
    ok = copyTree(from + "/" + name.get(), to + "/" + name.get());
  }
  dir.close();
  return ok;
}

// One pass: each file's book state goes with it, then the emptied folders (post-order).
bool removeTree(const std::string& path) {
  auto file = Storage.open(path.c_str());
  if (!file) {
    LOG_ERR("FileOps", "Failed to open for removal: %s", path.c_str());
    return false;
  }
  if (!file.isDirectory()) {
    file.close();
    return removeBookFile(path);
  }
  file.close();
  auto name = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!name) return false;

  // (dirPath, postOrder): postOrder = rmdir this path, its children are done.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({path, false});
  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();
    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileOps", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }
    auto dir = Storage.open(currentPath.c_str());
    if (!dir || !dir.isDirectory()) {
      LOG_ERR("FileOps", "Not a readable directory: %s", currentPath.c_str());
      return false;
    }
    stack.push_back({currentPath, true});
    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name.get(), NAME_BUFFER_SIZE);
      if (strcmp(name.get(), ".") == 0 || strcmp(name.get(), "..") == 0) continue;
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') entryPath += "/";
      entryPath += name.get();
      const bool isDir = entry.isDirectory();
      entry.close();
      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else if (!removeBookFile(entryPath)) {
        LOG_ERR("FileOps", "Failed to remove file: %s", entryPath.c_str());
        return false;
      }
    }
  }
  return true;
}

Transfer transferInto(const std::string& srcPath, const std::string& destDir, const bool copy, bool& isDirectory) {
  const std::string name = srcPath.substr(srcPath.find_last_of('/') + 1);
  const std::string dest = (destDir.back() == '/' ? destDir : destDir + "/") + name;
  isDirectory = false;
  if (dest == srcPath && !copy) return Transfer::Ok;  // moved onto itself: nothing to do
  // A folder can't go inside itself, and nothing overwrites an existing name.
  if (dest == srcPath || dest.rfind(srcPath + "/", 0) == 0 || Storage.exists(dest.c_str())) return Transfer::Refused;
  bool ok;
  if (copy) {
    ok = copyTree(srcPath, dest);
    if (!ok) removeTree(dest);  // nothing half-copied stays behind
  } else {
    ok = moveBookWithState(srcPath, dest);
  }
  auto probe = Storage.open(ok && !copy ? dest.c_str() : srcPath.c_str());
  isDirectory = probe && probe.isDirectory();
  if (probe) probe.close();
  if (!ok) LOG_ERR("FileOps", "%s failed: %s -> %s", copy ? "Copy" : "Move", srcPath.c_str(), dest.c_str());
  return ok ? Transfer::Ok : Transfer::Failed;
}

}  // namespace fileops
