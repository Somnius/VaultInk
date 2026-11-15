#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>  // the device's build settings; the simulator has none
#endif

// While a long job runs (a PDF conversion, a LiveSync sync), blocks over 64 bytes come from the PSRAM where the board
// has it. Those jobs hold tens of thousands of blocks of 100 to 4000 bytes, and malloc() takes blocks up to
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (4096) from the internal heap while it has room: a 166-page PDF left 140 bytes
// of it free, and a LiveSync pull 104 bytes (both measured on an X4 Pro). The Wi-Fi and TCP stack can only use the
// internal heap, so with none left the server's answers were never received. The setting is malloc()'s own (Arduino
// sets it once at boot) and is put back when the job is done. A block that does not fit in the PSRAM still falls back
// to the internal heap.
class SmallBlocksInPsram {
 public:
  SmallBlocksInPsram() {
#if defined(CONFIG_SPIRAM_USE_MALLOC) && defined(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
    active = psramFound();
    if (active) heap_caps_malloc_extmem_enable(64);
#endif
  }
  ~SmallBlocksInPsram() {
#if defined(CONFIG_SPIRAM_USE_MALLOC) && defined(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
    if (active) heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
  }
  SmallBlocksInPsram(const SmallBlocksInPsram&) = delete;
  SmallBlocksInPsram& operator=(const SmallBlocksInPsram&) = delete;

 private:
  bool active = false;
};
