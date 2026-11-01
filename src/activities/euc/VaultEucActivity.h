#pragma once

#include <Alarms.h>
#include <Euc.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Presets.h>
#include <RideLog.h>
#include <RideStats.h>
#include <Units.h>

#include <memory>

#include "activities/Activity.h"
#include "euc/EucTransport.h"

class MappedInputManager;

// VaultEUC (0.62.0): an electric unicycle companion, built in next to Dice and Sudoku in Home > Plugins. It connects
// over Bluetooth LE to a wheel (Begode, Veteran, KingSong, InMotion, Ninebot, VESC), shows its numbers on the slow
// e-ink screen every 5, 10, 15 or 30 seconds, watches the alarm thresholds on every update, logs each ride to
// /VaultEUC on the SD card and keeps a list of your wheels. A demo wheel (the fake transport) runs the whole stack
// without hardware. It is a glance aid, never to be used while riding; it says so at every start, and settings that
// change how the wheel rides are only sent when it is stopped.
//
// Bluetooth is the priority: the page turner's radio is off outside books and the Home, and Wi-Fi is turned off
// while VaultEUC is open (it is said on the first screen).
class VaultEucActivity final : public Activity {
 public:
  explicit VaultEucActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("VaultEuc", renderer, mappedInput) {}
  ~VaultEucActivity() override;

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  bool preventAutoSleep() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen : uint8_t {
    Warning,
    Wheels,
    Remove,
    Scan,
    Maker,
    Model,
    Dash,
    More,
    Rides,
    Ride,
    Settings,
    Alarms
  };

  struct Row {
    char label[48] = {};
    char value[32] = {};
    int tag = 0;
  };
  static constexpr int MAX_ROWS = 40;

  struct Prefs {
    euc::UnitSettings units;
    uint8_t refreshSeconds = 10;
    bool keepAwake = false;
    bool vibrate = false;
    bool demoWheels = false;
    euc::AlarmThresholds alarms;
  };

  // Screens
  void go(Screen screen);
  void buildRows();
  void addRow(const char* label, const char* value, int tag);
  void activate(int tag);
  void back();
  void moveFocus(int delta);
  int listTop() const;
  int visibleRows() const;
  bool handleTap(int x, int y);
  void drawList(const char* title, const char* note) const;
  void drawDash() const;
  void drawWarning() const;
  void drawRide() const;

  // Storage
  void loadFiles();
  void savePresets() const;
  void savePrefs() const;
  void appendRideIndex() const;
  void loadRideIndex();

  // Radio and wheel
  void startRadio();
  void stopRadio();
  void startScan();
  void connectTo(const euc::WheelPreset& preset);
  void connectEntry(const euc::ScanEntry& entry, euc::Brand brand);
  void disconnectWheel(bool saveRide);
  void pumpWheel(uint32_t nowMs);
  void onTelemetry(uint32_t nowMs);
  void openRideLog();
  void writeRideLine(uint32_t nowMs);
  void closeRideLog();
  void sendCommand(euc::Command command, int value);
  void finishAdding(const char* nickname);
  void cycleThreshold(int tag);
  euc::Transport* transportFor(const euc::ScanEntry& entry) const;
  const char* statusText() const;

  Screen screen = Screen::Warning;
  Row rows[MAX_ROWS];
  int rowCount = 0;
  int focus = 0;
  int top = 0;
  bool wifiWasOn = false;
  bool warnedNoRadio = false;

  Prefs prefs;
  euc::PresetFile presets;
  int activePreset = -1;  // index in presets while a wheel is open

  std::unique_ptr<euc::Transport> radio;
  std::unique_ptr<euc::Transport> fake;
  euc::Transport* active = nullptr;  // the one the wheel is on
  std::unique_ptr<euc::Protocol> protocol;
  euc::ScanEntry target;
  euc::Brand targetBrand = euc::Brand::Begode;
  euc::ScanEntry scanned[16];
  int scannedCount = 0;
  uint32_t lastScanPoll = 0;
  euc::ScanEntry picked;  // the scan result being added
  char pickedMaker[16] = {};
  char makers[16][16] = {};
  int makerCount = 0;

  euc::Telemetry live;
  euc::Telemetry shown;
  euc::Alarms alarms;
  euc::RideStats stats;
  euc::AlarmKind lastAlarm = euc::AlarmKind::None;
  uint32_t lastAlarmDraw = 0;
  uint32_t lastDraw = 0;
  uint32_t lastFrameMs = 0;
  uint32_t connectedAt = 0;
  uint32_t reconnectAt = 0;
  int reconnectTries = 0;
  bool linkWanted = false;
  bool gotTelemetry = false;
  uint32_t renders = 0;

  // Ride log
  struct RideFile {
    HalFile file;
    int lines = 0;
  };
  std::unique_ptr<RideFile> ride;
  uint32_t lastLogMs = 0;
  int64_t rideStartEpoch = 0;
  char rideFileName[euc::ridelog::kMaxFileName] = {};
  char rideNick[24] = {};

  // History
  struct HistoryEntry {
    char file[64] = {};
    int64_t start = 0;
    uint32_t duration = 0;
    float km = 0, top = 0, avg = 0, batt = 0, minV = 0;
  };
  static constexpr int MAX_HISTORY = 24;
  HistoryEntry history[MAX_HISTORY];
  int historyCount = 0;
  int historyPick = -1;
};
