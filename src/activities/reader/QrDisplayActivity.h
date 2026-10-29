#pragma once
#include <I18n.h>

#include <string>

#include "activities/Activity.h"

class QrDisplayActivity final : public Activity {
 public:
  // `title` replaces the "Display QR" header; `caption` (a few lines) goes under the code.
  explicit QrDisplayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& textPayload,
                             const char* title = nullptr, const char* caption = nullptr)
      : Activity("QrDisplay", renderer, mappedInput), textPayload(textPayload), title(title), caption(caption) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string textPayload;
  const char* title;
  const char* caption;
};
