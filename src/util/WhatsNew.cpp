#include "WhatsNew.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "util/ChangelogData.generated.h"

namespace whatsnew {
namespace {

constexpr const char* NOTE = "/.vaultink/whats-new.md";

// "0.24.0-x4pro" -> {0, 24, 0}; false when it doesn't start with three numbers.
bool parse(const std::string& v, int out[3]) { return sscanf(v.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]) == 3; }

int compare(const std::string& a, const std::string& b) {
  int x[3] = {0, 0, 0};
  int y[3] = {0, 0, 0};
  parse(a, x);
  parse(b, y);
  for (int i = 0; i < 3; ++i) {
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  }
  return 0;
}

std::string running() {
  int v[3] = {0, 0, 0};
  if (!parse(CROSSPOINT_VERSION, v)) return "";
  return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
}

// The note with the releases after `since` (all when empty), newest first.
bool write(const std::string& since, std::string& path) {
  std::string text = std::string("# ") + tr(STR_WHATS_NEW) + "\n\n";
  // On a CrossPoint home style, point to the VaultInk home: the new features land there.
  if (SETTINGS.uiTheme != CrossPointSettings::VAULTINK)
    text += std::string("> ") + tr(STR_WHATS_NEW_THEME_TIP) + "\n\n";
  int count = 0;
  for (int i = 0; i < changelog_data::COUNT; ++i) {
    const auto& e = changelog_data::ENTRIES[i];
    if (e.text[0] == '\0') continue;
    if (!since.empty() && compare(e.version, since) <= 0) continue;
    text += std::string("## ") + e.version + " (" + e.date + ")\n\n" + e.text + "\n\n";
    ++count;
  }
  if (count == 0) return false;
  // The reading font has no arrow; menu paths read fine with ">".
  for (size_t at; (at = text.find("\xE2\x86\x92")) != std::string::npos;) text.replace(at, 3, ">");
  Storage.ensureDirectoryExists("/.vaultink");
  if (!Storage.writeFile(NOTE, String(text.c_str()))) return false;
  // The note's page cache is keyed by its path: drop it so the new text is laid out.
  Storage.removeDir(("/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(NOTE))).c_str());
  path = NOTE;
  return true;
}

}  // namespace

bool afterUpdate(std::string& path) {
  const std::string now = running();
  if (now.empty()) return false;
  std::string before = APP_STATE.lastVersionRun;
  if (before == now) return false;
  // Versions before What's New didn't record themselves: a device already in use (it has seen
  // the welcome guide) gets this release's entry; a fresh install gets the welcome guide.
  const bool unknownBefore = before.empty() && APP_STATE.welcomeGuideSeen != 0;
  if (unknownBefore) {
    before.clear();
    for (int i = 0; i + 1 < changelog_data::COUNT; ++i) {
      if (compare(changelog_data::ENTRIES[i].version, now) == 0) before = changelog_data::ENTRIES[i + 1].version;
    }
  }
  const std::string previous = APP_STATE.lastVersionRun;
  APP_STATE.lastVersionRun = now;
  APP_STATE.saveToFile();
  if (before.empty() || compare(before, now) >= 0) return false;  // first install, or a downgrade
  if (!SETTINGS.whatsNewAfterUpdate) return false;
  LOG_INF("NEW", "Updated from %s to %s", previous.empty() ? "(unknown)" : previous.c_str(), now.c_str());
  if (write(before, path)) return true;
  APP_STATE.lastVersionRun = previous;  // couldn't write the note: try again next boot
  APP_STATE.saveToFile();
  return false;
}

bool writeAll(std::string& path) { return write("", path); }

}  // namespace whatsnew
