#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  const auto res = updater.checkForUpdate();
  // NO_UPDATE here means the release carries no firmware asset for this board
  // (expected until per-board assets are published) — not a failure.
  if (res == OtaUpdater::NO_UPDATE) {
    LOG_DBG("OTA", "No firmware asset for this board in latest release");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    return;
  }

  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
  noteLines.clear();
  noteScroll = 0;
  requestUpdate();
}

void OtaUpdateActivity::wrapNotes(const int width) {
  noteLines.clear();
  const std::string& notes = updater.getReleaseNotes();
  size_t start = 0;
  while (start <= notes.size()) {
    size_t end = notes.find('\n', start);
    if (end == std::string::npos) end = notes.size();
    std::string para = notes.substr(start, end - start);
    if (!para.empty() && para.back() == '\r') para.pop_back();
    // Plain text: drop Markdown bold markers (arrows and task boxes are in the UI font since 0.52.0).
    for (size_t at; (at = para.find("**")) != std::string::npos;) para.erase(at, 2);
    // Code backticks read as clutter.
    para.erase(std::remove(para.begin(), para.end(), '`'), para.end());
    if (para.empty()) {
      // Keep one blank line between paragraphs, none at the top.
      if (!noteLines.empty() && !noteLines.back().empty()) noteLines.emplace_back();
    } else {
      const auto lines = renderer.wrappedText(UI_10_FONT_ID, para.c_str(), width, 40);
      noteLines.insert(noteLines.end(), lines.begin(), lines.end());
    }
    start = end + 1;
  }
  while (!noteLines.empty() && noteLines.back().empty()) noteLines.pop_back();
  if (noteLines.empty()) noteLines.emplace_back(tr(STR_NO_RELEASE_NOTES));
}

void OtaUpdateActivity::onEnter() {
  Activity::onEnter();

  // Turn on WiFi immediately
  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the SHUTTING_DOWN state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OtaUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_UPDATE));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  float updaterProgress = 0;
  if (state == UPDATE_IN_PROGRESS) {
    LOG_DBG("OTA", "Update progress: %d / %d", updater.getProcessedSize(), updater.getTotalSize());
    updaterProgress = static_cast<float>(updater.getProcessedSize()) / static_cast<float>(updater.getTotalSize());
    // Only update every 2% at the most
    if (static_cast<int>(updaterProgress * 50) == lastUpdaterPercentage / 2) {
      return;
    }
    lastUpdaterPercentage = static_cast<int>(updaterProgress * 100);
  }

  if (state == CHECKING_FOR_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_CHECKING_UPDATE));
  } else if (state == WAITING_CONFIRMATION) {
    // Versions at the top, the release notes scrolling under them, and
    // Cancel/Update fixed at the bottom so they're always in reach (#9).
    const int side = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, side, y, (std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION).c_str());
    y += height;
    renderer.drawText(UI_10_FONT_ID, side, y, (std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion()).c_str(),
                      true, EpdFontFamily::BOLD);
    y += height + metrics.verticalSpacing;
    renderer.drawLine(side, y, pageWidth - side, y);
    y += metrics.verticalSpacing;

    const int buttonH = 56;
    const int buttonTop = pageHeight - buttonH - metrics.verticalSpacing * 2;
    const int gap = metrics.verticalSpacing * 2;
    const int buttonW = (pageWidth - side * 2 - gap) / 2;
    cancelRect = Rect{side, buttonTop, buttonW, buttonH};
    updateRect = Rect{side + buttonW + gap, buttonTop, buttonW, buttonH};

    if (noteLines.empty()) wrapNotes(pageWidth - side * 2);
    const int notesBottom = buttonTop - metrics.verticalSpacing * 2;
    notesPerPage = std::max(1, (notesBottom - y) / height);
    const int maxScroll = std::max(0, static_cast<int>(noteLines.size()) - notesPerPage);
    noteScroll = std::min(std::max(noteScroll, 0), maxScroll);
    for (int i = 0; i < notesPerPage && noteScroll + i < static_cast<int>(noteLines.size()); ++i) {
      const std::string& line = noteLines[noteScroll + i];
      const bool heading = line.rfind("#", 0) == 0;
      const size_t skip = heading ? line.find_first_not_of("# ") : 0;
      renderer.drawText(UI_10_FONT_ID, side, y + i * height, skip == std::string::npos ? "" : line.c_str() + skip, true,
                        heading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    }
    if (maxScroll > 0) {
      // Where we are in the notes: "lines 1-12 of 30" as a thin bar on the right.
      const int trackTop = y;
      const int trackH = notesBottom - y;
      const int thumbH = std::max(12, trackH * notesPerPage / static_cast<int>(noteLines.size()));
      const int thumbTop = trackTop + (trackH - thumbH) * noteScroll / maxScroll;
      renderer.fillRect(pageWidth - side / 2 - 2, thumbTop, 4, thumbH);
    }

    renderer.drawRect(cancelRect.x, cancelRect.y, cancelRect.width, cancelRect.height, 2, true);
    renderer.fillRect(updateRect.x, updateRect.y, updateRect.width, updateRect.height);
    const int textY = buttonTop + (buttonH - height) / 2;
    const char* cancel = tr(STR_CANCEL);
    const char* update = tr(STR_UPDATE);
    renderer.drawText(UI_10_FONT_ID, cancelRect.x + (buttonW - renderer.getTextWidth(UI_10_FONT_ID, cancel)) / 2, textY,
                      cancel);
    renderer.drawText(UI_10_FONT_ID,
                      updateRect.x + (buttonW - renderer.getTextWidth(UI_10_FONT_ID, update, EpdFontFamily::BOLD)) / 2,
                      textY, update, false, EpdFontFamily::BOLD);
  } else if (state == UPDATE_IN_PROGRESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING));

    int y = top + height + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(updaterProgress * 100), 100);

    y += metrics.progressBarHeight + metrics.verticalSpacing;
    // Percent label is drawn by BaseTheme::drawProgressBar; this slot is left intentionally empty
    // so the bytes line below stays at the same Y it was at when the activity drew its own percent.
    y += height + metrics.verticalSpacing;
    renderer.drawCenteredText(
        UI_10_FONT_ID, y,
        (std::to_string(updater.getProcessedSize()) + " / " + std::to_string(updater.getTotalSize())).c_str());
  } else if (state == NO_UPDATE) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_NO_UPDATE), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (failedDetail != nullptr) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + metrics.verticalSpacing, failedDetail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == FINISHED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    const int hintY = top + height + metrics.verticalSpacing;
    const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                          pageHeight - hintY};
    UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_POWER_ON_HINT), 3, true,
                                     EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  }

  renderer.displayBuffer();
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
  }
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* ctx) {
        // immediate=true notifies the render task directly. The default deferred path only
        // sets a flag consumed at the end of ActivityManager::loop(), which never runs while
        // installUpdate() blocks this task.
        static_cast<OtaUpdateActivity*>(ctx)->requestUpdate(true);
      },
      this);

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = res == OtaUpdater::WRONG_DEVICE_ERROR ? tr(STR_FIRMWARE_WRONG_DEVICE) : nullptr;
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(3000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}

void OtaUpdateActivity::loop() {
  if (state == WAITING_CONFIRMATION) {
    int x = 0;
    int y = 0;
    const auto swipe = mappedInput.wasSwipe();
    int scrollBy = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Down) || swipe == MappedInputManager::SwipeDir::Up)
      scrollBy = std::max(1, notesPerPage - 1);
    else if (mappedInput.wasPressed(MappedInputManager::Button::Up) || swipe == MappedInputManager::SwipeDir::Down)
      scrollBy = -std::max(1, notesPerPage - 1);
    if (scrollBy != 0) {
      noteScroll += scrollBy;
      requestUpdate();
      return;
    }
    const bool tapped = mappedInput.wasScreenTapped(x, y);
    const auto inside = [x, y](const Rect& r) {
      return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
    };
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) || (tapped && inside(updateRect))) {
      runUpdateInstall();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || (tapped && inside(cancelRect))) {
      finish();
    }
    return;
  }

  if (state == FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == NO_UPDATE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }

  if (state == SHUTTING_DOWN) {
    ESP.restart();
  }
}
