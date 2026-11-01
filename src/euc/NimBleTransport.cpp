#include "EucTransport.h"

#if defined(ESP_PLATFORM)

#include <Arduino.h>
#include <Logging.h>
#include <NimBLEDevice.h>

#include <atomic>
#include <cstring>
#include <new>
#include <string>

// The real radio for VaultEUC: NimBLE as a GATT client for one wheel. The page turner's radio (bleturner) is off
// whenever the screen is not a book or the Home, so this owns the stack while VaultEUC is open; it brings it up
// in begin() and takes it all the way down in end().
//
// Threads: NimBLE calls the scan, notify and disconnect callbacks on its host task. They only touch the
// fixed ring (single producer, single consumer, atomics) and the scan table (a spinlock); everything else runs
// on the activity's loop task. The connect itself blocks for seconds, so it runs on its own small task.
namespace euc {
namespace {

constexpr size_t kRingBytes = 2048;
constexpr int kMaxScan = 12;
constexpr uint32_t kConnectTimeoutMs = 10000;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

class NimBleTransport;
NimBleTransport* g_self = nullptr;

class NimBleTransport final : public Transport, public NimBLEScanCallbacks, public NimBLEClientCallbacks {
 public:
  NimBleTransport() { g_self = this; }
  ~NimBleTransport() override {
    end();
    g_self = nullptr;
  }

  bool begin() override {
    if (up_) return true;
    if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);  // a stack left over from another feature
    if (!NimBLEDevice::init("VaultEUC")) {
      setError("Bluetooth did not start");
      return false;
    }
    NimBLEDevice::setMTU(23);  // 20-byte payloads like the reference apps: the decoders split frames on that size
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(this, true);
    scan->setMaxResults(0);
    scan->setActiveScan(true);
    scan->setInterval(160);
    scan->setWindow(120);
    client_ = NimBLEDevice::createClient();
    if (!client_) {
      NimBLEDevice::deinit(true);
      setError("Bluetooth client unavailable");
      return false;
    }
    client_->setConnectTimeout(kConnectTimeoutMs);
    client_->setConnectionParams(12, 24, 0, 800);
    client_->setClientCallbacks(this, false);
    xTaskCreate(&NimBleTransport::connectTask, "euc-conn", 4096, this, 3, &task_);
    up_ = true;
    state_.store(static_cast<uint8_t>(LinkState::Idle));
    return true;
  }

  void end() override {
    if (!up_) return;
    up_ = false;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan && scan->isScanning()) scan->stop();
    cancel_.store(true);
    // Let a connect in flight finish or fail before the stack goes away.
    // The connect timeout is 10 s: wait past it, and never kill the task while it is inside NimBLE.
    for (int i = 0; i < 300 && connecting_.load(); ++i) {
      if (client_) client_->cancelConnect();
      vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (connecting_.load()) {
      LOG_ERR("EUC", "connect still running at teardown: the stack is left up");
      return;
    }
    if (task_) {
      vTaskDelete(task_);
      task_ = nullptr;
    }
    if (client_) {
      // A delete while the link is up only defers, and deinit would then leak the client slot: wait for the link
      // to go (the supervision timeout is 8 s).
      for (int i = 0; i < 240 && client_->isConnected(); ++i) {
        client_->disconnect();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      if (client_->isConnected()) {
        LOG_ERR("EUC", "link still up at teardown: the stack is left up");
        return;
      }
      NimBLEDevice::deleteClient(client_);
      client_ = nullptr;
    }
    NimBLEDevice::deinit(true);
    notifyChar_ = nullptr;
    writeChar_ = nullptr;
    state_.store(static_cast<uint8_t>(LinkState::Off));
    cancel_.store(false);
  }

  void startScan() override {
    if (!up_) return;
    portENTER_CRITICAL(&g_mux);
    scanCount_ = 0;
    portEXIT_CRITICAL(&g_mux);
    NimBLEDevice::getScan()->start(0, false);  // until stopScan(): the activity decides
    state_.store(static_cast<uint8_t>(LinkState::Scanning));
  }
  void stopScan() override {
    if (!up_) return;
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan->isScanning()) scan->stop();
    if (state() == LinkState::Scanning) state_.store(static_cast<uint8_t>(LinkState::Idle));
  }
  int scanCount() const override {
    portENTER_CRITICAL(&g_mux);
    const int n = scanCount_;
    portEXIT_CRITICAL(&g_mux);
    return n;
  }
  bool scanAt(const int index, ScanEntry& out) const override {
    bool ok = false;
    portENTER_CRITICAL(&g_mux);
    if (index >= 0 && index < scanCount_) {
      out = scan_[index];
      ok = true;
    }
    portEXIT_CRITICAL(&g_mux);
    return ok;
  }

  void connect(const ScanEntry& wheel, const GattProfile& profile, const Brand brand) override {
    if (!up_ || connecting_.load()) return;
    stopScan();
    target_ = wheel;
    profile_ = profile;
    brand_ = brand;
    head_.store(0);
    tail_.store(0);
    err_[0] = '\0';
    cancel_.store(false);
    connecting_.store(true);
    state_.store(static_cast<uint8_t>(LinkState::Connecting));
    xTaskNotifyGive(task_);
  }
  void disconnect() override {
    cancel_.store(true);
    if (client_) {
      client_->cancelConnect();
      if (client_->isConnected()) client_->disconnect();
    }
    if (state() != LinkState::Off) state_.store(static_cast<uint8_t>(LinkState::Idle));
  }

  LinkState state() const override { return static_cast<LinkState>(state_.load()); }
  const char* lastError() const override { return err_; }

  bool write(const uint8_t* data, const size_t size) override {
    if (state() != LinkState::Connected || !writeChar_) return false;
    return writeChar_->writeValue(data, size, profile_.writeWithResponse);
  }

  // One notification per call (a length byte, then the bytes): the protocols rely on the boundaries.
  size_t read(uint8_t* out, const size_t cap) override {
    size_t tail = tail_.load(std::memory_order_acquire);
    const size_t head = head_.load(std::memory_order_acquire);
    if (tail == head) return 0;
    const size_t length = ring_[tail];
    tail = (tail + 1) % kRingBytes;
    size_t n = 0;
    for (size_t i = 0; i < length; ++i) {
      if (n < cap) out[n++] = ring_[tail];
      tail = (tail + 1) % kRingBytes;
    }
    tail_.store(tail, std::memory_order_release);
    return n;
  }

  void tick(uint32_t) override {}

  // --- NimBLE callbacks (host task) ---
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (!dev) return;
    ScanEntry entry;
    const std::string addr = dev->getAddress().toString();
    std::snprintf(entry.addr, sizeof(entry.addr), "%s", addr.c_str());
    entry.addrType = dev->getAddress().getType();
    entry.rssi = static_cast<int8_t>(dev->getRSSI());
    if (dev->haveName()) std::snprintf(entry.name, sizeof(entry.name), "%s", dev->getName().c_str());
    if (dev->haveServiceUUID()) {
      std::snprintf(entry.service, sizeof(entry.service), "%s", dev->getServiceUUID(0).toString().c_str());
    }
    if (entry.name[0] == '\0') return;  // wheels advertise a name; anonymous devices are noise
    portENTER_CRITICAL(&g_mux);
    int slot = -1;
    for (int i = 0; i < scanCount_; ++i) {
      if (std::strcmp(scan_[i].addr, entry.addr) == 0) slot = i;
    }
    if (slot < 0 && scanCount_ < kMaxScan) slot = scanCount_++;
    if (slot >= 0) scan_[slot] = entry;
    portEXIT_CRITICAL(&g_mux);
  }

  void onDisconnect(NimBLEClient*, int reason) override {
    // An established link that drops is Lost (the activity reconnects); a failed attempt is reported by the task.
    if (state() == LinkState::Connected) {
      std::snprintf(err_, sizeof(err_), "Link lost (%d)", reason);
      state_.store(static_cast<uint8_t>(LinkState::Lost));
    }
  }
  bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*) override { return false; }

 private:
  static void connectTask(void* arg) {
    auto* self = static_cast<NimBleTransport*>(arg);
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      self->doConnect();
      self->connecting_.store(false);
    }
  }

  static void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
    NimBleTransport* self = g_self;
    if (!self) return;
    if (length == 0 || length > 255) return;
    size_t head = self->head_.load(std::memory_order_relaxed);
    const size_t tail = self->tail_.load(std::memory_order_acquire);
    const size_t used = (head + kRingBytes - tail) % kRingBytes;
    if (used + length + 1 >= kRingBytes) return;  // full: the activity is behind; drop it like a radio would
    self->ring_[head] = static_cast<uint8_t>(length);
    head = (head + 1) % kRingBytes;
    for (size_t i = 0; i < length; ++i) {
      self->ring_[head] = data[i];
      head = (head + 1) % kRingBytes;
    }
    self->head_.store(head, std::memory_order_release);
  }

  void fail(const char* message) {
    setError(message);
    if (client_ && client_->isConnected()) client_->disconnect();
    state_.store(static_cast<uint8_t>(LinkState::Failed));
  }

  void doConnect() {
    NimBLEAddress addr(std::string(target_.addr), target_.addrType);
    if (!client_->connect(addr)) {
      if (cancel_.load()) return;
      fail(client_->getLastError() == BLE_HS_ETIMEOUT ? "Connect timeout" : "Connection failed");
      return;
    }
    NimBLERemoteService* notifyService = client_->getService(NimBLEUUID(profile_.notifyService));
    NimBLERemoteService* writeService = std::strcmp(profile_.writeService, profile_.notifyService) == 0
                                            ? notifyService
                                            : client_->getService(NimBLEUUID(profile_.writeService));
    if (!notifyService || !writeService) {
      fail("Not the expected wheel (no service)");
      return;
    }
    notifyChar_ = notifyService->getCharacteristic(NimBLEUUID(profile_.notifyChar));
    writeChar_ = writeService->getCharacteristic(NimBLEUUID(profile_.writeChar));
    if (!notifyChar_ || !writeChar_) {
      fail("Not the expected wheel (no characteristic)");
      return;
    }
    if (!notifyChar_->canNotify() || !notifyChar_->subscribe(true, &NimBleTransport::onNotify)) {
      fail("Could not turn notifications on");
      return;
    }
    if (cancel_.load()) {
      client_->disconnect();
      return;
    }
    state_.store(static_cast<uint8_t>(LinkState::Connected));
  }

  void setError(const char* message) { std::snprintf(err_, sizeof(err_), "%s", message); }

  NimBLEClient* client_ = nullptr;
  TaskHandle_t task_ = nullptr;
  NimBLERemoteCharacteristic* notifyChar_ = nullptr;
  NimBLERemoteCharacteristic* writeChar_ = nullptr;
  GattProfile profile_{};
  ScanEntry target_{};
  Brand brand_ = Brand::Begode;
  bool up_ = false;
  std::atomic<uint8_t> state_{static_cast<uint8_t>(LinkState::Off)};
  std::atomic<bool> connecting_{false};
  std::atomic<bool> cancel_{false};
  char err_[48] = {};
  uint8_t ring_[kRingBytes] = {};
  std::atomic<size_t> head_{0};
  std::atomic<size_t> tail_{0};
  ScanEntry scan_[kMaxScan];
  int scanCount_ = 0;
};

}  // namespace

Transport* makeNimBleTransport() { return new (std::nothrow) NimBleTransport(); }

}  // namespace euc

#else

#include "EucTransport.h"

namespace euc {
Transport* makeNimBleTransport() { return nullptr; }
}  // namespace euc

#endif
