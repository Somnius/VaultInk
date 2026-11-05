#include "ReadingPresetsStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <memory>

#include "CrossPointSettings.h"

namespace {
constexpr const char* FILE_PATH = "/.crosspoint/reading-presets.json";
constexpr const char* TMP_PATH = "/.crosspoint/reading-presets.json.tmp";
constexpr const char* BAD_PATH = "/.crosspoint/reading-presets.json.bad";
}  // namespace

void ReadingPresetsStore::ensureLoaded() {
  if (loaded) return;
  loaded = true;
  list.reset();
  // A save cut off between removing the old file and renaming the new one leaves only the .tmp.
  if (!Storage.exists(FILE_PATH) && Storage.exists(TMP_PATH)) Storage.rename(TMP_PATH, FILE_PATH);
  if (!Storage.exists(FILE_PATH)) return;

  const String json = Storage.readFile(FILE_PATH);
  if (json.isEmpty() || !readingpresets::parse(json.c_str(), json.length(), list)) {
    // Keep it for a look later instead of writing an empty list over it on the next save.
    LOG_ERR("RPS", "reading-presets.json unreadable; kept as .bad");
    Storage.remove(BAD_PATH);
    Storage.rename(FILE_PATH, BAD_PATH);
    list.reset();
  }
}

const readingpresets::Preset& ReadingPresetsStore::slot(const int index) {
  ensureLoaded();
  return list.slots[index < 0 || index >= SLOTS ? 0 : index];
}

int ReadingPresetsStore::currentSlot() {
  ensureLoaded();
  return list.find(capture());
}

readingpresets::Layout ReadingPresetsStore::capture() {
  readingpresets::Layout l;
  l.fontFamily = SETTINGS.fontFamily;
  strncpy(l.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(l.sdFontFamilyName) - 1);
  l.sdFontFamilyName[sizeof(l.sdFontFamilyName) - 1] = '\0';
  l.fontPointSize = SETTINGS.fontPointSize;
  l.lineSpacing = SETTINGS.lineSpacing;
  l.paragraphAlignment = SETTINGS.paragraphAlignment;
  l.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  l.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  l.evenWordGaps = SETTINGS.evenWordGaps;
  l.wordSpacing = SETTINGS.wordSpacing;
  l.characterSpacing = SETTINGS.characterSpacing;
  l.screenMargin = SETTINGS.screenMargin;
  l.paragraphIndentSpaces = SETTINGS.paragraphIndentSpaces;
  l.textLigatures = SETTINGS.textLigatures;
  l.textKerning = SETTINGS.textKerning;
  l.textWidowOrphan = SETTINGS.textWidowOrphan;
  l.textKeepHeadings = SETTINGS.textKeepHeadings;
  l.textHanging = SETTINGS.textHanging;
  return l;
}

bool ReadingPresetsStore::saveCurrent(const int index, const std::string& name) {
  ensureLoaded();
  if (!readingpresets::save(list, index, capture())) return false;
  if (!name.empty()) readingpresets::setName(list, index, name.c_str());
  return save();
}

bool ReadingPresetsStore::apply(const int index) {
  ensureLoaded();
  if (index < 0 || index >= SLOTS || !list.slots[index].saved) return false;
  const readingpresets::Layout& l = list.slots[index].layout;
  SETTINGS.fontFamily = l.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT;
  strncpy(SETTINGS.sdFontFamilyName, l.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.fontPointSize = l.fontPointSize;  // SdCardFontSystem::ensureLoaded snaps it to a shipped size
  SETTINGS.lineSpacing = l.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT;
  SETTINGS.paragraphAlignment = l.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT;
  SETTINGS.hyphenationEnabled = l.hyphenationEnabled ? 1 : 0;
  SETTINGS.focusReadingEnabled = l.focusReadingEnabled ? 1 : 0;
  SETTINGS.evenWordGaps = l.evenWordGaps > 2 ? 2 : l.evenWordGaps;
  SETTINGS.wordSpacing = l.wordSpacing < CrossPointSettings::WORD_SPACING_MIN   ? CrossPointSettings::WORD_SPACING_MIN
                         : l.wordSpacing > CrossPointSettings::WORD_SPACING_MAX ? CrossPointSettings::WORD_SPACING_MAX
                                                                                : l.wordSpacing;
  SETTINGS.characterSpacing = l.characterSpacing > 2 * CrossPointSettings::CHARACTER_SPACING_OFFSET
                                  ? CrossPointSettings::CHARACTER_SPACING_OFFSET
                                  : l.characterSpacing;
  SETTINGS.screenMargin =
      l.screenMargin > CrossPointSettings::SCREEN_MARGIN_MAX ? CrossPointSettings::SCREEN_MARGIN_MAX : l.screenMargin;
  SETTINGS.paragraphIndentSpaces = l.paragraphIndentSpaces > 5 ? 5 : l.paragraphIndentSpaces;
  SETTINGS.textLigatures = l.textLigatures ? 1 : 0;
  SETTINGS.textKerning = l.textKerning ? 1 : 0;
  SETTINGS.textWidowOrphan = l.textWidowOrphan ? 1 : 0;
  SETTINGS.textKeepHeadings = l.textKeepHeadings ? 1 : 0;
  SETTINGS.textHanging = l.textHanging ? 1 : 0;
  SETTINGS.saveToFile();
  return true;
}

bool ReadingPresetsStore::save() {
  // 10 slots at ~250 bytes each, ~610 with every name byte escaped; the buffer lives on the heap, not the stack of
  // the UI task.
  constexpr size_t CAP = 6400;
  std::unique_ptr<char[]> buf(new (std::nothrow) char[CAP]);
  if (!buf) return false;
  const size_t need = readingpresets::serialize(list, buf.get(), CAP);
  if (need >= CAP) {
    LOG_ERR("RPS", "presets do not fit (%u bytes)", static_cast<unsigned>(need));
    return false;
  }
  Storage.mkdir("/.crosspoint");
  const String content(buf.get());
  if (content.isEmpty()) {  // the copy failed for memory: never rename an empty file over the presets
    LOG_ERR("RPS", "no memory to save reading presets");
    return false;
  }
  if (!Storage.writeFile(TMP_PATH, content)) {
    LOG_ERR("RPS", "cannot write reading presets");
    return false;
  }
  Storage.remove(FILE_PATH);
  if (!Storage.rename(TMP_PATH, FILE_PATH)) {
    LOG_ERR("RPS", "cannot put reading presets in place");
    return false;
  }
  return true;
}
