#include "VaultSdFs.h"

#include <HalStorage.h>

#include <algorithm>

namespace vaultsd {
namespace {

constexpr size_t NAME_LEN = 256;
constexpr size_t LINE_CAP = vaultindex::MAX_LINE_BYTES + 1024;  // index lines are written below this

// The file being written: a temp file renamed over the old one on commit.
struct Writer {
  HalFile file;
  std::string path;
  std::string tmp;
};
Writer writer;

}  // namespace

vaultindex::IndexFs fs() {
  vaultindex::IndexFs fs;
  fs.exists = [](const std::string& path) { return Storage.exists(path.c_str()); };
  fs.list = [](const std::string& dir, const std::function<void(const std::string&, bool)>& entry) {
    HalFile root = Storage.open(dir.c_str());
    if (!root || !root.isDirectory()) return;
    char name[NAME_LEN];
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
      file.getName(name, sizeof(name));
      entry(name, file.isDirectory());
    }
  };
  fs.stamp = [](const std::string& path) -> uint64_t {
    HalFile file;
    if (!Storage.openFileForRead("VLT", path, file)) return 0;
    return (static_cast<uint64_t>(file.modificationTime()) << 32) | static_cast<uint32_t>(file.fileSize()) | 1u << 31;
  };
  // The first `cap` bytes; empty notes are fine (they are still found by title).
  fs.read = [](const std::string& path, const size_t cap, std::string& out) {
    out.clear();
    HalFile file;
    if (!Storage.openFileForRead("VLT", path, file) || file.isDirectory()) return false;
    const size_t size = std::min(cap, static_cast<size_t>(file.fileSize()));
    out.resize(size);
    // A short read would store a cut note as complete; treat it as a failed read.
    return size == 0 || file.read(out.data(), size) == static_cast<int>(size);
  };
  fs.lines = [](const std::string& path, const std::function<bool(std::string_view)>& line) {
    HalFile file;
    if (!Storage.exists(path.c_str()) || !Storage.openFileForRead("VLT", path, file)) return false;
    std::string current;
    uint8_t buf[1024];
    int n = 0;
    while ((n = file.read(buf, sizeof(buf))) > 0) {
      for (int i = 0; i < n; ++i) {
        if (buf[i] == '\n') {
          if (!line(current)) return true;
          current.clear();
        } else if (current.size() < LINE_CAP) {
          current += static_cast<char>(buf[i]);
        }
      }
    }
    if (!current.empty()) line(current);
    return true;
  };
  fs.begin = [](const std::string& path) {
    const size_t slash = path.find_last_of('/');
    Storage.ensureDirectoryExists(path.substr(0, slash).c_str());
    if (writer.file) writer.file.close();  // a build that never committed
    writer.path = path;
    writer.tmp = path + ".tmp";
    return Storage.openFileForWrite("VLT", writer.tmp, writer.file);
  };
  fs.append = [](std::string_view text) { return writer.file.write(text.data(), text.size()) == text.size(); };
  fs.commit = [](const bool keep) {
    writer.file.flush();
    writer.file.close();
    if (!keep) {
      Storage.remove(writer.tmp.c_str());
      return true;
    }
    return Storage.replaceFile(writer.tmp.c_str(), writer.path.c_str());
  };
  fs.known = [lines = fs.lines](const std::string& root, const std::function<void(std::string_view)>& path) {
    return lines(vaultindex::fileListPathFor(root), [&](std::string_view line) {
      path(line);
      return true;
    });
  };
  return fs;
}

}  // namespace vaultsd
