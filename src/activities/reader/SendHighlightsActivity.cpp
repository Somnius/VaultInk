// clang-format off
// HalStorage.h brings in SdFat, whose macros have to be seen before the network headers (the same
// order OtaUpdater.cpp and TlsPolicyDevice.cpp pin).
#include <HalStorage.h>
#include "SendHighlightsActivity.h"
// clang-format on

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "network/HighlightExportClient.h"

namespace {

constexpr char MARKER_DIR[] = "/.vaultink/exports";
// The largest marker file there can be, with room to spare.
constexpr size_t MAX_MARKER_FILE = 8 + highlightexport::SentMarkers::MAX_ENTRIES * 8 + 4;
// Memos takes one request per highlight: the marks go to the card every few, so a lost connection
// or a flat battery costs at most this many duplicates on the next run.
constexpr size_t MEMOS_SAVE_EVERY = 10;

std::string formatCount(const StrId id, const int count) {
  char buf[96];
  snprintf(buf, sizeof(buf), I18N.get(id), count);
  return buf;
}

std::string errorText(const highlightexport::Outcome& outcome) {
  using highlightexport::Result;
  switch (outcome.result) {
    case Result::NotConnected:
      return tr(STR_SEND_HL_NOT_CONNECTED);
    case Result::BadToken:
      return tr(STR_SEND_HL_BAD_TOKEN);
    case Result::RateLimited:
      return outcome.retryAfterSeconds > 0
                 ? formatCount(StrId::STR_SEND_HL_RETRY_IN, static_cast<int>(outcome.retryAfterSeconds))
                 : std::string(tr(STR_SEND_HL_RATE_LIMITED));
    case Result::NotFound:
      return tr(STR_SEND_HL_NOT_FOUND);
    case Result::Rejected:
      return tr(STR_SEND_HL_REJECTED);
    case Result::ServerError:
    default:
      return tr(STR_SEND_HL_SERVER_ERROR);
  }
}

}  // namespace

std::string SendHighlightsActivity::markerPath() const {
  return std::string(MARKER_DIR) + "/" + highlightexport::sentFileName(bookPath);
}

bool SendHighlightsActivity::loadMarkers() {
  markers.clear();
  markersDirty = false;
  const std::string path = markerPath();
  if (!Storage.exists(path.c_str())) return true;
  std::string bytes;
  if (!Storage.readFileToString("EXP", path, MAX_MARKER_FILE, bytes) ||
      !markers.parse(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size())) {
    // Nothing is lost but the memory of what went out: the next send starts from none.
    LOG_ERR("EXP", "Sent-marker file unreadable, starting empty");
    return false;
  }
  return true;
}

bool SendHighlightsActivity::saveMarkers() {
  if (!markersDirty) return true;
  Storage.mkdir(MARKER_DIR);  // with its parents
  const std::string path = markerPath();
  const std::string tmp = path + ".tmp";
  const std::string bytes = markers.serialize();
  HalFile file;
  if (!Storage.openFileForWrite("EXP", tmp, file)) return false;
  const bool written = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  file.close();
  if (!written || !Storage.replaceFile(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    LOG_ERR("EXP", "Sent-marker file could not be written");
    return false;
  }
  markersDirty = false;
  return true;
}

uint64_t SendHighlightsActivity::keyFor(const size_t index) const {
  const Clipping* clipping = CLIPPINGS.clippingAt(index);
  return highlightexport::sentKey(clipping ? clipping->id : "", destination);
}

void SendHighlightsActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // The reader unloaded the clippings on its way out. prepareSync() gives every clipping its id
  // (they get one the first time they are needed), which is what the sent marks are made from.
  if (!CLIPPINGS.loadForBook(bookPath, title, author, "epub") || !CLIPPINGS.prepareSync()) {
    showResult(tr(STR_SEND_HL_READ_FAILED));
    return;
  }
  const size_t total = CLIPPINGS.clippingCount();
  if (total == 0) {
    showResult(tr(STR_NO_CLIPPINGS));
    return;
  }
  loadMarkers();
  size_t pending = 0;
  for (size_t i = 0; i < total; i++) {
    if (!markers.contains(keyFor(i))) pending++;
  }

  // What to send, before any radio: the new ones, or everything again.
  std::vector<std::string> options;
  int newIndex = -1;
  int allIndex = -1;
  if (pending > 0) {
    newIndex = static_cast<int>(options.size());
    options.push_back(formatCount(StrId::STR_SEND_HL_COUNT, static_cast<int>(pending)));
  }
  if (pending < total) {
    allIndex = static_cast<int>(options.size());
    options.push_back(formatCount(StrId::STR_SEND_HL_ALL_AGAIN, static_cast<int>(total)));
  }
  options.emplace_back(tr(STR_CANCEL));
  const StrId titleId =
      destination == highlightexport::Destination::Readwise ? StrId::STR_SEND_TO_READWISE : StrId::STR_SEND_TO_MEMOS;
  popup.show(titleId, options, 0, [this, newIndex, allIndex](const int index) {
    if (index != newIndex && index != allIndex) {
      returnToReader();
      return;
    }
    sendAll = index == allIndex;
    startWifi();
  });
  requestUpdate();
}

void SendHighlightsActivity::startWifi() {
  {
    RenderLock lock(*this);
    state = State::Wifi;
  }
  // Past this point the radio may have been on, so the way out is the restart in onExit().
  wifiUsed = true;
  if (WiFi.status() == WL_CONNECTED) {
    onWifiDone(true);
    return;
  }
  // The usual join screen: it connects to a saved network by itself, and with none saved it
  // lists the networks to pick from. Backing out of it goes back to the book.
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& wifi) { onWifiDone(!wifi.isCancelled); });
}

void SendHighlightsActivity::onWifiDone(const bool connected) {
  if (!connected || WiFi.status() != WL_CONNECTED) {
    esp_wifi_stop();
    showResult(tr(STR_SEND_HL_NOT_CONNECTED));
    return;
  }
  {
    RenderLock lock(*this);
    state = State::Busy;
  }
  jobPending = true;
  requestUpdate();
}

void SendHighlightsActivity::runJob() {
  using namespace highlightexport;
  requestUpdateAndWait();  // paint "Sending highlights..." before the requests block

  size_t sent = 0;
  std::string problem;
  {
    highlight_export_client::Job job;
    switch (job.begin(destination)) {
      case highlight_export_client::Setup::Ok:
        break;
      case highlight_export_client::Setup::NotSetUp:
        problem = tr(STR_SEND_HL_NOT_SET_UP);
        break;
      case highlight_export_client::Setup::LowMemory:
        problem = tr(STR_DICT_LOW_MEMORY);
        break;
      case highlight_export_client::Setup::NoClock:
        problem = tr(STR_SEND_HL_NO_CLOCK);
        break;
      case highlight_export_client::Setup::NoTls:
      default:
        problem = tr(STR_SEND_HL_NO_CA);
        break;
    }

    if (problem.empty()) {
      // In reading order: by chapter, then where in it (as the vault note export does).
      const size_t total = CLIPPINGS.clippingCount();
      std::vector<uint16_t> order(total);
      for (size_t i = 0; i < total; i++) order[i] = static_cast<uint16_t>(i);
      std::stable_sort(order.begin(), order.end(), [](const uint16_t a, const uint16_t b) {
        const Clipping* x = CLIPPINGS.clippingAt(a);
        const Clipping* y = CLIPPINGS.clippingAt(b);
        if (x->spineIndex != y->spineIndex) return x->spineIndex < y->spineIndex;
        if (x->startOffset != y->startOffset) return x->startOffset < y->startOffset;
        return x->startPage < y->startPage;
      });

      const highlightexport::Book book{CLIPPINGS.title(), CLIPPINGS.author()};
      ReadwiseBatch batch(book);
      std::vector<uint64_t> batchKeys;
      batchKeys.reserve(READWISE_MAX_BATCH);
      Outcome outcome;
      outcome.result = Result::Ok;
      // One Readwise request: on success its highlights are marked as sent and the marks saved.
      const auto flush = [&]() {
        if (batch.empty()) return true;
        outcome = job.post(batch.body());
        if (!outcome.ok()) return false;
        sent += batch.count();
        for (const uint64_t key : batchKeys) markersDirty = markers.add(key) || markersDirty;
        saveMarkers();
        batch.reset();
        batchKeys.clear();
        return true;
      };

      std::string text;
      for (size_t rank = 0; rank < total; rank++) {
        const size_t index = order[rank];
        const Clipping* clipping = CLIPPINGS.clippingAt(index);
        const uint64_t key = keyFor(index);
        if (!sendAll && markers.contains(key)) continue;
        if (!CLIPPINGS.readClippingText(index, text) || text.empty()) {
          LOG_ERR("EXP", "Clipping %u has no text, skipped", static_cast<unsigned>(index));
          continue;
        }
        Highlight highlight;
        highlight.text = text;
        highlight.chapter = clipping->chapterTitle;
        highlight.order = static_cast<uint32_t>(rank + 1);
        highlight.timestamp = clipping->timestamp;
        if (destination == Destination::Readwise) {
          // The batch copies the text, so `text` is free for the next clipping.
          if (!batch.add(highlight)) {
            if (!flush()) break;
            batch.add(highlight);
          }
          batchKeys.push_back(key);
        } else {
          outcome = job.post(memosBody(book, highlight));
          if (!outcome.ok()) break;
          sent++;
          markersDirty = markers.add(key) || markersDirty;
          if (sent % MEMOS_SAVE_EVERY == 0) saveMarkers();
        }
      }
      if (outcome.ok() && destination == Destination::Readwise) flush();
      saveMarkers();
      if (!outcome.ok()) problem = errorText(outcome);
    }
    job.end();
  }
  // The radio goes down now, whatever came back.
  esp_wifi_stop();
  LOG_INF("EXP", "%u highlights sent%s", static_cast<unsigned>(sent), problem.empty() ? "" : ", stopped on an error");

  std::string text = problem;
  if (problem.empty() || sent > 0) {
    if (!text.empty()) text += "\n";
    text += formatCount(StrId::STR_SENT_HL_COUNT, static_cast<int>(sent));
  }
  showResult(text);
}

void SendHighlightsActivity::showResult(const std::string& text) {
  {
    RenderLock lock(*this);
    state = State::Done;
  }
  const char* options[] = {tr(STR_OK_BUTTON)};
  const StrId titleId =
      destination == highlightexport::Destination::Readwise ? StrId::STR_SEND_TO_READWISE : StrId::STR_SEND_TO_MEMOS;
  popup.showMessage(I18N.get(titleId), text.c_str(), options, 1, 0, [this](int) { returnToReader(); });
  requestUpdate();
}

void SendHighlightsActivity::returnToReader() {
  if (leaving) return;
  leaving = true;
  activityManager.goToReader(bookPath);
}

void SendHighlightsActivity::onExit() {
  Activity::onExit();
  CLIPPINGS.unload();
  if (wifiUsed) {
    // As after KOReader sync: drop the connection and restart into the book on a clean heap.
    WiFi.disconnect(false);
    delay(30);
    silentRestartToReader();
  }
}

void SendHighlightsActivity::loop() {
  if (jobPending) {
    jobPending = false;
    runJob();
    return;
  }
  if (leaving || (state != State::Choosing && state != State::Done)) return;
  popup.handleInput(mappedInput, [this] { requestUpdate(); });
  // Closed without a choice (Back, or a tap beside it), or the result was read: back to the book.
  // A choice that started the job has moved the state on by now.
  if (!popup.isActive() && (state == State::Choosing || state == State::Done)) returnToReader();
}

void SendHighlightsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (state == State::Busy || state == State::Wifi) {
    GUI.drawPopup(renderer, tr(STR_SENDING_HIGHLIGHTS));
    return;
  }
  popup.processRender(renderer, mappedInput);
}
