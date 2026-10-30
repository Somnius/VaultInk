#include "GalleryActivity.h"

#include <Epub/blocks/ImageBlock.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/BmpViewerActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "components/icons/settings2.h"
#include "fontIds.h"
#include "util/FileOps.h"
#include "util/TaskWatchdog.h"
#include "util/VaultFiles.h"

namespace {
constexpr int HEADER = 56;
constexpr int FOOTER = 40;
constexpr int MARGIN = 10;
constexpr int GEAR = 32;
constexpr int GAP = 8;
constexpr int LABEL = 22;
constexpr const char* THUMBS = "/.crosspoint/thumbs";
// The folders "Chosen folders" shows (kos25k #8), one path per line.
constexpr const char* CHOSEN = "/.vaultink/gallery-folders.txt";
constexpr int FILTER_CHOSEN = 3;

std::vector<std::string> readChosen() {
  std::vector<std::string> folders;
  std::string text;
  if (!Storage.readFileToString("GAL", CHOSEN, 16 * 1024, text)) return folders;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) folders.push_back(line);
    start = end + 1;
  }
  return folders;
}

void writeChosen(const std::vector<std::string>& folders) {
  std::string text;
  for (const auto& f : folders) text += f + "\n";
  Storage.ensureDirectoryExists("/.vaultink");
  Storage.writeFile(CHOSEN, String(text.c_str()));
}
}  // namespace

GalleryActivity::GalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string folder)
    : Activity("Gallery", renderer, mappedInput), root(folder.empty() ? "/" : std::move(folder)) {}

void GalleryActivity::onEnter() {
  Activity::onEnter();
  scan();
  page = std::min(page, std::max(0, pageCount() - 1));
  requestUpdate(true);
}

// Every picture under the root, depth first; hidden folders (.crosspoint, .obsidian, .trash...)
// are skipped.
void GalleryActivity::scan() {
  pictures.clear();
  truncated = false;
  // Settings from the gear (kos25k #8): all, PNG only, or only pictures inside a vault.
  const uint8_t filter = SETTINGS.galleryFilter;
  std::vector<std::string> vaultRoots;
  if (filter == 2) {
    const auto vaults = vaultfiles::findVaults();
    std::transform(vaults.begin(), vaults.end(), std::back_inserter(vaultRoots),
                   [](const vaultfiles::Vault& vault) { return vault.path + "/"; });
  }
  const auto wanted = [&](const std::string& path) {
    if (filter == 1) return FsHelpers::hasPngExtension(path);
    if (filter == 2) {
      return std::any_of(vaultRoots.begin(), vaultRoots.end(),
                         [&path](const std::string& root) { return path.rfind(root, 0) == 0; });
    }
    return true;
  };
  // Chosen folders replace the whole card; a gallery opened on one folder keeps that folder.
  std::vector<std::string> folders{root};
  if (filter == FILTER_CHOSEN && root == "/") folders = readChosen();
  auto name = makeUniqueNoThrow<char[]>(500);
  if (!name) return;
  while (!folders.empty() && !truncated) {
    const std::string dir = folders.back();
    folders.pop_back();
    auto handle = Storage.open(dir.c_str());
    if (!handle || !handle.isDirectory()) continue;
    for (auto entry = handle.openNextFile(); entry; entry = handle.openNextFile()) {
      entry.getName(name.get(), 500);
      const bool isDir = entry.isDirectory();
      entry.close();
      if (name[0] == '.' || strcmp(name.get(), "System Volume Information") == 0) continue;
      const std::string path = (dir == "/" ? "/" : dir + "/") + name.get();
      if (isDir) {
        folders.push_back(path);
      } else if (FsHelpers::hasImageExtension(path) && wanted(path)) {
        if (pictures.size() >= MAX_PICTURES) {
          truncated = true;
          break;
        }
        pictures.push_back(path);
      }
      resetTaskWatchdogIfSubscribed();
    }
    handle.close();
  }
  std::sort(pictures.begin(), pictures.end());
}

int GalleryActivity::pageCount() const { return static_cast<int>((pictures.size() + PER_PAGE - 1) / PER_PAGE); }

void GalleryActivity::cellRect(const int slot, int& x, int& y, int& w, int& h) const {
  const int width = renderer.getScreenWidth() - 2 * MARGIN;
  const int height = renderer.getScreenHeight() - HEADER - FOOTER;
  w = (width - (COLS - 1) * GAP) / COLS;
  h = (height - (ROWS - 1) * GAP) / ROWS;
  x = MARGIN + (slot % COLS) * (w + GAP);
  y = HEADER + (slot / COLS) * (h + GAP);
}

bool GalleryActivity::cellAt(const int px, const int py, int& index) const {
  for (int slot = 0; slot < PER_PAGE; ++slot) {
    const int i = page * PER_PAGE + slot;
    if (i >= static_cast<int>(pictures.size())) return false;
    int x, y, w, h;
    cellRect(slot, x, y, w, h);
    if (px >= x && px < x + w && py >= y && py < y + h) {
      index = i;
      return true;
    }
  }
  return false;
}

// The picture fitted into the box, from its pixel cache when there is one; the first draw decodes
// it and writes that cache.
void GalleryActivity::drawThumb(const std::string& path, const int x, const int y, const int w, const int h) {
  const auto decode = [&](ImageToFramebufferDecoder& decoder) {
    ImageDimensions dims;
    if (!decoder.getDimensions(path, dims) || dims.width <= 0 || dims.height <= 0) return false;
    const float fit = std::min(static_cast<float>(w) / dims.width, static_cast<float>(h) / dims.height);
    RenderConfig config{0, 0, std::max(1, std::min(w, static_cast<int>(dims.width * fit))),
                        std::max(1, std::min(h, static_cast<int>(dims.height * fit)))};
    config.x = x + (w - config.maxWidth) / 2;
    config.y = y + (h - config.maxHeight) / 2;
    config.useExactDimensions = true;
    config.useGrayscale = false;  // the grid is one black-and-white pass
    // Keyed by path, size and box: a replaced picture (or a hash clash) gets its own thumbnail.
    HalFile probe;
    size_t bytes = 0;
    if (Storage.openFileForRead("GAL", path, probe)) {
      bytes = probe.fileSize();
      probe.close();
    }
    config.cachePath = std::string(THUMBS) + "/" + std::to_string(std::hash<std::string>{}(path)) + "_" +
                       std::to_string(bytes) + "_" + std::to_string(config.maxWidth) + "x" +
                       std::to_string(config.maxHeight) + ".pxc";
    if (renderImagePixelCache(renderer, config.cachePath, config.x, config.y, config.maxWidth, config.maxHeight)) {
      return true;
    }
    Storage.ensureDirectoryExists(THUMBS);
    return decoder.decodeToFramebuffer(path, renderer, config);
  };
  bool drawn = false;
  if (FsHelpers::hasJpgExtension(path)) {
    JpegToFramebufferConverter jpeg;
    drawn = decode(jpeg);
  } else if (FsHelpers::checkFileExtension(path, ".png")) {
    PngToFramebufferConverter png;
    drawn = decode(png);
  }
  if (!drawn) {
    // A format the grid doesn't decode (or a broken file): its name in a frame.
    renderer.drawRect(x, y, w, h);
    const std::string name = path.substr(path.find_last_of('/') + 1);
    renderer.drawText(SMALL_FONT_ID, x + 6, y + h / 2 - 10,
                      renderer.truncatedText(SMALL_FONT_ID, name.c_str(), w - 12).c_str());
  }
  resetTaskWatchdogIfSubscribed();
}

void GalleryActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  // Header: back arrow area, the folder and the page.
  const std::string folderName = root == "/" ? tr(STR_GALLERY) : root.substr(root.find_last_of('/') + 1);
  renderer.drawText(UI_12_FONT_ID, MARGIN, 14, ("« " + folderName).c_str(), true, EpdFontFamily::BOLD);
  // The gear (kos25k #8) at the right, the page left of it.
  renderer.drawIcon(Settings2Icon, width - MARGIN - GEAR, 8, GEAR);
  // Refresh (kos25k #8) left of the gear: looks for new and removed pictures again.
  renderer.drawIcon(icon_refresh_cw_32_bits, refreshX(), 8, GEAR);
  if (!pictures.empty()) {
    char pages[32];
    snprintf(pages, sizeof(pages), "%d/%d", page + 1, pageCount());
    renderer.drawText(SMALL_FONT_ID, refreshX() - 12 - renderer.getTextWidth(SMALL_FONT_ID, pages), 20, pages);
  }
  renderer.drawLine(0, HEADER - 6, width, HEADER - 6);
  if (pictures.empty()) {
    const bool noneChosen = SETTINGS.galleryFilter == FILTER_CHOSEN && root == "/" && readChosen().empty();
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2,
                              noneChosen ? tr(STR_GALLERY_NO_FOLDERS) : tr(STR_GALLERY_EMPTY));
  }
  for (int slot = 0; slot < PER_PAGE; ++slot) {
    const int i = page * PER_PAGE + slot;
    if (i >= static_cast<int>(pictures.size())) break;
    int x, y, w, h;
    cellRect(slot, x, y, w, h);
    drawThumb(pictures[i], x, y, w, h - LABEL);
    const std::string name = pictures[i].substr(pictures[i].find_last_of('/') + 1);
    renderer.drawText(SMALL_FONT_ID, x, y + h - LABEL + 2,
                      renderer.truncatedText(SMALL_FONT_ID, name.c_str(), w).c_str());
  }
  if (truncated) {
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - FOOTER + 8, tr(STR_GALLERY_TRUNCATED));
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void GalleryActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }
  const auto turn = [this](const int delta) {
    const int next = page + delta;
    if (next < 0 || next >= pageCount()) return;
    page = next;
    requestUpdate();
  };
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) return turn(1);
  if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) return turn(-1);
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    return turn(1);
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    return turn(-1);
  }
  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    if (y < HEADER && x < renderer.getScreenWidth() / 2) {  // the « title: back home
      activityManager.goHome();
      return;
    }
    if (y < HEADER && x > renderer.getScreenWidth() - MARGIN - GEAR - 12) {
      filterMenu();
      return;
    }
    if (y < HEADER && x > refreshX() - 12 && x < refreshX() + GEAR + 6) {
      {
        RenderLock lock(*this);
        scan();
        page = std::min(page, std::max(0, pageCount() - 1));
      }
      requestUpdate(true);
      return;
    }
    int index = 0;
    if (cellAt(x, y, index)) pictureActions(index);
  }
}

int GalleryActivity::refreshX() const { return renderer.getScreenWidth() - MARGIN - GEAR - 16 - GEAR; }

void GalleryActivity::rescanFromTop() {
  {
    RenderLock lock(*this);
    scan();
    page = 0;
  }
  requestUpdate(true);
}

void GalleryActivity::filterMenu() {
  // The four filters, then the chosen-folder list (kos25k #8).
  static constexpr StrId OPTIONS[] = {StrId::STR_GALLERY_SHOW_ALL,   StrId::STR_GALLERY_SHOW_PNG,
                                      StrId::STR_GALLERY_SHOW_NOTES, StrId::STR_GALLERY_SHOW_CHOSEN,
                                      StrId::STR_GALLERY_ADD_FOLDER, StrId::STR_GALLERY_CLEAR_FOLDERS};
  optionPopup.show(StrId::STR_GALLERY, OPTIONS, 6, SETTINGS.galleryFilter, [this](const int choice) {
    if (choice >= 0 && choice <= FILTER_CHOSEN) {
      if (choice != SETTINGS.galleryFilter) {
        SETTINGS.galleryFilter = static_cast<uint8_t>(choice);
        SETTINGS.saveToFile();
        rescanFromTop();
        return;
      }
    } else if (choice == 4) {
      addChosenFolder();
      return;
    } else if (choice == 5) {
      writeChosen({});
      rescanFromTop();
      return;
    }
    requestUpdate(true);
  });
  requestUpdate();
}

// Picks a folder in the file browser, adds it to the chosen list and shows Chosen folders.
void GalleryActivity::addChosenFolder() {
  auto picker =
      makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickFolder);
  if (!picker) return;
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      std::string folder = std::get<FilePathResult>(result.data).path;
      if (folder.size() > 1 && folder.back() == '/') folder.pop_back();
      auto folders = readChosen();
      if (std::find(folders.begin(), folders.end(), folder) == folders.end()) folders.push_back(folder);
      writeChosen(folders);
      SETTINGS.galleryFilter = FILTER_CHOSEN;
      SETTINGS.saveToFile();
    }
    rescanFromTop();
  });
}

void GalleryActivity::pictureActions(const int index) {
  if (index < 0 || index >= static_cast<int>(pictures.size())) return;
  // Set as sleep cover is here too (kos25k #21): on touch screens the viewer shows no button hint for it.
  static constexpr StrId OPTIONS[] = {StrId::STR_GALLERY_VIEW, StrId::STR_DELETE, StrId::STR_MOVE, StrId::STR_COPY_TO,
                                      StrId::STR_SET_AS_SLEEP_PICTURE};
  const std::string path = pictures[index];
  optionPopup.show(StrId::STR_GALLERY, OPTIONS, 5, 0, [this, path](const int choice) {
    if (choice == 0 || choice == 4) {
      if (auto viewer = makeUniqueNoThrow<BmpViewerActivity>(renderer, mappedInput, path, true, choice == 4)) {
        startActivityForResult(std::move(viewer), [this](const ActivityResult&) { requestUpdate(true); });
      }
    } else if (choice == 1) {
      auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_ITEM_CONFIRM),
                                                            path.substr(path.find_last_of('/') + 1));
      if (!dialog) return;
      startActivityForResult(std::move(dialog), [this, path](const ActivityResult& result) {
        if (!result.isCancelled && !Storage.remove(path.c_str())) LOG_ERR("GAL", "Could not delete %s", path.c_str());
        onEnter();
      });
    } else if (choice == 2 || choice == 3) {
      transfer(path, choice == 3);
    } else {
      requestUpdate();
    }
  });
  requestUpdate();
}

// Move or copy one picture into a folder picked in the file browser; nothing is overwritten.
void GalleryActivity::transfer(const std::string& path, const bool copy) {
  auto picker = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, FsHelpers::extractFolderPath(path),
                                                       FileBrowserActivity::Mode::PickFolder);
  if (!picker) return;
  startActivityForResult(std::move(picker), [this, path, copy](const ActivityResult& result) {
    if (!result.isCancelled) {
      std::string dest = std::get<FilePathResult>(result.data).path;
      if (dest.back() != '/') dest += "/";
      dest += path.substr(path.find_last_of('/') + 1);
      bool ok = false;
      if (dest != path && !Storage.exists(dest.c_str())) {
        ok = copy ? fileops::copyFile(path, dest) : Storage.rename(path.c_str(), dest.c_str());
      }
      if (!ok) {
        const std::string okLabel = tr(STR_OK_BUTTON);
        if (auto dialog = makeUniqueNoThrow<ConfirmationActivity>(
                renderer, mappedInput, I18N.get(copy ? StrId::STR_COPY_FAILED : StrId::STR_MOVE_FAILED),
                path.substr(path.find_last_of('/') + 1), okLabel, okLabel)) {
          startActivityForResult(std::move(dialog), [this](const ActivityResult&) { onEnter(); });
          return;
        }
      }
    }
    onEnter();
  });
}
