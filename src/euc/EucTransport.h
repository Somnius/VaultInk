#pragma once

#include <Euc.h>

#include <cstddef>
#include <cstdint>

// What VaultEUC needs from a radio: scan, connect to one wheel, deliver its notifications, write to it. The real
// one is NimBleTransport (the ESP32 build); FakeTransport replays a simulated wheel (WheelSimulator) so the whole
// stack runs in the emulator and, as a demo wheel, on the device too.
namespace euc {

enum class LinkState : uint8_t {
  Off,   // the stack is not up
  Idle,  // up, nothing going on
  Scanning,
  Connecting,  // connecting, discovering, subscribing
  Connected,
  Lost,    // an established link dropped (reconnect from the activity)
  Failed,  // the connect attempt failed (lastError says why)
};

struct ScanEntry {
  char name[32] = {};
  char addr[18] = {};    // "AA:BB:CC:DD:EE:FF"
  uint8_t addrType = 0;  // BLE address type (0 public, 1 random)
  int8_t rssi = 0;
  char service[40] = {};  // the first advertised service UUID worth knowing, lower case, or ""
  bool demo = false;      // the fake wheel
  Brand demoBrand = Brand::Begode;
};

class Transport : public Link {
 public:
  ~Transport() override = default;

  // Brings the stack up / down. begin() returns false when the stack could not start (message in lastError()).
  virtual bool begin() = 0;
  virtual void end() = 0;

  virtual void startScan() = 0;
  virtual void stopScan() = 0;
  virtual int scanCount() const = 0;
  virtual bool scanAt(int index, ScanEntry& out) const = 0;

  // Starts connecting (asynchronous: watch state()). The profile says which service and characteristics to use.
  virtual void connect(const ScanEntry& wheel, const GattProfile& profile, Brand brand) = 0;
  virtual void disconnect() = 0;

  virtual LinkState state() const = 0;
  virtual const char* lastError() const = 0;

  // The next received notification (its bytes, not merged with the next one) into `out`; returns its size, 0 when
  // none is waiting. Main loop.
  virtual size_t read(uint8_t* out, size_t cap) = 0;
  // Once per loop pass.
  virtual void tick(uint32_t nowMs) = 0;
};

// The fake wheel (always built): one demo wheel per brand shows up in the scan list.
Transport* makeFakeTransport();
// The radio (device builds only; null in the simulator).
Transport* makeNimBleTransport();

}  // namespace euc
