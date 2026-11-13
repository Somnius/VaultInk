#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

/**
 * The device's SSH public key: as a QR code and as text (the line for ~/.ssh/authorized_keys on
 * the server), with its fingerprint. Only the public half is ever shown; the private half stays
 * sealed in /.vaultink/ssh/ (docs/ssh.md). Confirm makes a new key, after a question.
 */
class SshKeyActivity final : public Activity {
 public:
  explicit SshKeyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SshKey", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string line_;         // "ssh-ed25519 AAAA... vaultink"
  std::string fingerprint_;  // "SHA256:..."
  bool failed_ = false;
  int newKeyTop_ = 0;  // the "Make a new key" touch target (a band across the screen)
  int newKeyBottom_ = 0;

  void load();
  void askNewKey();
};
