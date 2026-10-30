#include "BatteryActivity.h"

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <I18n.h>
#ifndef SIMULATOR
#include <Wire.h>
#endif

#include <cstdio>

#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

constexpr unsigned long REFRESH_MS = 10000;

// CW2017 TEMP register (0x06): -40 °C + value / 2 (datasheet V1.0). Tenths of a degree, or false.
bool readCw2017Temperature(int& tenths) {
#ifdef SIMULATOR
  (void)tenths;
  return false;  // no I2C gauge in the simulator
#else
  const auto& gauge = BoardConfig::ACTIVE.batteryGauge;
  if (gauge.gaugeAddr == 0 || gauge.gaugeType != BoardConfig::GaugeType::Cw2017) return false;
  TwoWire& wire = gauge.i2cBus == 1 ? Wire1 : Wire;
  wire.beginTransmission(gauge.gaugeAddr);
  wire.write(static_cast<uint8_t>(0x06));
  if (wire.endTransmission(false) != 0) return false;
  if (wire.requestFrom(gauge.gaugeAddr, static_cast<uint8_t>(1), static_cast<uint8_t>(true)) < 1) return false;
  const int value = wire.read();
  tenths = -400 + value * 5;
  // A battery without its thermistor reads at one end of the scale; don't show that.
  return tenths > -200 && tenths < 700;
#endif
}

}  // namespace

const char* BatteryActivity::headerTitle() const { return tr(STR_BATTERY); }

void BatteryActivity::onEnter() {
  UiListActivity::onEnter();
  readBattery();
  requestUpdate();
}

void BatteryActivity::loop() {
  if (millis() - lastRead >= REFRESH_MS) {
    readBattery();
    requestUpdate();
  }
  UiListActivity::loop();
}

void BatteryActivity::readBattery() {
  lastRead = millis();
  char buffer[24];
  rowCount = 0;
  const auto add = [this](const StrId label, const std::string& value) {
    if (rowCount >= MAX_ROWS) return;
    values[rowCount] = value;
    rows[rowCount] = fui::ListItem{};
    rows[rowCount].label = I18N.get(label);
    rows[rowCount].value = values[rowCount].c_str();
    rows[rowCount].actionValue = static_cast<int16_t>(rowCount);
    ++rowCount;
  };
  snprintf(buffer, sizeof(buffer), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  add(StrId::STR_BATTERY_CHARGE, buffer);
#ifndef SIMULATOR
  const BatteryMonitor battery;  // also sets up the gauge's I2C bus before the temperature read
  const uint16_t mv = battery.readMillivolts();
  snprintf(buffer, sizeof(buffer), "%u.%02u V", static_cast<unsigned>(mv / 1000),
           static_cast<unsigned>(mv % 1000 / 10));
  add(StrId::STR_BATTERY_VOLTAGE, buffer);
  add(StrId::STR_BATTERY_CHARGING, battery.isCharging() ? tr(STR_YES) : tr(STR_NO));
#endif
  add(StrId::STR_BATTERY_CHARGER, gpio.isUsbConnected() ? tr(STR_YES) : tr(STR_NO));
  int tenths = 0;
  if (readCw2017Temperature(tenths)) {
    snprintf(buffer, sizeof(buffer), "%d.%d °C", tenths / 10, (tenths < 0 ? -tenths : tenths) % 10);
    add(StrId::STR_BATTERY_TEMPERATURE, buffer);
  }
}

void BatteryActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}
