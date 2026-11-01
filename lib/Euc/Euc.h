#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

// VaultEUC core contracts (0.62.0): what every wheel protocol implements and what the rest of the firmware
// sees. Platform-free (no Arduino/ESP/NimBLE headers, no exceptions, no iostream): the BLE client and the
// activities live in src/. Protocols are decoders and command encoders; they never touch a radio or the SD card.
//
// Safety rule for everything in here: only commands from the safe list below exist. No firmware update, factory
// reset, calibration, power-off, tiltback-off, PWM limit, lock or transport mode, ever.
namespace euc {

enum class Brand : uint8_t {
  Begode,
  Veteran,
  Kingsong,
  InmotionV1,
  InmotionV2,
  NinebotS,  // One S1/S2/A1: Nordic UART, register reads
  NinebotE,  // One E+/C/P: FFE0, register reads
  NinebotZ,  // One Z6/Z8/Z10: Nordic UART, read only
  Vesc,
  Count
};

// The GATT layout a brand talks on (128-bit UUIDs as lower-case strings).
struct GattProfile {
  const char* notifyService;
  const char* notifyChar;
  const char* writeService;  // same as notifyService for most
  const char* writeChar;
  bool writeWithResponse;  // false = write without response
  const char* scanName;    // a name prefix to look for, or "" (the brand is found by service)
};

// Telemetry fields; Telemetry::valid has one bit each so the UI shows only what the wheel reported.
enum Field : uint32_t {
  F_VOLTAGE = 1u << 0,
  F_CURRENT = 1u << 1,
  F_POWER = 1u << 2,
  F_SPEED = 1u << 3,
  F_PWM = 1u << 4,
  F_BATTERY = 1u << 5,  // percent, from the wheel or from a voltage curve
  F_TEMP_MOS = 1u << 6,
  F_TEMP_MOTOR = 1u << 7,
  F_TEMP_BATTERY = 1u << 8,
  F_TEMP_BOARD = 1u << 9,
  F_PITCH = 1u << 10,
  F_ROLL = 1u << 11,
  F_TRIP = 1u << 12,
  F_TOTAL = 1u << 13,
  F_RIDE_TIME = 1u << 14,
  F_TOP_SPEED = 1u << 15,
  F_MODEL = 1u << 16,
  F_FIRMWARE = 1u << 17,
  F_SERIAL = 1u << 18,
  F_LIGHT = 1u << 19,
  F_CHARGING = 1u << 20,
  F_RANGE = 1u << 21,
  F_ALARMS = 1u << 22,  // alarmBits
  F_FAULT = 1u << 23,   // faultCode
  F_RIDE_MODE = 1u << 24,
};

// What the wheel itself flags (not the watch-side thresholds in Alarms.h).
enum AlarmBit : uint16_t {
  ALARM_SPEED1 = 1 << 0,
  ALARM_SPEED2 = 1 << 1,
  ALARM_SPEED3 = 1 << 2,
  ALARM_HIGH_POWER = 1 << 3,
  ALARM_LOW_VOLTAGE = 1 << 4,
  ALARM_OVER_VOLTAGE = 1 << 5,
  ALARM_HIGH_TEMP = 1 << 6,
  ALARM_HALL_ERROR = 1 << 7,
  ALARM_TRANSPORT = 1 << 8,
  ALARM_OVER_CURRENT = 1 << 9,
  ALARM_MOTOR_BLOCKED = 1 << 10,
  ALARM_LIFTED = 1 << 11,
  ALARM_TILTBACK = 1 << 12,
};

struct Telemetry {
  uint32_t valid = 0;
  float voltage = 0;     // V
  float current = 0;     // A (negative = regeneration)
  float power = 0;       // W
  float speedKmh = 0;    // always km/h, the UI converts
  float pwm = 0;         // percent 0..100+
  float batteryPct = 0;  // 0..100
  float tempMos = 0;     // degrees C
  float tempMotor = 0;
  float tempBattery = 0;
  float tempBoard = 0;
  float pitch = 0;  // degrees
  float roll = 0;
  float tripKm = 0;
  float totalKm = 0;
  float topSpeedKmh = 0;
  float rangeKm = 0;
  uint32_t rideSeconds = 0;
  uint16_t alarmBits = 0;
  uint16_t faultCode = 0;
  uint8_t rideMode = 0;
  uint8_t lightMode = 0;  // 0 off, 1 on, 2 auto, 3 strobe
  bool charging = false;
  char model[24] = {};
  char firmware[16] = {};
  char serial[24] = {};

  bool has(Field f) const { return (valid & f) != 0; }
  void mark(Field f) { valid |= f; }
};

// The safe command set. Value meaning is per command; a protocol returns false from supports() for the ones it
// cannot do, and send() refuses anything else.
enum class Command : uint8_t {
  Horn,       // a short sound
  Beep,       // the wheel's beep
  Lights,     // value 0 off, 1 on, 2 auto, 3 strobe
  RideLeds,   // value 0 off, 1 on
  PedalMode,  // value 0 soft, 1 medium, 2 hard (only while stopped)
  Volume,     // value 0..100 (or the wheel's own steps)
  ResetTrip,
  Miles,        // value 0 km, 1 miles on the wheel's own display (only while stopped)
  RequestInfo,  // ask for model, firmware, serial again
  Count
};

// True for the few commands allowed while moving; everything else is blocked above 2 km/h (WheelDash's rule).
constexpr bool allowedWhileMoving(Command c) {
  return c == Command::Horn || c == Command::Beep || c == Command::Lights || c == Command::RideLeds ||
         c == Command::Volume || c == Command::RequestInfo;
}

// Where a protocol writes. The BLE client and the test/fake transports implement it.
class Link {
 public:
  virtual ~Link() = default;
  // One GATT write of at most 20 bytes (the protocol chunks longer frames). False on a radio error.
  virtual bool write(const uint8_t* data, size_t size) = 0;
};

// A decoder and command encoder for one brand. One instance per connection; not thread safe (the activity calls
// it from one task).
class Protocol {
 public:
  virtual ~Protocol() = default;
  virtual Brand brand() const = 0;
  virtual const GattProfile& profile() const = 0;
  virtual const char* name() const = 0;

  // Called once the notifications are on: sends the init requests (model, firmware, serial, polling start).
  virtual void start(Link& link, uint32_t nowMs) = 0;
  // Bytes from a notification, any chunking; updates `t` when a whole frame is decoded. Returns true when `t`
  // changed.
  virtual bool feed(const uint8_t* data, size_t size, Telemetry& t) = 0;
  // Called often (every loop) for the protocols the wheel answers only when asked (InMotion, Ninebot, VESC):
  // sends the next request when it is due.
  virtual void poll(Link& link, uint32_t nowMs) = 0;
  // False when this brand cannot do the command.
  virtual bool supports(Command c) const = 0;
  // Encodes and writes the command. Refuses (false) an unsupported command or an out of range value.
  virtual bool send(Command c, int value, Link& link) = 0;
};

// What a fake wheel does: replies to writes and produces notification frames from a Telemetry, in the brand's own
// framing, so the whole stack (BLE client -> protocol -> screens -> ride log) can be run without a wheel.
class WheelSimulator {
 public:
  virtual ~WheelSimulator() = default;
  virtual Brand brand() const = 0;
  // The app wrote these bytes to the wheel; any reply frames are appended with emit().
  virtual void onWrite(const uint8_t* data, size_t size) = 0;
  // Frames the wheel sends by itself at its own rate, for the current `t` (called about every 100 ms). Each frame
  // is delivered as one or more notifications of at most 20 bytes by the transport.
  virtual void tick(uint32_t nowMs, const Telemetry& t) = 0;
  // Takes the next whole frame the wheel wants to send; false when none is pending.
  virtual bool pop(uint8_t* out, size_t cap, size_t& size) = 0;
};

std::unique_ptr<Protocol> makeProtocol(Brand brand);
std::unique_ptr<WheelSimulator> makeSimulator(Brand brand);

const char* brandName(Brand brand);

}  // namespace euc
