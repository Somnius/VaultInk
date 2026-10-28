#include "FileOps.h"

#include <HalStorage.h>
#include <Memory.h>

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

}  // namespace fileops
