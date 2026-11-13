#pragma once

#if defined(VAULTINK_SSH) && VAULTINK_SSH && !defined(SIMULATOR)

#include <Arduino.h>
#include <SshSession.h>
#include <WiFiClient.h>

#include <string>

// The bytes of an SSH session over Wi-Fi: a plain TCP connection (wolfSSH does the encryption).
class SshWifiSocket final : public sshclient::Socket {
 public:
  bool connect(const std::string& host, const uint16_t port, const uint32_t timeoutMs) {
    client.stop();
    if (client.connect(host.c_str(), port, static_cast<int32_t>(timeoutMs)) == 0) return false;
    client.setNoDelay(true);  // typed characters go out at once
    return true;
  }
  void stop() { client.stop(); }
  bool hasData() { return client.available() > 0; }

  int read(uint8_t* buf, const size_t len) override {
    if (client.available() > 0) {
      const int n = client.read(buf, len);
      return n > 0 ? n : 0;
    }
    return client.connected() ? 0 : -1;
  }
  int write(const uint8_t* buf, const size_t len) override {
    if (!client.connected()) return -1;
    return static_cast<int>(client.write(buf, len));
  }
  uint32_t nowMs() override { return millis(); }
  void idle() override { delay(2); }

 private:
  WiFiClient client;
};

#endif
