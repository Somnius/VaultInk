#include "HalFrontlight.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <driver/gpio.h>

HalFrontlight HalFrontlight::instance;

namespace {
// The light's pads when both are known ESP GPIOs driven by LEDC and LOW means off: the X4 Pro only. The other
// boards of this firmware have an I2C LED driver or a PMIC PWM and get false.
bool pwmPads(int8_t pads[2]) {
#if FREEINK_DEVICE_X4PRO
  const auto& fl = BoardConfig::ACTIVE.frontlight;
  if (BoardConfig::ACTIVE.board != BoardConfig::Board::XteinkX4Pro) return false;
  if (fl.viaPm1Pwm || !fl.activeHigh || fl.gpio < 0 || fl.gpioWarm < 0) return false;
  pads[0] = fl.gpio;
  pads[1] = fl.gpioWarm;
  return true;
#else
  (void)pads;
  return false;
#endif
}
}  // namespace

bool HalFrontlight::parkForDeepSleep() {
  int8_t pads[2];
  if (!pwmPads(pads)) return false;
  if (manager.present()) {
    lit = false;
    manager.setBrightness(0);
  }
  for (const int8_t pin : pads) {
    const auto g = static_cast<gpio_num_t>(pin);
    // A held pad ignores the drive below, so release a hold that is still there first.
    gpio_hold_dis(g);
    // Takes the pad away from LEDC (and stops the channel) when it is attached; pinMode then owns it.
    if (manager.present()) ledcDetach(static_cast<uint8_t>(pin));
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    gpio_hold_en(g);
  }
  return true;
}

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
  // begin() runs the hardware probe (EEGO A4: an I2C ACK from the LM3630A at
  // 0x36) that decides present(), so it must come FIRST. Guarding on present()
  // before begin() would skip the probe forever and hide the light on units
  // that actually have one. begin() is inert on boards without a frontlight.
  // parkForDeepSleep() held the PWM pads LOW and that hold is still there after the wake, when nothing in RAM
  // remembers it; a held pad ignores the PWM, so release always (a no-op on a pad that is not held).
  int8_t pads[2];
  if (pwmPads(pads)) {
    for (const int8_t pin : pads) gpio_hold_dis(static_cast<gpio_num_t>(pin));
  }
  manager.begin();
  if (!manager.present()) return;

  lastBrightness = brightness > 100 ? 100 : brightness;
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);
  lit = on;
  everLit = everLit || lit;
  manager.setBrightness(lit ? lastBrightness : 0);
  LOG_INF("LIGHT", "Frontlight up: %u%% warm=%u%% %s", lastBrightness, manager.colorTemperature(), lit ? "on" : "off");
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) manager.setBrightness(lastBrightness);
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
}

void HalFrontlight::setOn(const bool on) {
  if (on == lit) return;
  lit = on;
  everLit = everLit || lit;
  manager.setBrightness(lit ? lastBrightness : 0);
}
