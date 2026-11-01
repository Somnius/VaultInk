#include "FileSearchActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/BookInfo.h"
#include "util/FileOps.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;
constexpr int16_t kSidePadding = 6;
constexpr size_t MAX_QUERY = 80;
}  // namespace

FileSearchActivity::StorageSource::StorageSource() : nameBuffer(makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE)) {}

bool FileSearchActivity::StorageSource::list(const std::string& path, std::vector<filesearch::Entry>& out) {
  if (!nameBuffer) return false;
  auto dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) return false;
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(nameBuffer.get(), NAME_BUFFER_SIZE);
    out.push_back({std::string(nameBuffer.get()), entry.isDirectory()});
    entry.close();
  }
  dir.close();
  return true;
}

FileSearchActivity::FileSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string rootPath)
    : UiListActivity("FileSearch", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      rootPath(rootPath.empty() ? "/" : std::move(rootPath)) {}

const char* FileSearchActivity::headerTitle() const { return tr(STR_FILE_SEARCH); }

void FileSearchActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
  requestUpdate();
}

void FileSearchActivity::onExit() {
  Activity::onExit();
  walker.reset();
  shown.clear();
  rows.clear();
}

// Back, whichever way it arrives: a press, a release or a hold. Looked at on every step; the flags are
// per input update, so a tap that ends before the next step is still seen.
bool FileSearchActivity::backRequested() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) return true;
  if (mappedInput.wasPressed(Button::Back) || mappedInput.isPressed(Button::Back)) {
    swallowBackRelease = true;  // the release that follows must not also leave the screen
    return true;
  }
  return false;
}

void FileSearchActivity::loop() {
  if (askPending) {
    askPending = false;
    askForQuery();
    return;
  }
  if (searching) {
    if (backRequested()) {
      finishSearch();
      return;
    }
    UiListActivity::loop();  // scroll and pick while it works
    if (searching) stepSearch();
    return;
  }
  if (swallowBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      swallowBackRelease = false;
      return;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) swallowBackRelease = false;
  }
  UiListActivity::loop();
}

void FileSearchActivity::askForQuery() {
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FILE_SEARCH_PROMPT), query, MAX_QUERY);
  if (!keyboard) {
    LOG_ERR("FileSearch", "OOM: search keyboard");
    onBackButton();
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      if (query.empty()) onBackButton();  // nothing searched yet: back to Browse Files
      return;
    }
    startSearch(std::get<KeyboardResult>(result.data).text);
  });
}

void FileSearchActivity::startSearch(const std::string& text) {
  query = text;
  auto next = makeUniqueNoThrow<filesearch::Walker>(source, rootPath, query, SETTINGS.showHiddenFiles != 0);
  if (!next || !source.ok()) {
    LOG_ERR("FileSearch", "OOM: walker");
    return;
  }
  {
    RenderLock lock;
    walker = std::move(next);
    searching = !walker->done();
    shown.clear();
    progressText.clear();
    nav.reset(0);
    rebuildRows();
  }
  lastRedrawMs = millis();
  requestUpdate();
}

// One slice of work, about STEP_MS: loop() reads the buttons before the next one.
void FileSearchActivity::stepSearch() {
  const unsigned long start = millis();
  bool more = true;
  while (more && millis() - start < STEP_MS) more = walker->step();
  if (!more) {
    finishSearch();
    return;
  }
  // One e-ink refresh now and then, not one per folder.
  if (millis() - lastRedrawMs >= REDRAW_MS) {
    lastRedrawMs = millis();
    char buf[64];
    snprintf(buf, sizeof(buf), tr(STR_SEARCHING_FILES), static_cast<int>(walker->foldersRead()));
    {
      RenderLock lock;
      progressText = buf;
      refreshShown();
    }
    requestUpdate();
  }
}

void FileSearchActivity::finishSearch() {
  searching = false;
  if (!walker) return;
  LOG_DBG("FileSearch", "Search \"%s\": %u hits in %u folders%s", query.c_str(),
          static_cast<unsigned>(walker->results().size()), static_cast<unsigned>(walker->foldersRead()),
          walker->done() ? "" : " (stopped)");
  char buf[64];
  snprintf(buf, sizeof(buf), walker->truncated() ? tr(STR_FILE_SEARCH_MORE) : tr(STR_FILE_SEARCH_DONE),
           static_cast<int>(walker->results().size()), static_cast<int>(walker->foldersRead()));
  {
    RenderLock lock;
    progressText = walker->results().empty() ? tr(STR_NO_RESULTS) : buf;
    refreshShown();
    if (nav.selected == 0 && !shown.empty()) nav.selected = 1;
  }
  requestUpdate();
}

// The render task reads `shown` and `rows`: call under the render lock.
void FileSearchActivity::refreshShown() {
  shown.clear();
  if (walker) {
    const auto& hits = walker->results();
    shown.reserve(hits.size());
    std::transform(hits.begin(), hits.end(), std::back_inserter(shown), [](const filesearch::Hit& hit) {
      return Shown{utf8ComposeNfc(hit.name), utf8ComposeNfc(hit.folder), hit.isDirectory};
    });
  }
  rebuildRows();
}

void FileSearchActivity::rebuildRows() {
  queryLabel = std::string(tr(STR_FILE_SEARCH)) + ": " + query;
  rows.assign(shown.size() + 1, fui::ListItem{});
  rows[0].label = queryLabel.c_str();
  if (searching) {
    rows[0].subtitle = progressText.empty() ? tr(STR_SEARCHING) : progressText.c_str();
  } else {
    rows[0].subtitle =
        query.empty() ? nullptr : (progressText.empty() ? tr(STR_SEARCH_AGAIN_HINT) : progressText.c_str());
  }
  rows[0].actionValue = 0;
  for (size_t i = 0; i < shown.size(); ++i) {
    rows[i + 1].label = shown[i].label.c_str();
    rows[i + 1].subtitle = shown[i].folder.c_str();
    rows[i + 1].icon = listIconFor(UITheme::getFileIcon(shown[i].isDirectory ? shown[i].label + "/" : shown[i].label));
    rows[i + 1].actionValue = static_cast<int16_t>(i + 1);
  }
}

void FileSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  // The folder line is clipped from the left, two dots first, so the folder nearest the file stays visible.
  {
    const fui::TextStyle style = screen.theme().smallText;
    const int available = screen.frame().screen().width - 2 * kSidePadding - 22 - 40;  // icon and air
    fitted.resize(shown.size());
    for (size_t i = 0; i < shown.size() && i + 1 < rows.size(); ++i) {
      const std::string& folder = shown[i].folder;
      if (screen.target().measureText(style.font, folder.c_str(), style).width <= available) {
        rows[i + 1].subtitle = folder.c_str();
        continue;
      }
      size_t from = 0;
      do {
        ++from;
        while (from < folder.size() && (static_cast<unsigned char>(folder[from]) & 0xC0) == 0x80) ++from;
        fitted[i] = ".." + folder.substr(from);
      } while (from < folder.size() &&
               screen.target().measureText(style.font, fitted[i].c_str(), style).width > available);
      rows[i + 1].subtitle = fitted[i].c_str();
    }
  }
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  // Tap shows the hit's folder; long-press offers the actions (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.subtitleText = screen.theme().smallText;
  props.sidePadding = kSidePadding;
  syncListViewport(screen, props);
  screen.list(props);
}

void FileSearchActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == 0) {
    if (searching) finishSearch();
    askForQuery();
    return;
  }
  showInFolder(index - 1);
}

void FileSearchActivity::onRowLongPress(const int index) {
  app.clearTapFlash();
  if (index >= 1) showHitActions(index - 1);
}

bool FileSearchActivity::handleCustomInput() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (infoAt != 0 && millis() >= infoAt && !optionPopup.isActive()) {
    infoAt = 0;
    static const char* const OK[] = {"OK"};
    optionPopup.showMessage(tr(STR_BOOK_INFO), infoText.c_str(), OK, 1, 0, [this](int) { requestUpdate(); });
    requestUpdate();
    return true;
  }
  return false;
}

bool FileSearchActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    app.clearTapFlash();
    if (nav.selected >= 1) showHitActions(nav.selected - 1);
    return true;
  }
  return UiListActivity::handleButtons();
}

void FileSearchActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

std::string FileSearchActivity::hitPath(const filesearch::Hit& hit) const {
  return (hit.folder == "/" ? hit.folder : hit.folder + "/") + hit.name;
}

// The same menu as Browse Files for this file, cut to what makes sense from a result list (kos25k #13).
void FileSearchActivity::showHitActions(const int hit) {
  if (optionPopup.isActive() || !walker || hit < 0 || hit >= static_cast<int>(walker->results().size())) return;
  if (searching) finishSearch();  // the list must hold still while a row is acted on
  static constexpr StrId OPTIONS[] = {StrId::STR_INFO, StrId::STR_COPY_TO, StrId::STR_MOVE, StrId::STR_SHOW_IN_FOLDER};
  optionPopup.show(StrId::STR_FILENAME, OPTIONS, 4, 0, [this, hit](const int index) {
    switch (index) {
      case 0:
        showInfo(hit);
        break;
      case 1:
        startTransfer(hit, true);
        break;
      case 2:
        startTransfer(hit, false);
        break;
      case 3:
        showInFolder(hit);
        break;
      default:
        break;
    }
  });
  requestUpdate();
}

void FileSearchActivity::showInfo(const int hit) {
  if (!walker || hit < 0 || hit >= static_cast<int>(walker->results().size())) return;
  const filesearch::Hit& h = walker->results()[hit];
  const std::string path = hitPath(h);
  const bool book = !h.isDirectory && (FsHelpers::hasReflowableBookExtension(h.name) ||
                                       FsHelpers::hasXtcExtension(h.name) || FsHelpers::hasCbzExtension(h.name));
  infoText = book ? bookinfo::text(path) : bookinfo::fileText(path);
  infoAt = millis() + bookinfo::SHOW_AFTER_MS;  // opened from the file menu: let it close first
  requestUpdate();
}

// Move/Copy: the Browse Files folder picker, then the shared transfer (no overwrite, a folder never into
// itself, nothing half-copied left). A moved hit leaves the list; a copied one stays where it was.
void FileSearchActivity::startTransfer(const int hit, const bool copy) {
  if (!walker || hit < 0 || hit >= static_cast<int>(walker->results().size())) return;
  const filesearch::Hit& h = walker->results()[hit];
  const std::string src = hitPath(h);
  auto picker =
      makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, h.folder, FileBrowserActivity::Mode::PickFolder);
  if (!picker) return;
  startActivityForResult(std::move(picker), [this, src, hit, copy](const ActivityResult& result) {
    if (result.isCancelled) return;
    const std::string destDir = std::get<FilePathResult>(result.data).path;
    const std::string name = src.substr(src.find_last_of('/') + 1);
    if (copy) {
      RenderLock lock(*this);
      GUI.drawPopup(renderer, tr(STR_COPYING));
    }
    bool isDirectory = false;
    const auto r = fileops::transferInto(src, destDir, copy, isDirectory);
    if (r != fileops::Transfer::Ok) {
      showError(copy ? StrId::STR_COPY_FAILED : StrId::STR_MOVE_FAILED, name);
      return;
    }
    if (!copy && walker && !Storage.exists(src.c_str())) {
      char buf[64];
      snprintf(buf, sizeof(buf), tr(STR_FILE_SEARCH_DONE), static_cast<int>(walker->results().size() - 1),
               static_cast<int>(walker->foldersRead()));
      RenderLock lock;
      walker->erase(static_cast<size_t>(hit));
      progressText = buf;  // the count without the moved row
      refreshShown();
      if (nav.selected >= listCount()) nav.selected = listCount() - 1;
      nav.follow(listCount());
    }
    requestUpdate(true);
  });
}

void FileSearchActivity::showInFolder(const int hit) {
  if (!walker || hit < 0 || hit >= static_cast<int>(walker->results().size())) return;
  const filesearch::Hit& h = walker->results()[hit];
  if (searching) finishSearch();
  ActivityResult res{FilePathResult{hitPath(h) + (h.isDirectory ? "/" : "")}};
  res.isCancelled = false;
  setResult(std::move(res));
  finish();
}

void FileSearchActivity::showError(const StrId heading, const std::string& name) {
  const std::string ok = tr(STR_OK_BUTTON);
  if (auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, I18N.get(heading),
                                                            utf8ComposeNfc(name), ok, ok)) {
    startActivityForResult(std::move(dialog), nullptr);
  }
}

void FileSearchActivity::onBackButton() {
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
