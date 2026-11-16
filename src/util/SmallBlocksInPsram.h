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
//
// `keepInternal`: blocks up to this size still come from the internal heap (malloc()'s rule is
// "size <= limit": a block of exactly 64 bytes stays internal with 64). A sync passes 0, every
// block from the PSRAM: what it keeps for a whole vault is mostly blocks of 64 bytes and less. Each
// file the vault has is one node in each of two std::map<string, string> (64 bytes on this 32-bit
// board, measured with a 32-bit build on a computer), its path and its revision ("12-" and 32 hex
// digits): about 220 bytes of internal heap a file with the limit at 64, taken when the sync
// starts and given back when it ends. The JSON documents' strings (ids, revisions) are blocks of
// that size too.
class SmallBlocksInPsram {
 public:
  explicit SmallBlocksInPsram(const size_t keepInternal = 64) {
#if defined(CONFIG_SPIRAM_USE_MALLOC) && defined(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
    active = psramFound();
    if (active) heap_caps_malloc_extmem_enable(keepInternal);
#else
    (void)keepInternal;
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
