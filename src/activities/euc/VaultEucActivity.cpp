#include "VaultEucActivity.h"

#include <Catalog.h>
#include <HalDisplay.h>
#include <HalHaptics.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Protocols.h>
#include <Session.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <WiFi.h>
#endif

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

using euc::Brand;
using euc::Command;
using euc::LinkState;
using euc::PresetFile;
using euc::Telemetry;
using euc::UnitSettings;
using euc::WheelPreset;

namespace {
constexpr char kDir[] = "/VaultEUC";
constexpr char kPresetPath[] = "/VaultEUC/wheels.txt";
constexpr char kPrefsPath[] = "/VaultEUC/settings.txt";
constexpr char kIndexPath[] = "/VaultEUC/rides.idx";

constexpr int ROW_H = 62;
constexpr int TAG_ADD = 1000;
constexpr int TAG_RIDES = 1001;
constexpr int TAG_SETTINGS = 1002;
constexpr int TAG_REMOVE = 1003;
constexpr int TAG_RESCAN = 1004;
constexpr int TAG_UNITS = 1100;
constexpr int TAG_TEMP = 1101;
constexpr int TAG_REFRESH = 1102;
constexpr int TAG_AWAKE = 1103;
constexpr int TAG_VIBRATE = 1104;
constexpr int TAG_DEMO = 1105;
constexpr int TAG_ALARMS = 1106;
constexpr int TAG_LIGHTS = 1200;
constexpr int TAG_HORN = 1201;
constexpr int TAG_BEEP = 1202;
constexpr int TAG_PEDAL = 1203;
constexpr int TAG_MILES = 1204;
constexpr int TAG_RESET_TRIP = 1205;
constexpr int TAG_INFO = 1206;
constexpr int TAG_WHEEL_ALARMS = 1207;
constexpr int TAG_REFRESH_WHEEL = 1208;
constexpr int TAG_DISCONNECT = 1209;
constexpr int TAG_THR_PWM = 1300;
constexpr int TAG_THR_SPEED = 1301;
constexpr int TAG_THR_TEMP = 1302;
constexpr int TAG_THR_CURRENT = 1303;
constexpr int TAG_THR_BATTERY = 1304;
constexpr int TAG_THR_WHEEL = 1305;

constexpr float kPwmChoices[] = {0, 60, 70, 80, 90};
constexpr float kSpeedChoices[] = {0, 20, 25, 30, 35, 40, 45, 50};
constexpr float kTempChoices[] = {0, 50, 55, 60, 65, 70, 75, 80};
constexpr float kCurrentChoices[] = {0, 30, 40, 50, 60, 80, 100};
constexpr float kBatteryChoices[] = {0, 10, 15, 20, 25, 30};

template <size_t N>
float nextChoice(const float (&choices)[N], const float current) {
  size_t at = 0;
  for (size_t i = 0; i < N; ++i) {
    if (std::fabs(choices[i] - current) < 0.5f) at = i;
  }
  return choices[(at + 1) % N];
}

euc::DateTime nowDateTime() {
  int64_t now = trustedtime::trustedNow();
  if (now <= 0) now = static_cast<int64_t>(time(nullptr));
  return euc::fromEpochSeconds(now);
}

int64_t nowEpoch() {
  const int64_t now = trustedtime::trustedNow();
  return now > 0 ? now : static_cast<int64_t>(time(nullptr));
}

const char* lightName(const uint8_t mode) {
  switch (mode) {
    case 0:
      return tr(STR_STATE_OFF);
    case 1:
      return tr(STR_STATE_ON);
    case 2:
      return tr(STR_EUC_AUTO);
    default:
      return tr(STR_EUC_STROBE);
  }
}
}  // namespace

VaultEucActivity::~VaultEucActivity() = default;

// ---------------------------------------------------------------------------------------------------------------
// Lifecycle

void VaultEucActivity::onEnter() {
  Activity::onEnter();
  Storage.ensureDirectoryExists(kDir);
  loadFiles();
  alarms.setThresholds(prefs.alarms);
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Bluetooth first: with both on, the radio and the Wi-Fi share one antenna and the link to the wheel stutters.
  wifiWasOn = WiFi.getMode() != WIFI_OFF;
  if (wifiWasOn) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
#endif
  go(Screen::Warning);
  requestUpdate();
}

void VaultEucActivity::onExit() {
  disconnectWheel(true);
  stopRadio();
  savePrefs();
  Activity::onExit();
}

void VaultEucActivity::prepareForSleep() {
  disconnectWheel(true);
  savePrefs();
  stopRadio();  // the stack must not be left up when the device sleeps
}

bool VaultEucActivity::preventAutoSleep() { return prefs.keepAwake && screen == Screen::Dash && linkWanted; }

// ---------------------------------------------------------------------------------------------------------------
// Files

void VaultEucActivity::loadFiles() {
  prefs = Prefs{};
  presets = PresetFile{};
  HalFile file;
  if (Storage.exists(kPrefsPath) && Storage.openFileForRead("EUC", kPrefsPath, file)) {
    char text[768];
    const int n = file.read(text, sizeof(text) - 1);
    file.close();
    text[std::max(0, n)] = '\0';
    char* save = nullptr;
    for (char* line = strtok_r(text, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
      char* eq = std::strchr(line, '=');
      if (!eq) continue;
      *eq = '\0';
      const char* key = line;
      const float v = static_cast<float>(std::atof(eq + 1));
      if (!std::strcmp(key, "speed")) prefs.units.speed = v > 0.5f ? euc::SpeedUnit::Mph : euc::SpeedUnit::Kmh;
      if (!std::strcmp(key, "temp")) prefs.units.temp = v > 0.5f ? euc::TempUnit::Fahrenheit : euc::TempUnit::Celsius;
      if (!std::strcmp(key, "refresh") && euc::validRefresh(static_cast<uint8_t>(v))) {
        prefs.refreshSeconds = static_cast<uint8_t>(v);
      }
      if (!std::strcmp(key, "awake")) prefs.keepAwake = v > 0.5f;
      if (!std::strcmp(key, "vibrate")) prefs.vibrate = v > 0.5f;
      if (!std::strcmp(key, "demo")) prefs.demoWheels = v > 0.5f;
      if (!std::strcmp(key, "pwm")) prefs.alarms.pwmPct = v;
      if (!std::strcmp(key, "alarmSpeed")) prefs.alarms.speedKmh = v;
      if (!std::strcmp(key, "alarmTemp")) prefs.alarms.tempC = v;
      if (!std::strcmp(key, "alarmCurrent")) prefs.alarms.currentA = v;
      if (!std::strcmp(key, "alarmBattery")) prefs.alarms.lowBatteryPct = v;
      if (!std::strcmp(key, "alarmWheel")) prefs.alarms.wheelAlarms = v > 0.5f;
    }
    prefs.units =
        euc::UnitSettings{prefs.units.speed, prefs.units.temp,
                          prefs.units.speed == euc::SpeedUnit::Mph ? euc::DistanceUnit::Mi : euc::DistanceUnit::Km};
  }
  auto text = makeUniqueNoThrow<char[]>(euc::kPresetFileCapacity + 1);
  if (text && Storage.exists(kPresetPath) && Storage.openFileForRead("EUC", kPresetPath, file)) {
    const int n = file.read(text.get(), euc::kPresetFileCapacity);
    file.close();
    if (n > 0) euc::parsePresets(text.get(), static_cast<size_t>(n), presets);
  }
}

void VaultEucActivity::savePrefs() const {
  HalFile file;
  if (!Storage.openFileForWrite("EUC", kPrefsPath, file)) return;
  char text[512];
  const int n = std::snprintf(
      text, sizeof(text),
      "speed=%d\ntemp=%d\nrefresh=%u\nawake=%d\nvibrate=%d\ndemo=%d\npwm=%.0f\nalarmSpeed=%.0f\n"
      "alarmTemp=%.0f\nalarmCurrent=%.0f\nalarmBattery=%.0f\nalarmWheel=%d\n",
      prefs.units.speed == euc::SpeedUnit::Mph ? 1 : 0, prefs.units.temp == euc::TempUnit::Fahrenheit ? 1 : 0,
      static_cast<unsigned>(prefs.refreshSeconds), prefs.keepAwake ? 1 : 0, prefs.vibrate ? 1 : 0,
      prefs.demoWheels ? 1 : 0, prefs.alarms.pwmPct, prefs.alarms.speedKmh, prefs.alarms.tempC, prefs.alarms.currentA,
      prefs.alarms.lowBatteryPct, prefs.alarms.wheelAlarms ? 1 : 0);
  file.write(text, static_cast<size_t>(std::min<int>(n, sizeof(text) - 1)));
}

void VaultEucActivity::savePresets() const {
  auto text = makeUniqueNoThrow<char[]>(euc::kPresetFileCapacity + 1);
  if (!text) return;
  const int n = euc::serializePresets(presets, text.get(), euc::kPresetFileCapacity);
  HalFile file;
  if (n > 0 && Storage.openFileForWrite("EUC", kPresetPath, file)) {
    file.write(text.get(), static_cast<size_t>(std::min<int>(n, euc::kPresetFileCapacity)));
  }
}

// One line per finished ride, so the history does not read every CSV: file|start|seconds|km|top|avg|battery used|min V.
void VaultEucActivity::appendRideIndex() const {
  HalFile file = Storage.open(kIndexPath, O_RDWR | O_CREAT | O_AT_END);
  if (!file) return;
  char line[160];
  const int n = std::snprintf(line, sizeof(line), "%s|%lld|%u|%.2f|%.1f|%.1f|%.0f|%.1f\n", rideFileName,
                              static_cast<long long>(rideStartEpoch), static_cast<unsigned>(stats.durationSec()),
                              stats.distanceKm(), stats.topSpeedKmh(), stats.avgMovingKmh(),
                              std::max(0.0f, stats.startBatteryPct() - stats.endBatteryPct()), stats.minVoltage());
  file.write(line, static_cast<size_t>(std::min<int>(n, sizeof(line) - 1)));
}

void VaultEucActivity::loadRideIndex() {
  historyCount = 0;
  HalFile file;
  if (!Storage.exists(kIndexPath) || !Storage.openFileForRead("EUC", kIndexPath, file)) return;
  const size_t size = file.size();
  const size_t read = std::min<size_t>(size, 12000);  // the newest rides are at the end
  if (size > read) file.seekSet(size - read);
  auto text = makeUniqueNoThrow<char[]>(read + 1);
  if (!text) return;
  const int n = file.read(text.get(), read);
  file.close();
  if (n <= 0) return;
  text[n] = '\0';
  char* save = nullptr;
  for (char* line = strtok_r(text.get(), "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
    HistoryEntry e;
    char* parts[8] = {};
    int count = 0;
    char* field = line;
    while (count < 8) {
      parts[count++] = field;
      char* bar = std::strchr(field, '|');
      if (!bar) break;
      *bar = '\0';
      field = bar + 1;
    }
    if (count < 8) continue;
    std::snprintf(e.file, sizeof(e.file), "%s", parts[0]);
    e.start = std::atoll(parts[1]);
    e.duration = static_cast<uint32_t>(std::atol(parts[2]));
    e.km = static_cast<float>(std::atof(parts[3]));
    e.top = static_cast<float>(std::atof(parts[4]));
    e.avg = static_cast<float>(std::atof(parts[5]));
    e.batt = static_cast<float>(std::atof(parts[6]));
    e.minV = static_cast<float>(std::atof(parts[7]));
    // Newest first.
    const int keep = std::min(historyCount, MAX_HISTORY - 1);
    for (int i = keep; i > 0; --i) history[i] = history[i - 1];
    history[0] = e;
    historyCount = std::min(historyCount + 1, MAX_HISTORY);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Radio

void VaultEucActivity::startRadio() {
  if (!radio) radio.reset(euc::makeNimBleTransport());
  if (radio && radio->state() == LinkState::Off && !radio->begin()) {
    LOG_ERR("EUC", "radio: %s", radio->lastError());
  }
  if (!fake) fake.reset(euc::makeFakeTransport());
  if (fake && fake->state() == LinkState::Off) fake->begin();
}

void VaultEucActivity::stopRadio() {
  if (radio) radio->end();
  if (fake) fake->end();
  radio.reset();
  fake.reset();
  active = nullptr;
}

euc::Transport* VaultEucActivity::transportFor(const euc::ScanEntry& entry) const {
  return entry.demo ? fake.get() : radio.get();
}

void VaultEucActivity::startScan() {
  scannedCount = 0;
  if (radio && radio->state() != LinkState::Off) radio->startScan();
  if (fake && (prefs.demoWheels || !radio)) fake->startScan();
  lastScanPoll = 0;
}

void VaultEucActivity::connectTo(const WheelPreset& preset) {
  euc::ScanEntry entry;
  std::snprintf(entry.name, sizeof(entry.name), "%s", preset.nickname);
  std::snprintf(entry.addr, sizeof(entry.addr), "%s", preset.address);
  entry.addrType = preset.addressType;
  entry.demo = std::strncmp(preset.address, "DE:AD:BE:EF", 11) == 0;
  entry.demoBrand = preset.brand;
  connectEntry(entry, preset.brand);
}

void VaultEucActivity::connectEntry(const euc::ScanEntry& entry, const Brand brand) {
  disconnectWheel(true);
  startRadio();
  target = entry;
  targetBrand = brand;
  active = transportFor(entry);
  if (!active || active->state() == LinkState::Off) {
    // No radio (it did not start): say it on the Dash screen through the empty state.
    active = nullptr;
    return;
  }
  protocol = euc::makeProtocol(brand);
  if (!protocol) return;
  live = Telemetry{};
  shown = Telemetry{};
  alarms.setThresholds(prefs.alarms);
  alarms.reset();
  stats.reset();
  gotTelemetry = false;
  linkWanted = true;
  reconnectTries = 0;
  lastAlarm = euc::AlarmKind::None;
  active->connect(target, protocol->profile(), brand);
  connectedAt = 0;
  go(Screen::Dash);
}

void VaultEucActivity::disconnectWheel(const bool saveRide) {
  if (ride) {
    closeRideLog();
    if (saveRide && stats.updates() > 5) appendRideIndex();
  }
  linkWanted = false;
  if (active) active->disconnect();
  protocol.reset();
  active = nullptr;
}

void VaultEucActivity::pumpWheel(const uint32_t nowMs) {
  if (!active || !protocol) return;
  active->tick(nowMs);
  const LinkState state = active->state();
  if (state == LinkState::Connected) {
    if (connectedAt == 0) {
      connectedAt = nowMs;
      reconnectTries = 0;
      protocol->start(*active, nowMs);
      requestUpdate();
    }
    uint8_t buf[256];
    bool changed = false;
    for (int guard = 0; guard < 64; ++guard) {
      const size_t n = active->read(buf, sizeof(buf));
      if (n == 0) break;
      changed = protocol->feed(buf, n, live) || changed;
    }
    protocol->poll(*active, nowMs);
    if (changed) onTelemetry(nowMs);
    return;
  }
  if ((state == LinkState::Lost || state == LinkState::Failed) && linkWanted) {
    if (connectedAt != 0) {
      connectedAt = 0;
      reconnectAt = nowMs + 3000;
      requestUpdate();
    }
    if (reconnectAt == 0) reconnectAt = nowMs + 3000;
    if (nowMs >= reconnectAt) {
      // A fresh decoder: a half frame from the last link must not poison the next.
      reconnectTries++;
      reconnectAt = nowMs + std::min<uint32_t>(30000, 3000u * static_cast<uint32_t>(reconnectTries));
      protocol = euc::makeProtocol(targetBrand);
      if (protocol) active->connect(target, protocol->profile(), targetBrand);
      requestUpdate();
    }
  }
}

void VaultEucActivity::onTelemetry(const uint32_t nowMs) {
  gotTelemetry = true;
  lastFrameMs = nowMs;
  stats.update(live, nowMs);
  const euc::AlarmEvent event = alarms.evaluate(live, nowMs);
  if (event.result == euc::AlarmResult::Fire) {
    if (prefs.vibrate) HalHaptics::longPress(true, SETTINGS.hapticIntensity);
    // The slow screen would sit on an old number: an alarm draws right away, at most once a second.
    if (nowMs - lastAlarmDraw >= 1000) {
      lastAlarmDraw = nowMs;
      shown = live;
      lastAlarm = event.kind;
      requestUpdate();
    }
  } else if (event.result == euc::AlarmResult::Quiet) {
    if (lastAlarm != euc::AlarmKind::None) {
      // The banner must not outlive the alarm: draw once now (at most once a second).
      lastAlarm = euc::AlarmKind::None;
      if (nowMs - lastAlarmDraw >= 1000) {
        lastAlarmDraw = nowMs;
        shown = live;
        requestUpdate();
      }
    }
  }
  if (!ride) openRideLog();
  if (ride && nowMs - lastLogMs >= 1000) {
    lastLogMs = nowMs;
    writeRideLine(nowMs);
  }
  const uint32_t interval = static_cast<uint32_t>(euc::session::clampRefreshSeconds(prefs.refreshSeconds)) * 1000u;
  if (lastDraw == 0 || nowMs - lastDraw >= interval) {
    lastDraw = nowMs;
    shown = live;
    requestUpdate();
  }
}

void VaultEucActivity::openRideLog() {
  if (!live.has(euc::F_SPEED) && !live.has(euc::F_VOLTAGE)) return;
  const euc::DateTime when = nowDateTime();
  const char* nick = activePreset >= 0 && activePreset < presets.count ? presets.wheels[activePreset].nickname : "ride";
  std::snprintf(rideNick, sizeof(rideNick), "%s", nick);
  euc::ridelog::fileName(rideFileName, sizeof(rideFileName), rideNick, when);
  char path[96];
  std::snprintf(path, sizeof(path), "%s/%s", kDir, rideFileName);
  ride = makeUniqueNoThrow<RideFile>();
  if (!ride) return;
  const bool exists = Storage.exists(path);
  ride->file = Storage.open(path, O_RDWR | O_CREAT | O_AT_END);
  if (!ride->file) {
    ride.reset();
    return;
  }
  if (!exists) {
    char header[160];
    const int n = euc::ridelog::formatHeader(header, sizeof(header));
    ride->file.write(header, static_cast<size_t>(std::min<int>(n, sizeof(header) - 1)));
  }
  rideStartEpoch = nowEpoch();
  lastLogMs = 0;
}

void VaultEucActivity::writeRideLine(const uint32_t) {
  if (!ride) return;
  char line[euc::ridelog::kMaxLine];
  const int n = euc::ridelog::formatLine(line, sizeof(line), live, nowDateTime());
  ride->file.write(line, static_cast<size_t>(std::min<int>(n, sizeof(line) - 1)));
  if (++ride->lines % 10 == 0) ride->file.flush();
}

void VaultEucActivity::closeRideLog() {
  if (!ride) return;
  ride->file.flush();
  ride->file.close();
  ride.reset();
}

void VaultEucActivity::sendCommand(const Command command, const int value) {
  if (!active || !protocol || active->state() != LinkState::Connected) return;
  if (!euc::session::commandAllowed(command, live)) return;  // moving (or speed unknown): refused
  protocol->send(command, value, *active);
}

// ---------------------------------------------------------------------------------------------------------------
// Lists

void VaultEucActivity::addRow(const char* label, const char* value, const int tag) {
  if (rowCount >= MAX_ROWS) return;
  Row& r = rows[rowCount++];
  std::snprintf(r.label, sizeof(r.label), "%s", label ? label : "");
  std::snprintf(r.value, sizeof(r.value), "%s", value ? value : "");
  r.tag = tag;
}

void VaultEucActivity::go(const Screen next) {
  if (screen == Screen::Scan && next != Screen::Scan) {
    if (radio) radio->stopScan();
    if (fake) fake->stopScan();
  }
  screen = next;
  focus = 0;
  top = 0;
  buildRows();
  if (next == Screen::Scan) {
    startRadio();
    startScan();
  }
  requestUpdate();
}

void VaultEucActivity::buildRows() {
  rowCount = 0;
  char buf[48];
  const UnitSettings units = prefs.units;
  switch (screen) {
    case Screen::Warning:
    case Screen::Dash:
    case Screen::Ride:
      break;
    case Screen::Wheels:
    case Screen::Remove:
      for (int i = 0; i < presets.count; ++i) {
        std::snprintf(buf, sizeof(buf), "%s %s", presets.wheels[i].maker, presets.wheels[i].model);
        addRow(presets.wheels[i].nickname, buf, i);
      }
      if (screen == Screen::Wheels) {
        addRow(tr(STR_EUC_ADD_WHEEL), ">", TAG_ADD);
        addRow(tr(STR_EUC_RIDES), ">", TAG_RIDES);
        addRow(tr(STR_EUC_SETTINGS), ">", TAG_SETTINGS);
        if (presets.count > 0) addRow(tr(STR_EUC_REMOVE_WHEEL), ">", TAG_REMOVE);
      }
      break;
    case Screen::Scan:
      for (int i = 0; i < scannedCount; ++i) {
        const euc::Detection d =
            euc::catalog::detectBrand(scanned[i].name, scanned[i].service[0] ? scanned[i].service : nullptr);
        std::snprintf(buf, sizeof(buf), "%s  %d", d.count == 1 ? euc::brandName(d.brand()) : "", scanned[i].rssi);
        addRow(scanned[i].name, buf, i);
      }
      addRow(tr(STR_EUC_RESCAN), "", TAG_RESCAN);
      break;
    case Screen::Maker: {
      const char* list[16] = {};
      const size_t n = euc::catalog::makers(list, 16);
      makerCount = static_cast<int>(n);
      for (size_t i = 0; i < n; ++i) {
        std::snprintf(makers[i], sizeof(makers[i]), "%s", list[i]);
        addRow(list[i], ">", static_cast<int>(i));
      }
      break;
    }
    case Screen::Model: {
      const euc::Model* list[48] = {};
      const size_t n = euc::catalog::modelsOf(pickedMaker, list, 48);
      for (size_t i = 0; i < n; ++i) {
        addRow(list[i]->name, euc::brandName(list[i]->brand), static_cast<int>(i));
      }
      break;
    }
    case Screen::More: {
      const bool connected = protocol && active && active->state() == LinkState::Connected;
      if (connected) {
        if (protocol->supports(Command::Lights)) addRow(tr(STR_EUC_LIGHTS), lightName(live.lightMode), TAG_LIGHTS);
        if (protocol->supports(Command::Horn)) addRow(tr(STR_EUC_HORN), "", TAG_HORN);
        if (protocol->supports(Command::Beep)) addRow(tr(STR_EUC_BEEP), "", TAG_BEEP);
        if (protocol->supports(Command::PedalMode)) addRow(tr(STR_EUC_PEDAL), tr(STR_EUC_STOPPED_ONLY), TAG_PEDAL);
        if (protocol->supports(Command::Miles)) addRow(tr(STR_EUC_WHEEL_MILES), tr(STR_EUC_STOPPED_ONLY), TAG_MILES);
        if (protocol->supports(Command::ResetTrip))
          addRow(tr(STR_EUC_RESET_TRIP), tr(STR_EUC_STOPPED_ONLY), TAG_RESET_TRIP);
        if (protocol->supports(Command::RequestInfo)) addRow(tr(STR_EUC_INFO), ">", TAG_INFO);
      }
      std::snprintf(buf, sizeof(buf), "%u s", static_cast<unsigned>(prefs.refreshSeconds));
      addRow(tr(STR_EUC_REFRESH), buf, TAG_REFRESH_WHEEL);
      addRow(tr(STR_EUC_ALARMS), ">", TAG_ALARMS);
      addRow(tr(STR_EUC_DISCONNECT), "", TAG_DISCONNECT);
      break;
    }
    case Screen::Rides:
      loadRideIndex();
      for (int i = 0; i < historyCount; ++i) {
        const euc::DateTime d = euc::fromEpochSeconds(history[i].start);
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u", d.year, d.month, d.day, d.hour, d.minute);
        char km[24];
        euc::formatDistance(km, sizeof(km), history[i].km, units.distance);
        addRow(buf, km, i);
      }
      break;
    case Screen::Settings:
      addRow(tr(STR_EUC_SPEED_UNIT), euc::speedUnitLabel(units.speed), TAG_UNITS);
      addRow(tr(STR_EUC_TEMP_UNIT), euc::tempUnitLabel(units.temp), TAG_TEMP);
      std::snprintf(buf, sizeof(buf), "%u s", static_cast<unsigned>(prefs.refreshSeconds));
      addRow(tr(STR_EUC_REFRESH), buf, TAG_REFRESH);
      addRow(tr(STR_EUC_KEEP_AWAKE), prefs.keepAwake ? tr(STR_STATE_ON) : tr(STR_STATE_OFF), TAG_AWAKE);
      addRow(tr(STR_EUC_VIBRATE), prefs.vibrate ? tr(STR_STATE_ON) : tr(STR_STATE_OFF), TAG_VIBRATE);
      addRow(tr(STR_EUC_DEMO), prefs.demoWheels ? tr(STR_STATE_ON) : tr(STR_STATE_OFF), TAG_DEMO);
      addRow(tr(STR_EUC_ALARMS), ">", TAG_ALARMS);
      break;
    case Screen::Alarms: {
      const euc::AlarmThresholds& a = prefs.alarms;
      const auto show = [&](const char* name, const float v, const char* unit, const int tag) {
        if (v < 0.5f)
          std::snprintf(buf, sizeof(buf), "%s", tr(STR_STATE_OFF));
        else
          std::snprintf(buf, sizeof(buf), "%.0f %s", v, unit);
        addRow(name, buf, tag);
      };
      show(tr(STR_EUC_ALARM_PWM), a.pwmPct, "%", TAG_THR_PWM);
      show(tr(STR_EUC_ALARM_SPEED), a.speedKmh, "km/h", TAG_THR_SPEED);
      show(tr(STR_EUC_ALARM_TEMP), a.tempC, "C", TAG_THR_TEMP);
      show(tr(STR_EUC_ALARM_CURRENT), a.currentA, "A", TAG_THR_CURRENT);
      show(tr(STR_EUC_ALARM_BATTERY), a.lowBatteryPct, "%", TAG_THR_BATTERY);
      addRow(tr(STR_EUC_ALARM_WHEEL), a.wheelAlarms ? tr(STR_STATE_ON) : tr(STR_STATE_OFF), TAG_THR_WHEEL);
      break;
    }
  }
}

void VaultEucActivity::cycleThreshold(const int tag) {
  euc::AlarmThresholds& a = prefs.alarms;
  switch (tag) {
    case TAG_THR_PWM:
      a.pwmPct = nextChoice(kPwmChoices, a.pwmPct);
      break;
    case TAG_THR_SPEED:
      a.speedKmh = nextChoice(kSpeedChoices, a.speedKmh);
      break;
    case TAG_THR_TEMP:
      a.tempC = nextChoice(kTempChoices, a.tempC);
      break;
    case TAG_THR_CURRENT:
      a.currentA = nextChoice(kCurrentChoices, a.currentA);
      break;
    case TAG_THR_BATTERY:
      a.lowBatteryPct = nextChoice(kBatteryChoices, a.lowBatteryPct);
      break;
    default:
      a.wheelAlarms = !a.wheelAlarms;
      break;
  }
  alarms.setThresholds(a);
}

void VaultEucActivity::finishAdding(const char* nickname) {
  WheelPreset preset;
  std::snprintf(preset.nickname, sizeof(preset.nickname), "%s", nickname);
  std::snprintf(preset.maker, sizeof(preset.maker), "%s", picked.demo ? "Demo" : pickedMaker);
  std::snprintf(preset.address, sizeof(preset.address), "%s", picked.addr);
  preset.addressType = picked.addrType;
  preset.brand = targetBrand;
  preset.alarms = prefs.alarms;
  preset.refreshSeconds = prefs.refreshSeconds;
  presets.upsert(preset);
  savePresets();
  go(Screen::Wheels);
}

void VaultEucActivity::activate(const int tag) {
  switch (screen) {
    case Screen::Warning:
      go(Screen::Wheels);
      return;
    case Screen::Wheels:
      if (tag == TAG_ADD) {
        go(Screen::Scan);
      } else if (tag == TAG_RIDES) {
        go(Screen::Rides);
      } else if (tag == TAG_SETTINGS) {
        go(Screen::Settings);
      } else if (tag == TAG_REMOVE) {
        go(Screen::Remove);
      } else if (tag >= 0 && tag < presets.count) {
        activePreset = tag;
        connectTo(presets.wheels[tag]);
      }
      return;
    case Screen::Remove:
      if (tag >= 0 && tag < presets.count) {
        presets.remove(static_cast<size_t>(tag));
        savePresets();
      }
      go(Screen::Wheels);
      return;
    case Screen::Scan:
      if (tag == TAG_RESCAN) {
        startScan();
        buildRows();
      } else if (tag >= 0 && tag < scannedCount) {
        picked = scanned[tag];
        const euc::Detection d = euc::catalog::detectBrand(picked.name, picked.service[0] ? picked.service : nullptr);
        targetBrand = picked.demo ? picked.demoBrand : (d.count > 0 ? d.brand() : Brand::Begode);
        if (picked.demo) {
          activePreset = -1;
          finishAdding(picked.name);
        } else {
          go(Screen::Maker);
        }
      }
      return;
    case Screen::Maker:
      if (tag >= 0 && tag < makerCount) {
        std::snprintf(pickedMaker, sizeof(pickedMaker), "%s", makers[tag]);
        go(Screen::Model);
      }
      return;
    case Screen::Model: {
      const euc::Model* list[48] = {};
      const size_t n = euc::catalog::modelsOf(pickedMaker, list, 48);
      if (tag >= 0 && static_cast<size_t>(tag) < n) {
        targetBrand = list[tag]->brand;
        char suggestion[24];
        std::snprintf(suggestion, sizeof(suggestion), "%s", list[tag]->name);
        auto keyboard =
            makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_EUC_NICKNAME), suggestion, 20);
        if (!keyboard) return;
        startActivityForResult(std::move(keyboard), [this, suggestion](const ActivityResult& result) {
          if (result.isCancelled) {
            go(Screen::Wheels);
            return;
          }
          const std::string& text = std::get<KeyboardResult>(result.data).text;
          finishAdding(text.empty() ? suggestion : text.c_str());
        });
      }
      return;
    }
    case Screen::Rides:
      if (tag >= 0 && tag < historyCount) {
        historyPick = tag;
        go(Screen::Ride);
      }
      return;
    case Screen::Settings:
      switch (tag) {
        case TAG_UNITS:
          prefs.units.speed = prefs.units.speed == euc::SpeedUnit::Kmh ? euc::SpeedUnit::Mph : euc::SpeedUnit::Kmh;
          prefs.units.distance =
              prefs.units.speed == euc::SpeedUnit::Mph ? euc::DistanceUnit::Mi : euc::DistanceUnit::Km;
          break;
        case TAG_TEMP:
          prefs.units.temp =
              prefs.units.temp == euc::TempUnit::Celsius ? euc::TempUnit::Fahrenheit : euc::TempUnit::Celsius;
          break;
        case TAG_REFRESH: {
          const auto* at =
              std::find(std::begin(euc::kRefreshChoices), std::end(euc::kRefreshChoices), prefs.refreshSeconds);
          const size_t i =
              at == std::end(euc::kRefreshChoices) ? 0 : static_cast<size_t>(at - std::begin(euc::kRefreshChoices));
          prefs.refreshSeconds = euc::kRefreshChoices[(i + 1) % std::size(euc::kRefreshChoices)];
          break;
        }
        case TAG_AWAKE:
          prefs.keepAwake = !prefs.keepAwake;
          break;
        case TAG_VIBRATE:
          prefs.vibrate = !prefs.vibrate;
          break;
        case TAG_DEMO:
          prefs.demoWheels = !prefs.demoWheels;
          break;
        case TAG_ALARMS:
          go(Screen::Alarms);
          return;
        default:
          break;
      }
      savePrefs();
      buildRows();
      requestUpdate();
      return;
    case Screen::Alarms:
      cycleThreshold(tag);
      savePrefs();
      buildRows();
      requestUpdate();
      return;
    case Screen::More:
      switch (tag) {
        case TAG_LIGHTS:
          sendCommand(Command::Lights, (live.lightMode + 1) % 4);
          break;
        case TAG_HORN:
          sendCommand(Command::Horn, 0);
          break;
        case TAG_BEEP:
          sendCommand(Command::Beep, 0);
          break;
        case TAG_PEDAL:
          sendCommand(Command::PedalMode, (live.rideMode + 1) % 3);
          break;
        case TAG_MILES:
          sendCommand(Command::Miles, prefs.units.speed == euc::SpeedUnit::Mph ? 1 : 0);
          break;
        case TAG_RESET_TRIP:
          sendCommand(Command::ResetTrip, 0);
          break;
        case TAG_INFO:
          sendCommand(Command::RequestInfo, 0);
          break;
        case TAG_REFRESH_WHEEL: {
          const auto* at =
              std::find(std::begin(euc::kRefreshChoices), std::end(euc::kRefreshChoices), prefs.refreshSeconds);
          const size_t i =
              at == std::end(euc::kRefreshChoices) ? 0 : static_cast<size_t>(at - std::begin(euc::kRefreshChoices));
          prefs.refreshSeconds = euc::kRefreshChoices[(i + 1) % std::size(euc::kRefreshChoices)];
          savePrefs();
          break;
        }
        case TAG_ALARMS:
          go(Screen::Alarms);
          return;
        case TAG_DISCONNECT:
          disconnectWheel(true);
          go(Screen::Wheels);
          return;
        default:
          break;
      }
      buildRows();
      requestUpdate();
      return;
    default:
      return;
  }
}

void VaultEucActivity::back() {
  switch (screen) {
    case Screen::Warning:
    case Screen::Wheels:
      finish();
      return;
    case Screen::Dash:
      disconnectWheel(true);
      go(Screen::Wheels);
      return;
    case Screen::More:
      go(Screen::Dash);
      return;
    case Screen::Alarms:
      go(active ? Screen::More : Screen::Settings);
      return;
    case Screen::Ride:
      go(Screen::Rides);
      return;
    case Screen::Model:
      go(Screen::Maker);
      return;
    default:
      go(Screen::Wheels);
      return;
  }
}

// Where the list's first row is, and how many rows fit between it and the button hints.
int VaultEucActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + 40;
}

int VaultEucActivity::visibleRows() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return std::max(1, (renderer.getScreenHeight() - metrics.buttonHintsHeight - 10 - listTop()) / ROW_H);
}

void VaultEucActivity::moveFocus(const int delta) {
  if (rowCount <= 0) return;
  focus = (focus + delta + rowCount) % rowCount;
  const int visible = visibleRows();
  if (focus < top) top = focus;
  if (focus >= top + visible) top = focus - visible + 1;
}

bool VaultEucActivity::handleTap(const int x, const int y) {
  if (screen == Screen::Warning || screen == Screen::Ride) {
    if (screen == Screen::Warning) activate(0);
    return true;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (screen == Screen::Dash) {
    // The four buttons along the bottom: more, alarms off? (no): More, Disconnect.
    const int buttonTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - 80;
    if (y >= buttonTop && y < buttonTop + 70) {
      if (x < renderer.getScreenWidth() / 2) {
        go(Screen::More);
      } else {
        back();
      }
      return true;
    }
    return false;
  }
  if (y < listTop()) return false;
  const int index = top + (y - listTop()) / ROW_H;
  if (index >= top && index < top + visibleRows() && index < rowCount) {
    focus = index;
    activate(rows[index].tag);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Loop

void VaultEucActivity::loop() {
  using Button = MappedInputManager::Button;
  const uint32_t now = millis();

  if (screen == Screen::Scan) {
    // Collect what the radios found, about once a second.
    if (now - lastScanPoll > 800) {
      lastScanPoll = now;
      int n = 0;
      for (const euc::Transport* t : {radio.get(), fake.get()}) {
        if (!t) continue;
        for (int i = 0; i < t->scanCount() && n < 16; ++i) {
          euc::ScanEntry e;
          if (t->scanAt(i, e) && (!e.demo || prefs.demoWheels || !radio)) scanned[n++] = e;
        }
      }
      if (n != scannedCount) {
        scannedCount = n;
        buildRows();
        requestUpdate();
      }
    }
  }
  if (screen == Screen::Dash || screen == Screen::More) pumpWheel(now);

  if (mappedInput.wasReleased(Button::Back)) {
    back();
    return;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y) && handleTap(x, y)) return;
  if (mappedInput.wasPressed(Button::Up) || mappedInput.wasPressed(Button::Left)) {
    moveFocus(-1);
    requestUpdate();
  }
  if (mappedInput.wasPressed(Button::Down) || mappedInput.wasPressed(Button::Right)) {
    moveFocus(1);
    requestUpdate();
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    if (screen == Screen::Warning) {
      activate(0);
    } else if (screen == Screen::Dash) {
      go(Screen::More);
    } else if (focus >= 0 && focus < rowCount) {
      activate(rows[focus].tag);
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

const char* VaultEucActivity::statusText() const {
  if (!active) return tr(STR_EUC_NO_RADIO);
  switch (active->state()) {
    case LinkState::Connecting:
      return tr(STR_EUC_CONNECTING);
    case LinkState::Connected:
      return gotTelemetry ? "" : tr(STR_EUC_WAITING);
    case LinkState::Lost:
    case LinkState::Failed:
      return tr(STR_EUC_RECONNECTING);
    default:
      return tr(STR_EUC_CONNECTING);
  }
}

void VaultEucActivity::drawWarning() const {
  const int width = renderer.getScreenWidth();
  int y = UITheme::getInstance().getMetrics().topPadding + UITheme::getInstance().getMetrics().headerHeight + 24;
  const char* lines[] = {tr(STR_EUC_WARN_1), tr(STR_EUC_WARN_2), tr(STR_EUC_WARN_3), tr(STR_EUC_WARN_4)};
  for (const char* line : lines) {
    const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, line, width - 40, 6);
    for (const auto& piece : wrapped) {
      renderer.drawText(UI_10_FONT_ID, 20, y, piece.c_str(), true);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    }
    y += 14;
  }
  const char* understood = tr(STR_EUC_UNDERSTOOD);
  const int w = renderer.getTextWidth(UI_12_FONT_ID, understood, EpdFontFamily::BOLD);
  renderer.fillRect(20, y + 10, width - 40, 64, true);
  renderer.drawText(UI_12_FONT_ID, (width - w) / 2, y + 10 + (64 - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                    understood, false, EpdFontFamily::BOLD);
}

void VaultEucActivity::drawList(const char* title, const char* note) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, title, nullptr);
  int y = metrics.topPadding + metrics.headerHeight + 8;
  if (note && note[0]) renderer.drawText(SMALL_FONT_ID, 16, y, note, true);
  y += 32;
  const int visible = visibleRows();
  for (int i = top; i < rowCount && i < top + visible; ++i) {
    const int rowY = y + (i - top) * ROW_H;
    if (i == focus) renderer.drawRect(6, rowY, width - 12, ROW_H - 6, 3, true);
    const int textY = rowY + (ROW_H - 6 - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, 18, textY, rows[i].label, true);
    if (rows[i].value[0]) {
      const int w = renderer.getTextWidth(UI_10_FONT_ID, rows[i].value, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, width - 20 - w, textY, rows[i].value, true, EpdFontFamily::BOLD);
    }
  }
}

// A tile: a small caption over a large value.
static void drawTile(const GfxRenderer& r, const int x, const int y, const int w, const int h, const char* caption,
                     const char* value, const bool alert) {
  if (alert) r.fillRect(x, y, w, h, true);
  r.drawRect(x, y, w, h, 2, true);
  r.drawText(SMALL_FONT_ID, x + 10, y + 6, caption, !alert);
  r.drawText(NOTOSANS_18_FONT_ID, x + 10, y + 6 + r.getLineHeight(SMALL_FONT_ID) + 2, value, !alert,
             EpdFontFamily::BOLD);
}

void VaultEucActivity::drawDash() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const char* nick =
      activePreset >= 0 && activePreset < presets.count ? presets.wheels[activePreset].nickname : target.name;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, nick, nullptr);
  const UnitSettings u = prefs.units;
  const Telemetry& t = shown;
  int y = metrics.topPadding + metrics.headerHeight + 10;

  const char* status = statusText();
  char line[64];
  if (status[0]) {
    renderer.drawText(UI_10_FONT_ID, 16, y, status, true, EpdFontFamily::BOLD);
  } else {
    std::snprintf(line, sizeof(line), "%s %u s%s", tr(STR_EUC_UPDATED), static_cast<unsigned>(prefs.refreshSeconds),
                  ride ? "   REC" : "");
    renderer.drawText(SMALL_FONT_ID, 16, y, line, true);
  }
  y += 34;

  const bool alarmOn = lastAlarm != euc::AlarmKind::None;
  if (alarmOn) {
    renderer.fillRect(8, y, width - 16, 50, true);
    const char* name = euc::alarmKindName(lastAlarm);
    const int w = renderer.getTextWidth(UI_12_FONT_ID, name, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (width - w) / 2, y + (50 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, name, false,
                      EpdFontFamily::BOLD);
    y += 58;
  }

  struct Tile {
    const char* caption;
    char value[24];
    bool show;
    bool alert;
  };
  Tile tiles[10] = {};
  int n = 0;
  const auto add = [&](const char* caption, const bool show, const bool alert = false) -> char* {
    tiles[n].caption = caption;
    tiles[n].show = show;
    tiles[n].alert = alert;
    return tiles[n++].value;
  };
  euc::formatSpeed(add(tr(STR_EUC_SPEED), t.has(euc::F_SPEED),
                       t.alarmBits & (euc::ALARM_SPEED1 | euc::ALARM_SPEED2 | euc::ALARM_SPEED3)),
                   24, t.speedKmh, u.speed, u.speed == euc::SpeedUnit::Mph ? 1 : 0);
  euc::formatPercent(add(tr(STR_EUC_BATTERY), t.has(euc::F_BATTERY)), 24, t.batteryPct);
  euc::formatVoltage(add(tr(STR_EUC_VOLTAGE), t.has(euc::F_VOLTAGE)), 24, t.voltage);
  euc::formatCurrent(add(tr(STR_EUC_CURRENT), t.has(euc::F_CURRENT)), 24, t.current);
  float tempC = 0;
  const bool hasTemp = euc::Alarms::maxTemp(t, tempC);
  euc::formatTemp(add(tr(STR_EUC_TEMP), hasTemp), 24, tempC, u.temp);
  euc::formatDistance(add(tr(STR_EUC_TRIP), t.has(euc::F_TRIP)), 24, t.tripKm, u.distance);
  euc::formatPercent(add(tr(STR_EUC_PWM), t.has(euc::F_PWM)), 24, t.pwm);
  euc::formatPower(add(tr(STR_EUC_POWER), t.has(euc::F_POWER)), 24, t.power);
  euc::formatDuration(add(tr(STR_EUC_TIME), t.has(euc::F_RIDE_TIME)), 24, t.rideSeconds);
  euc::formatSpeed(add(tr(STR_EUC_TOP), t.has(euc::F_TOP_SPEED)), 24,
                   stats.topSpeedKmh() > t.topSpeedKmh ? stats.topSpeedKmh() : t.topSpeedKmh, u.speed, 0);
  const int tileW = (width - 24) / 2;
  const int tileH = 92;
  int col = 0;
  int rowY = y;
  for (int i = 0; i < n; ++i) {
    if (!tiles[i].show) continue;
    drawTile(renderer, 8 + col * (tileW + 8), rowY, tileW, tileH, tiles[i].caption, tiles[i].value, tiles[i].alert);
    if (++col == 2) {
      col = 0;
      rowY += tileH + 8;
    }
  }
  // The model line.
  if (t.has(euc::F_MODEL) || t.has(euc::F_FIRMWARE)) {
    std::snprintf(line, sizeof(line), "%s %s", t.model, t.firmware);
    renderer.drawText(SMALL_FONT_ID, 16, renderer.getScreenHeight() - metrics.buttonHintsHeight - 120, line, true);
  }
  // Buttons.
  const int buttonTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - 80;
  renderer.drawRect(8, buttonTop, width / 2 - 12, 70, 3, true);
  renderer.drawRect(width / 2 + 4, buttonTop, width / 2 - 12, 70, 3, true);
  const char* more = tr(STR_EUC_MORE);
  const char* stop = tr(STR_EUC_STOP);
  renderer.drawText(UI_12_FONT_ID, (width / 2 - renderer.getTextWidth(UI_12_FONT_ID, more)) / 2,
                    buttonTop + (70 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, more, true);
  renderer.drawText(UI_12_FONT_ID, width / 2 + (width / 2 - renderer.getTextWidth(UI_12_FONT_ID, stop)) / 2,
                    buttonTop + (70 - renderer.getLineHeight(UI_12_FONT_ID)) / 2, stop, true);
}

void VaultEucActivity::drawRide() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_EUC_RIDE), nullptr);
  if (historyPick < 0 || historyPick >= historyCount) return;
  const HistoryEntry& e = history[historyPick];
  const UnitSettings u = prefs.units;
  const euc::DateTime d = euc::fromEpochSeconds(e.start);
  int y = metrics.topPadding + metrics.headerHeight + 24;
  char line[64];
  char value[24];
  const auto put = [&](const char* label, const char* text) {
    renderer.drawText(UI_10_FONT_ID, 20, y, label, true);
    const int w = renderer.getTextWidth(UI_10_FONT_ID, text, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, width - 20 - w, y, text, true, EpdFontFamily::BOLD);
    y += 46;
  };
  std::snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u", d.year, d.month, d.day, d.hour, d.minute);
  put(tr(STR_EUC_DATE), line);
  euc::formatDuration(value, sizeof(value), e.duration);
  put(tr(STR_EUC_TIME), value);
  euc::formatDistance(value, sizeof(value), e.km, u.distance);
  put(tr(STR_EUC_TRIP), value);
  euc::formatSpeed(value, sizeof(value), e.top, u.speed, 0);
  put(tr(STR_EUC_TOP), value);
  euc::formatSpeed(value, sizeof(value), e.avg, u.speed, 1);
  put(tr(STR_EUC_AVERAGE), value);
  euc::formatPercent(value, sizeof(value), e.batt);
  put(tr(STR_EUC_BATTERY_USED), value);
  euc::formatVoltage(value, sizeof(value), e.minV);
  put(tr(STR_EUC_MIN_VOLTAGE), value);
  renderer.drawText(SMALL_FONT_ID, 20, y + 10, e.file, true);
}

void VaultEucActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  switch (screen) {
    case Screen::Warning:
      GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_EUC_TITLE), nullptr);
      drawWarning();
      break;
    case Screen::Dash:
      drawDash();
      break;
    case Screen::Ride:
      drawRide();
      break;
    case Screen::Wheels:
      drawList(tr(STR_EUC_TITLE), wifiWasOn ? tr(STR_EUC_WIFI_OFF) : "");
      break;
    case Screen::Remove:
      drawList(tr(STR_EUC_REMOVE_WHEEL), tr(STR_EUC_TAP_REMOVE));
      break;
    case Screen::Scan:
      drawList(tr(STR_EUC_ADD_WHEEL), radio || fake ? tr(STR_EUC_SCANNING) : tr(STR_EUC_NO_RADIO));
      break;
    case Screen::Maker:
      drawList(tr(STR_EUC_MAKER), "");
      break;
    case Screen::Model:
      drawList(pickedMaker, "");
      break;
    case Screen::More:
      drawList(tr(STR_EUC_MORE), "");
      break;
    case Screen::Rides:
      drawList(tr(STR_EUC_RIDES), rowCount == 0 ? tr(STR_EUC_NO_RIDES) : "");
      break;
    case Screen::Settings:
      drawList(tr(STR_EUC_SETTINGS), "");
      break;
    case Screen::Alarms:
      drawList(tr(STR_EUC_ALARMS), tr(STR_EUC_ALARM_NOTE));
      break;
  }
  const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "<", ">");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  // E-ink: a fast refresh most of the time, a half refresh now and then to clear ghosting.
  const bool clean = screen == Screen::Dash && (++renders % 8 == 0);
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
