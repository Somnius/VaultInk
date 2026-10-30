#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <functional>

#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavouritesStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookInfo.h"
#include "util/BookmarkUtil.h"
#include "util/FileOps.h"
#include "util/TaskWatchdog.h"
#include "util/ZipExtract.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
// The folder picker's first row: "put it in the folder shown". No real entry ends in "./".
constexpr const char* HERE_ENTRY = "./";

}  // namespace

std::string getFileExtension(const std::string& filename);

void formatFileName(const std::string& filename, char* buffer, const size_t bufferSize) {
  if (filename.empty()) {
    buffer[0] = '\0';
    return;
  }
  const bool isDirectory = filename.back() == '/';
  const size_t dot = isDirectory ? filename.size() - 1 : filename.rfind('.');
  const int length = static_cast<int>(dot == std::string::npos ? filename.size() : dot);
  const char* format = isDirectory && !UITheme::getInstance().getTheme().showsFileIcons() ? "[%.*s]" : "%.*s";
  snprintf(buffer, bufferSize, format, length, filename.c_str());
  // Compose only the display copy; filesystem lookup needs the raw entry bytes.
  utf8ComposeNfcInPlace(buffer);
}

void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {
  buffer[0] = '\0';
  if (filename.empty() || filename.back() == '/') return;
  if (const char* extension = strrchr(filename.c_str(), '.')) snprintf(buffer, bufferSize, "%s", extension);
}

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

void FileBrowserActivity::loadFiles() {
  files.clear();
  prewarmedStart = -1;  // new folder contents: re-prewarm the visible window

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    return;
  }

  // Size `files` once before the fill loop: doubling growth on a large folder
  // holds the old and new string blocks live across the final realloc. The
  // count-only pass reads no names, so filtered-out entries just over-reserve
  // by a few slots.
  size_t entryCount = 0;
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) ++entryCount;
  root.rewindDirectory();
  files.reserve(entryCount);

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDirectory = file.isDirectory();
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (isDirectory) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else if (mode == Mode::PickFolder) {
      continue;
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else if (FsHelpers::hasReflowableBookExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasCbzExtension(filename) || FsHelpers::hasImageExtension(filename) ||
                 FsHelpers::hasCanvasExtension(filename) || FsHelpers::hasPdfExtension(filename) ||
                 FsHelpers::checkFileExtension(filename, ".zip")) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
  if (mode == Mode::PickFolder) files.insert(files.begin(), HERE_ENTRY);
}

// fui::ListProps::rowProvider — formats row `index` from files[index] into the
// activity's scratch buffers on demand. Runs on the render task only for the
// rows the list actually lays out, so nothing per-file is materialized beyond
// `files` itself. The label/value pointers stay valid until the next call,
// which is all the provider contract requires. The "[folder]" format is
// theme-dependent, but it's re-derived here on every
// repaint, so a theme change picked up while this activity was paused
// underneath another screen needs no cache invalidation.
void FileBrowserActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<FileBrowserActivity*>(ctx);
  if (index >= self->files.size()) return;
  const std::string& entry = self->files[index];
  if (entry == HERE_ENTRY) {
    snprintf(self->rowNameBuf, sizeof(self->rowNameBuf), "%s", tr(STR_CHOOSE_THIS_FOLDER));
    item.label = self->rowNameBuf;
    item.actionValue = static_cast<int16_t>(index);
    return;
  }
  formatFileName(entry, self->rowNameBuf, sizeof(self->rowNameBuf));
  item.label = self->rowNameBuf;
  formatFileExtension(entry, self->rowExtBuf, sizeof(self->rowExtBuf));
  if (self->rowExtBuf[0] != '\0') {
    item.value = self->rowExtBuf;
  }
  item.icon = listIconFor(UITheme::getFileIcon(entry));
  item.actionValue = static_cast<int16_t>(index);
}

// Batch-prewarm the CJK fallback glyphs for a bounded window of display names
// starting at the viewport top — one SD pass per list page (the reader TOC's
// refreshTocWindow pattern) instead of one unbounded pass over the whole
// folder. Getter form: no concatenated copy (a bare-new string append aborts
// under heap pressure). The last index covers the bottom path band: basepath
// (possibly a CJK folder name) draws in the same small font, so it must live
// in the same batch or it would evict the rows' glyphs when the heap gate
// disables union merging. (prewarmFallbackText appends the truncation
// ellipsis.)
void FileBrowserActivity::prewarmRowGlyphs(const int start) {
  const int total = static_cast<int>(files.size());
  int clamped = start;
  if (clamped > total - PREWARM_WINDOW) clamped = total - PREWARM_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == prewarmedStart) return;
  prewarmedStart = clamped;
  const int count = total - clamped < PREWARM_WINDOW ? total - clamped : PREWARM_WINDOW;

  struct PrewarmCtx {
    FileBrowserActivity* self;
    int first;
    int count;
  } prewarmCtx{this, clamped, count};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        auto* c = const_cast<PrewarmCtx*>(static_cast<const PrewarmCtx*>(ctx));
        if (i < static_cast<uint32_t>(c->count)) {
          formatFileName(c->self->files[c->first + i], c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          return c->self->rowNameBuf;
        }
        return c->self->basepath.c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(count) + 1);
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    return removeBookFile(fullPath);
  }
  file.close();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        if (!removeBookFile(entryPath)) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

void FileBrowserActivity::activateIndex(const int index) {
  (void)index;  // base already synced nav.selected to the tapped row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  activateSelected();
}

void FileBrowserActivity::onRowLongPress(const int index) {
  (void)index;  // base already synced nav.selected to the pressed row
  app.clearTapFlash();
  if (mode == Mode::Books) {
    showEntryActions();
  } else {
    activateSelected();
  }
}

#if FREEINK_CAP_TOUCH
void FileBrowserActivity::swipeDelete(const int index) {
  nav.selected = index;
  deleteSelected();
}
#endif

void FileBrowserActivity::activateSelected() {
  if (files.empty()) return;
  // A touch activation can carry a row index captured before a delete/reload
  // shrank the list; the next render re-registers the rows.
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string& entry = files[nav.selected];
  bool isDirectory = (entry.back() == '/');

  if (mode == Mode::PickFolder && entry == HERE_ENTRY) {
    ActivityResult res{FilePathResult{basepath}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  // buildScreen() runs on the render task and reads basepath plus `files`
  // through the row provider; mutate only under the render lock.
  RenderLock lock(*this);
  if (basepath.back() != '/') basepath += "/";

  if (isDirectory) {
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    nav.selected = 0;
    nav.top = 0;
    lock.unlock();
    requestUpdate();
  } else if (mode == Mode::Books && FsHelpers::checkFileExtension(entry, ".zip")) {
    lock.unlock();
    showEntryActions();  // a zip isn't a book: offer Extract here
  } else {
    const std::string fullPath = basepath + entry;
    lock.unlock();  // onSelectBook launches an activity; don't hold the lock across it
    onSelectBook(fullPath);
  }
}

void FileBrowserActivity::showEntryActions() {
  if (mode != Mode::Books || files.empty() || optionPopup.isActive() || nav.selected < 0 ||
      nav.selected >= listCount()) {
    return;
  }

  // Same order for files and folders: Open, Delete, Rename, Move, Copy, New folder (in the folder shown).
  // The 7th option: Extract here on a .zip (kos25k #7), Open as gallery on a folder (#8).
  static constexpr StrId ZIP_OPTIONS[] = {StrId::STR_OPEN,        StrId::STR_DELETE,  StrId::STR_RENAME,
                                          StrId::STR_MOVE,        StrId::STR_COPY_TO, StrId::STR_NEW_FOLDER,
                                          StrId::STR_EXTRACT_HERE};
  static constexpr StrId FOLDER_OPTIONS[] = {StrId::STR_OPEN,           StrId::STR_DELETE,  StrId::STR_RENAME,
                                             StrId::STR_MOVE,           StrId::STR_COPY_TO, StrId::STR_NEW_FOLDER,
                                             StrId::STR_OPEN_AS_GALLERY};
  // On a book the 7th is Info (kos25k #15).
  static constexpr StrId BOOK_OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE,  StrId::STR_RENAME,
                                           StrId::STR_MOVE, StrId::STR_COPY_TO, StrId::STR_NEW_FOLDER,
                                           StrId::STR_INFO};
  const std::string& selected = files[nav.selected];
  const bool zip = FsHelpers::checkFileExtension(selected, ".zip");
  const bool folder = selected.back() == '/';
  const bool book = !folder && (FsHelpers::hasReflowableBookExtension(selected) ||
                                FsHelpers::hasXtcExtension(selected) || FsHelpers::hasCbzExtension(selected));
  optionPopup.show(StrId::STR_FILENAME, folder ? FOLDER_OPTIONS : (book ? BOOK_OPTIONS : ZIP_OPTIONS),
                   zip || folder || book ? 7 : 6, 0, [this, folder, book](const int index) {
                     switch (index) {
                       case 0:
                         activateSelected();
                         break;
                       case 1:
                         deleteSelected();
                         break;
                       case 2:
                         startRename();
                         break;
                       case 3:
                         startTransfer(false);
                         break;
                       case 4:
                         startTransfer(true);
                         break;
                       case 5:
                         startNewFolder();
                         break;
                       case 6:
                         if (book) {
                           showBookInfo();
                         } else if (folder) {
                           const std::string entry = files[nav.selected];
                           activityManager.goToGallery((basepath.back() == '/' ? basepath : basepath + "/") +
                                                       entry.substr(0, entry.size() - 1));
                         } else {
                           extractSelected();
                         }
                         break;
                       default:
                         break;
                     }
                   });
  requestUpdate();
}

void FileBrowserActivity::showBookInfo() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;
  const std::string path = (basepath.back() == '/' ? basepath : basepath + "/") + files[nav.selected];
  infoText = bookinfo::text(path);
  infoAt = millis() + bookinfo::SHOW_AFTER_MS;  // opened from the file menu: let it close first
  requestUpdate();
}

void FileBrowserActivity::deleteSelected() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const std::string entry = files[nav.selected];
  const std::string fullPath = cleanBasePath + entry;

  auto handler = [this, fullPath](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("FileBrowser", "Delete cancelled by user");
      return;
    }

    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    if (!removeDirFile(fullPath)) {
      LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
      return;
    }

    LOG_DBG("FileBrowser", "Deleted successfully");
    {
      RenderLock lock(*this);
      loadFiles();
      if (files.empty()) {
        nav.selected = 0;
      } else if (nav.selected >= listCount()) {
        nav.selected = listCount() - 1;
      }
      nav.follow(listCount());
    }
    requestUpdate(true);
  };

  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM),
                                                              utf8ComposeNfc(entry));
  if (!confirmation) {
    LOG_ERR("FileBrowser", "OOM: delete confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), std::move(handler));
}

void FileBrowserActivity::startRename() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string oldEntry = files[nav.selected];

  std::string cleanBasePath = basepath;
  if (cleanBasePath.back() != '/') cleanBasePath += "/";
  const bool isDirectory = oldEntry.back() == '/';
  const std::string oldPath = cleanBasePath + (isDirectory ? oldEntry.substr(0, oldEntry.size() - 1) : oldEntry);
  const std::string extension = getFileExtension(oldEntry);
  const std::string initialStem =
      utf8ComposeNfc(oldEntry.substr(0, oldEntry.size() - (isDirectory ? 1 : extension.size())));
  const size_t maxStemLength = NAME_BUFFER_SIZE - extension.size() - 1;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), initialStem,
                                                           maxStemLength, InputType::Text);
  if (!keyboard) {
    LOG_ERR("FileBrowser", "OOM: rename keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, oldPath, oldEntry, extension](const ActivityResult& result) {
    if (result.isCancelled) return;
    renameSelectedFile(oldPath, oldEntry, std::get<KeyboardResult>(result.data).text, extension);
  });
}

void FileBrowserActivity::renameSelectedFile(const std::string& oldPath, const std::string& oldEntry,
                                             const std::string& newStem, const std::string& extension) {
  const std::string newEntry = newStem + extension;
  if (newStem.empty() || newEntry.size() >= NAME_BUFFER_SIZE || !FsHelpers::isSafePathComponent(newEntry)) {
    LOG_ERR("FileBrowser", "Invalid rename target: %s", newEntry.c_str());
    return;
  }
  if (newEntry == utf8ComposeNfc(oldEntry.back() == '/' ? oldEntry.substr(0, oldEntry.size() - 1) : oldEntry)) return;

  const std::string parentPath = FsHelpers::extractFolderPath(oldPath);
  const std::string newPath = (parentPath == "/" ? parentPath : parentPath + "/") + newEntry;
  if (Storage.exists(newPath.c_str())) {
    LOG_ERR("FileBrowser", "Rename target already exists: %s", newPath.c_str());
    return;
  }

  if (!moveFile(oldPath, newPath)) {
    showError(StrId::STR_RENAME_FAILED, newEntry);
    return;
  }
  reloadSelecting(oldEntry.back() == '/' ? newEntry + "/" : newEntry);
}

// Renames or moves a file or folder on the card, with the books' saved state (BookCacheUtils).
bool FileBrowserActivity::moveFile(const std::string& oldPath, const std::string& newPath) {
  return moveBookWithState(oldPath, newPath);
}

void FileBrowserActivity::reloadSelecting(const std::string& entry) {
  {
    RenderLock lock(*this);
    loadFiles();
    nav.selected = static_cast<int>(findEntry(entry));
    nav.follow(listCount());
  }
  requestUpdate(true);
}

void FileBrowserActivity::showError(const StrId heading, const std::string& name) {
  const std::string ok = tr(STR_OK_BUTTON);
  if (auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, I18N.get(heading),
                                                            utf8ComposeNfc(name), ok, ok)) {
    startActivityForResult(std::move(dialog), nullptr);
  }
}

void FileBrowserActivity::extractSelected() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;
  const std::string entry = files[nav.selected];
  const std::string zipPath = (basepath.back() == '/' ? basepath : basepath + "/") + entry;
  {
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_EXTRACTING));
  }
  std::string folder;
  zipextract::Stats stats;
  const auto r = zipextract::extractHere(zipPath, folder, stats);
  const std::string folderName = folder.substr(folder.find_last_of('/') + 1);
  if (r != zipextract::Result::Ok) {
    showError(r == zipextract::Result::Exists   ? StrId::STR_EXTRACT_EXISTS
              : r == zipextract::Result::BadZip ? StrId::STR_EXTRACT_BAD_ZIP
                                                : StrId::STR_EXTRACT_FAILED,
              r == zipextract::Result::Exists ? folderName : entry);
    reloadSelecting(entry);
    return;
  }
  LOG_INF("FileBrowser", "Extracted %d files (%d skipped) to %s", stats.files, stats.skipped, folder.c_str());
  reloadSelecting(folderName + "/");
}

void FileBrowserActivity::startNewFolder() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NEW_FOLDER), "",
                                                           NAME_BUFFER_SIZE - 2, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const std::string name = std::get<KeyboardResult>(result.data).text;
    const std::string path = (basepath.back() == '/' ? basepath : basepath + "/") + name;
    if (name.empty() || !FsHelpers::isSafePathComponent(name) || Storage.exists(path.c_str()) ||
        !Storage.mkdir(path.c_str())) {
      showError(StrId::STR_NEW_FOLDER_FAILED, name);
      return;
    }
    reloadSelecting(name + "/");
  });
}

// Move/Copy: pick the target folder in a folder-only browser, then move or copy there.
void FileBrowserActivity::startTransfer(const bool copy) {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;
  const std::string entry = files[nav.selected];
  const std::string src = (basepath.back() == '/' ? basepath : basepath + "/") +
                          (entry.back() == '/' ? entry.substr(0, entry.size() - 1) : entry);
  auto pickerActivity = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, basepath, Mode::PickFolder);
  if (!pickerActivity) return;
  startActivityForResult(std::move(pickerActivity), [this, src, copy](const ActivityResult& result) {
    if (result.isCancelled) return;
    transferInto(src, std::get<FilePathResult>(result.data).path, copy);
  });
}

void FileBrowserActivity::transferInto(const std::string& srcPath, const std::string& destDir, const bool copy) {
  const std::string name = srcPath.substr(srcPath.find_last_of('/') + 1);
  const std::string dest = (destDir.back() == '/' ? destDir : destDir + "/") + name;
  const StrId failed = copy ? StrId::STR_COPY_FAILED : StrId::STR_MOVE_FAILED;
  if (dest == srcPath && !copy) return;  // moved onto itself: nothing to do
  // A folder can't go inside itself, and nothing overwrites an existing name.
  if (dest == srcPath || dest.rfind(srcPath + "/", 0) == 0 || Storage.exists(dest.c_str())) {
    showError(failed, name);
    return;
  }
  bool ok;
  if (copy) {
    {
      RenderLock lock(*this);
      GUI.drawPopup(renderer, tr(STR_COPYING));
    }
    ok = fileops::copyTree(srcPath, dest);
    if (!ok) removeDirFile(dest);  // nothing half-copied stays behind
  } else {
    ok = moveFile(srcPath, dest);
  }
  auto probe = Storage.open(ok && !copy ? dest.c_str() : srcPath.c_str());
  const bool isDirectory = probe && probe.isDirectory();
  if (probe) probe.close();
  if (!ok) {
    LOG_ERR("FileBrowser", "%s failed: %s -> %s", copy ? "Copy" : "Move", srcPath.c_str(), dest.c_str());
    showError(failed, name);
  }
  reloadSelecting(isDirectory ? name + "/" : name);
}

bool FileBrowserActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (infoAt != 0 && millis() >= infoAt && !optionPopup.isActive()) {
    infoAt = 0;
    static const char* const OK[] = {"OK"};
    optionPopup.showMessage(tr(STR_BOOK_INFO), infoText.c_str(), OK, 1, 0, [this](int) { requestUpdate(); });
    requestUpdate();
    return true;
  }

  // New folder from blank space (kos25k #6): a long press anywhere below the header that isn't on a
  // row, in any folder; in an empty folder also a tap, or Confirm.
  if (mode == Mode::Books && !optionPopup.isActive()) {
    int tx = 0;
    int ty = 0;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight;
    bool blankPress = false;
    if (mappedInput.peekScreenLongPress(tx, ty) && ty > contentTop) {
      fui::Interaction hit;
      RenderLock lock(*this);
      blankPress = files.empty() || !routingReady() || !app.hitPublished(tx, ty, ACTION_ROW, hit);
    }
    if (blankPress) mappedInput.takeScreenLongPress();
    const bool emptyTap = files.empty() && ((mappedInput.wasScreenTapped(tx, ty) && ty > contentTop) ||
                                            mappedInput.wasReleased(MappedInputManager::Button::Confirm));
    if (blankPress || emptyTap) {
      static constexpr StrId BLANK_OPTIONS[] = {StrId::STR_NEW_FOLDER};
      optionPopup.show(StrId::STR_BROWSE_FILES, BLANK_OPTIONS, 1, 0, [this](const int index) {
        if (index == 0) startNewFolder();
        requestUpdate();
      });
      requestUpdate();
      return true;
    }
  }

  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/") {
    {
      // buildScreen() runs on the render task and reads basepath plus `files`
      // through the row provider; mutate only under the render lock.
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    requestUpdate();
    return true;
  }

  return false;
}

bool FileBrowserActivity::handleButtons() {
  if (mode == Mode::Books && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, GO_HOME_MS)) {
    app.clearTapFlash();
    showEntryActions();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        {
          // buildScreen() runs on the render task and reads basepath plus `files`
          // through the row provider; mutate only under the render lock.
          RenderLock lock(*this);
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName));
          nav.top = 0;
          nav.follow(listCount());
        }

        requestUpdate();
      } else if (picker()) {
        // Picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
    return true;
  }

  return false;
}

void FileBrowserActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Full path band at the bottom: separator on top, left-truncated so the
  // deepest directory stays visible.
  {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (files.empty()) {
    screen.centeredText(mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND),
                        screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.rowProvider = &FileBrowserActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(files.size());
  props.action = ACTION_ROW;
  // Tap opens/navigates; long-press shows entry actions (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  // air between the extension and the row edge
  // Names use up to two small-font lines; shared list layout sizes each row.
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;

  // The trailing value here is just the short extension: skip the balanced
  // 60%-band wrap cap and let both name lines run the full width before it.
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);
  // Prewarm the window at the final viewport (syncListViewport just applied
  // follow/clamping to nav.top) before the list resolves rows through the
  // provider.
  prewarmRowGlyphs(nav.top);
#if FREEINK_CAP_TOUCH
  if (mode == Mode::Books) configureSwipeDelete(screen, props);
#endif
  screen.list(props);
}

void FileBrowserActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());
}

void FileBrowserActivity::drawFooter() {
  const char* backLabel = (basepath == "/") ? (picker() ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && nav.selected >= 0 &&
                                     nav.selected < listCount() && files[nav.selected].back() != '/';
  const char* confirmLabel = files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  const auto entry = std::find(files.begin(), files.end(), name);
  return entry != files.end() ? static_cast<size_t>(entry - files.begin()) : 0;
}
