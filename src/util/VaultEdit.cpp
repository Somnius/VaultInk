#include "VaultEdit.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TaskToggle.h>
#include <TrustedTime.h>
#include <VaultIndex.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <set>

#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "util/VaultFiles.h"

namespace vaultedit {
namespace {

constexpr size_t MAX_NOTE_BYTES = 1024 * 1024;  // a note bigger than this isn't edited on the device

std::string stateDirOf(const std::string& vaultDir) {
  const std::string index = vaultfiles::indexPathFor(vaultDir);
  return index.substr(0, index.find_last_of('/'));
}

std::string changedListOf(const std::string& vaultDir) { return stateDirOf(vaultDir) + "/changed.txt"; }

std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == 0 ? "/" : path.substr(0, slash);
}

bool writeAtomic(const std::string& path, const std::string& text) {
  const std::string tmp = path + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("EDIT", tmp, file)) return false;
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
  file.close();
  if (!ok || !Storage.replaceFile(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

// The vault's file list goes stale when a note appears, moves or goes: the next open rescans.
void dropFileList(const std::string& vaultDir) { Storage.remove(vaultindex::fileListPathFor(vaultDir).c_str()); }

std::string bookCacheOf(const std::string& path) {
  return "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
}

// "-20261027-153012" from the trusted clock, else a counter-free fallback of the uptime.
std::string timeSuffix() {
  const time_t now = static_cast<time_t>(trustedtime::trustedNow());
  char buf[24];
  if (now > 0) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(buf, sizeof(buf), "-%Y%m%d-%H%M%S", &t);
  } else {
    snprintf(buf, sizeof(buf), "-%lu", static_cast<unsigned long>(millis()));
  }
  return buf;
}

}  // namespace

std::string vaultDirOf(const std::string& path) {
  const auto vaults = vaultfiles::findVaults();
  const auto* vault = vaultfiles::vaultOf(vaults, path);
  return vault ? vault->path : "";
}

bool markChanged(const std::string& path) {
  const std::string vaultDir = vaultDirOf(path);
  if (vaultDir.empty() || path.size() <= vaultDir.size() + 1) return true;  // not in a vault: nothing syncs it
  const std::string rel = path.substr(vaultDir.size() + 1);
  const std::string list = changedListOf(vaultDir);
  std::string text;
  // A list that is there but can't be read is left alone: rewriting it would drop the others.
  if (Storage.exists(list.c_str()) && !Storage.readFileToString("EDIT", list, 256 * 1024, text)) {
    LOG_ERR("EDIT", "Could not read %s", list.c_str());
    return false;
  }
  // One path per line, each once.
  if (("\n" + text).find("\n" + rel + "\n") != std::string::npos) return true;
  text += rel + "\n";
  Storage.ensureDirectoryExists(stateDirOf(vaultDir).c_str());
  if (!writeAtomic(list, text)) {
    LOG_ERR("EDIT", "Could not record %s as changed", rel.c_str());
    return false;
  }
  return true;
}

std::function<bool(const std::string&)> changedLookup(const std::string& vaultDir) {
  auto paths = std::make_shared<std::set<std::string>>();
  std::string text;
  const std::string list = changedListOf(vaultDir);
  if (Storage.exists(list.c_str()) && Storage.readFileToString("EDIT", list, 256 * 1024, text)) {
    size_t start = 0;
    while (start < text.size()) {
      size_t end = text.find('\n', start);
      if (end == std::string::npos) end = text.size();
      if (end > start) paths->insert(text.substr(start, end - start));
      start = end + 1;
    }
  }
  if (paths->empty()) return nullptr;
  return [paths](const std::string& rel) { return paths->count(rel) > 0; };
}

std::vector<std::string> changedList(const std::string& vaultDir) {
  std::vector<std::string> out;
  std::string text;
  const std::string list = changedListOf(vaultDir);
  if (!Storage.exists(list.c_str()) || !Storage.readFileToString("EDIT", list, 256 * 1024, text)) return out;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    if (end > start) out.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

bool unmarkChanged(const std::string& vaultDir, const std::string& relPath) {
  const std::string list = changedListOf(vaultDir);
  std::string text;
  if (!Storage.exists(list.c_str())) return true;
  if (!Storage.readFileToString("EDIT", list, 256 * 1024, text)) return false;
  std::string kept;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(start, end - start);
    if (!line.empty() && line != relPath) kept += line + "\n";
    start = end + 1;
  }
  if (kept.empty()) return Storage.remove(list.c_str());
  return writeAtomic(list, kept);
}

void recoverTmp(const std::string& path) {
  const std::string tmp = path + ".tmp";
  if (!Storage.exists(tmp.c_str())) return;
  if (Storage.exists(path.c_str())) {
    Storage.remove(tmp.c_str());
  } else if (Storage.rename(tmp.c_str(), path.c_str())) {
    LOG_INF("EDIT", "Recovered %s from its temp file", path.c_str());
  }
}

Result toggleTask(const std::string& notePath, const std::string& href) {
  uint32_t line = 0;
  uint32_t hash = 0;
  if (!tasktoggle::parseHref(href, line, hash)) return Result::Failed;
  std::string text;
  if (!Storage.readFileToString("EDIT", notePath, MAX_NOTE_BYTES, text)) return Result::Failed;
  const auto r = tasktoggle::toggle(text, line, hash);
  if (r != tasktoggle::Result::Ok) return Result::Changed;
  // Listed first: a power cut after the save must not leave a change a LiveSync pull overwrites.
  if (!markChanged(notePath) || !writeAtomic(notePath, text)) return Result::Failed;
  return Result::Ok;
}

bool readNote(const std::string& notePath, std::string& text) {
  if (Storage.readFileToString("EDIT", notePath, 256 * 1024, text)) return true;
  // readFileToString refuses an empty file; an empty note is fine.
  HalFile file;
  if (!Storage.openFileForRead("EDIT", notePath, file)) return false;
  const bool empty = !file.isDirectory() && file.fileSize() == 0;
  file.close();
  text.clear();
  return empty;
}

uint32_t contentHash(const std::string& text) { return tasktoggle::lineHash(text); }

Result saveNote(const std::string& notePath, const std::string& text, const uint32_t loadedHash,
                const size_t loadedSize) {
  std::string current;
  if (!readNote(notePath, current)) return Result::Failed;
  if (current.size() != loadedSize || contentHash(current) != loadedHash) return Result::Changed;
  if (!markChanged(notePath) || !writeAtomic(notePath, text)) return Result::Failed;
  return Result::Ok;
}

bool validTitle(const std::string& title) {
  if (title.empty() || title.size() > 100 || title[0] == '.' || title[0] == ' ') return false;
  if (title.back() == ' ' || title.back() == '.') return false;
  return std::none_of(title.begin(), title.end(), [](const char c) {
    return static_cast<unsigned char>(c) < 0x20 || std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos;
  });
}

Result createNote(const std::string& folder, const std::string& title, std::string& outPath) {
  if (!validTitle(title)) return Result::BadName;
  outPath = (folder.back() == '/' ? folder : folder + "/") + title + ".md";
  if (Storage.exists(outPath.c_str())) return Result::Exists;
  if (!markChanged(outPath) || !writeAtomic(outPath, "# " + title + "\n")) return Result::Failed;
  dropFileList(vaultDirOf(outPath));
  return Result::Ok;
}

Result renameNote(const std::string& notePath, const std::string& title, std::string& outPath) {
  if (!validTitle(title)) return Result::BadName;
  outPath = parentOf(notePath) + "/" + title + ".md";
  if (outPath == notePath) return Result::Ok;
  // To LiveSync this is a new note and a deleted one; both stay local until pushed.
  if (!markChanged(notePath) || !markChanged(outPath)) return Result::Failed;
  // FAT ignores case: "note.md" and "Note.md" are one file, so a case-only rename goes through a
  // temporary name.
  const bool caseOnly = Storage.exists(outPath.c_str());
  if (caseOnly) {
    const auto lower = [](std::string text) {
      std::transform(text.begin(), text.end(), text.begin(),
                     [](const unsigned char c) { return static_cast<char>(tolower(c)); });
      return text;
    };
    if (lower(outPath) != lower(notePath)) return Result::Exists;
    const std::string mid = notePath + ".renaming";
    if (!Storage.rename(notePath.c_str(), mid.c_str())) return Result::Failed;
    if (!Storage.rename(mid.c_str(), outPath.c_str())) {
      Storage.rename(mid.c_str(), notePath.c_str());
      return Result::Failed;
    }
  } else if (!Storage.rename(notePath.c_str(), outPath.c_str())) {
    return Result::Failed;
  }
  // The reading position and page cache come along (the cache is keyed by the path).
  const std::string oldCache = bookCacheOf(notePath);
  const std::string newCache = bookCacheOf(outPath);
  if (Storage.exists(oldCache.c_str()) && !Storage.exists(newCache.c_str())) {
    Storage.rename(oldCache.c_str(), newCache.c_str());
  }
  RECENT_BOOKS.updatePath(notePath, outPath, oldCache, newCache);
  if (APP_STATE.openEpubPath == notePath) {
    APP_STATE.openEpubPath = outPath;
    APP_STATE.saveToFile();
  }
  dropFileList(vaultDirOf(outPath));
  return Result::Ok;
}

Result trashNote(const std::string& notePath) {
  const std::string vaultDir = vaultDirOf(notePath);
  if (vaultDir.empty()) return Result::Failed;
  const std::string rel = notePath.substr(vaultDir.size() + 1);
  std::string target = vaultDir + "/.trash/" + rel;
  if (Storage.exists(target.c_str())) {
    const size_t dot = target.find_last_of('.');
    const size_t slash = target.find_last_of('/');
    const size_t at = dot != std::string::npos && dot > slash + 1 ? dot : target.size();
    target.insert(at, timeSuffix());
  }
  if (!markChanged(notePath)) return Result::Failed;
  Storage.ensureDirectoryExists(parentOf(target).c_str());
  if (!Storage.rename(notePath.c_str(), target.c_str())) return Result::Failed;
  Storage.removeDir(bookCacheOf(notePath).c_str());
  if (APP_STATE.openEpubPath == notePath) {
    APP_STATE.openEpubPath.clear();
    APP_STATE.saveToFile();
  }
  dropFileList(vaultDir);
  return Result::Ok;
}

}  // namespace vaultedit
