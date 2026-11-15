#include "PdfConvert.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <InflateStream.h>
#include <Logging.h>
#include <PdfText.h>
#include <esp_heap_caps.h>

#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>  // the device's build settings; the simulator has none
#endif

#include <functional>
#include <memory>
#include <new>

#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "util/SmallBlocksInPsram.h"
#include "util/VaultFiles.h"

namespace pdfconvert {
namespace {

std::string directoryFor(const std::string& pdfPath) {
  return "/.vaultink/pdf/" + std::to_string(std::hash<std::string>{}(pdfPath));
}

std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name.empty() ? std::string("PDF") : name;
}

// The inflater for the PDF library: zlib-wrapped data into a string, up to a limit.
bool inflateZlib(const uint8_t* in, size_t len, std::string& out, size_t maxOut) {
  out.clear();
  InflateStream stream;
  if (!stream.init(true)) return false;
  stream.setSource(in, len);
  stream.setZlibWrapped();
  // Not on the stack: this runs under the PDF reader's nested frames, on the 24 KB loop task.
  constexpr size_t BUFFER_BYTES = 4096;
  const std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[BUFFER_BYTES]);
  if (!buffer) return false;
  while (true) {
    size_t produced = 0;
    const auto status = stream.readAtMost(buffer.get(), BUFFER_BYTES, &produced);
    out.append(reinterpret_cast<const char*>(buffer.get()), produced);
    if (out.size() > maxOut) return false;
    if (status == InflateStream::Status::Done) return true;
    if (status == InflateStream::Status::Error) return false;
  }
}

bool readStamp(const std::string& path, uint64_t& stamp) {
  HalFile file;
  if (!Storage.openFileForRead("PDF", path, file)) return false;
  char buffer[24] = {};
  const int n = file.read(buffer, sizeof(buffer) - 1);
  if (n <= 0) return false;
  stamp = std::strtoull(buffer, nullptr, 10);
  return true;
}

}  // namespace

std::string textPathFor(const std::string& pdfPath) { return directoryFor(pdfPath) + "/" + baseName(pdfPath) + ".txt"; }

Status ensureText(const GfxRenderer& renderer, const std::string& pdfPath, std::string& textPath) {
  textPath = textPathFor(pdfPath);
  const std::string dir = directoryFor(pdfPath);
  const std::string stampPath = dir + "/stamp.txt";
  // The stamp also carries the converter's version: the text with style marks (0.73.0) replaces what 0.72.5 and
  // earlier wrote, so a book converted before is converted again the next time it is opened.
  constexpr uint64_t CONVERTER_VERSION = 0x0300000000000000ULL;
  const uint64_t fileStamp = vaultfiles::sdFs().stamp(pdfPath);
  const uint64_t stamp = fileStamp == 0 ? 0 : fileStamp ^ CONVERTER_VERSION;
  uint64_t stored = 0;
  if (stamp != 0 && Storage.exists(textPath.c_str()) && readStamp(stampPath, stored) && stored == stamp) {
    return Status::Ok;
  }

  HalFile source;
  if (!Storage.openFileForRead("PDF", pdfPath, source)) return Status::SdError;
  pdf::Source src;
  src.size = source.fileSize64();
  src.read = [&source](uint64_t offset, uint8_t* out, size_t len) {
    if (!source.seek64(offset)) return false;
    return source.read(out, len) == static_cast<int>(len);
  };

  Storage.ensureDirectoryExists(dir.c_str());
  const std::string tmp = textPath + ".tmp";
  HalFile out;
  if (!Storage.openFileForWrite("PDF", tmp, out)) return Status::SdError;

  Rect popup{};
  {
    RenderLock lock;
    popup = GUI.drawPopup(renderer, tr(STR_PDF_READING));
  }
  bool writeOk = true;
  std::string pending;  // the last page with text, held back until the next one says how they join
  const auto flushPending = [&](const std::string& next) {
    if (pending.empty()) return;
    const std::string separator = next.empty() ? std::string("\n\n") : pdf::pageSeparator(pending, next);
    writeOk = writeOk && out.write(pending.data(), pending.size()) == pending.size() &&
              (separator.empty() || out.write(separator.data(), separator.size()) == separator.size());
    pending.clear();
  };
  pdf::Result result;
  pdf::Limits limits;
  limits.styleMarks = true;  // headings, bold and italic: the reader's Style setting decides if they show
  // The memory guard: the library drops what it can read again when the internal heap is nearly used up, and stops
  // (Status::OutOfMemory) before an allocation fails, which in this build is a reset.
  limits.freeMemory = [] { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); };
  const size_t heapBefore = limits.freeMemory();
  // cppcheck-suppress unusedVariable
  const SmallBlocksInPsram smallBlocks;  // its constructor and destructor are the point
  const bool opened = pdf::extractText(
      src, inflateZlib,
      [&](size_t page, size_t pages, const std::string& text) {
        delay(1);  // a long file takes a while; let the other tasks run
        if (!text.empty()) {
          flushPending(text);
          pending = text;
        }
        if (pages > 0 && (page % 4 == 0 || page + 1 == pages)) {
          RenderLock lock;
          GUI.fillPopupProgress(renderer, popup, static_cast<int>((page + 1) * 100 / pages));
        }
      },
      &result, limits);
  flushPending("");
  // How close the extraction came to the end of this task's stack (the loop task, 24 KB): a PDF with nested forms
  // once overflowed it. Logged so a device run shows the real figure, in words (the high-water mark is the least
  // stack that was ever free, in bytes on ESP-IDF).
  LOG_INF("PDF", "stack: least free %u bytes",
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));  // bytes on ESP-IDF's FreeRTOS
  out.flush();
  out.close();
  source.close();
  LOG_INF("PDF", "internal heap: %u free before, %u at the lowest since boot", static_cast<unsigned>(heapBefore),
          static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  if (!opened) {
    Storage.remove(tmp.c_str());
    if (result.outOfMemory) {
      LOG_ERR("PDF", "%s: out of memory at page %u", pdfPath.c_str(), static_cast<unsigned>(result.pages));
      return Status::OutOfMemory;
    }
    return result.encrypted ? Status::Encrypted : Status::Unreadable;
  }
  if (!writeOk) {
    Storage.remove(tmp.c_str());
    return Status::SdError;
  }
  if (result.textPages == 0) {
    Storage.remove(tmp.c_str());
    // Pages with glyphs that have no way to text (a CID font without a ToUnicode map) are not a scan.
    return result.droppedGlyphs > 0 ? Status::UnreadableFont : Status::NoText;
  }
  if (!Storage.replaceFile(tmp.c_str(), textPath.c_str())) return Status::SdError;

  HalFile stampFile;
  if (Storage.openFileForWrite("PDF", stampPath, stampFile)) {
    const std::string text = std::to_string(stamp);
    stampFile.write(text.data(), text.size());
    stampFile.flush();
    stampFile.close();
  }
  LOG_INF("PDF", "%s: %u pages, %u with text", pdfPath.c_str(), static_cast<unsigned>(result.pages),
          static_cast<unsigned>(result.textPages));
  return Status::Ok;
}

void showFailure(const GfxRenderer& renderer, const Status status) {
  const char* message = tr(STR_PDF_UNREADABLE);
  if (status == Status::NoText) message = tr(STR_PDF_NO_TEXT);
  if (status == Status::Encrypted) message = tr(STR_PDF_ENCRYPTED);
  if (status == Status::UnreadableFont) message = tr(STR_PDF_UNREADABLE_FONT);
  if (status == Status::OutOfMemory) message = tr(STR_PDF_OUT_OF_MEMORY);
  RenderLock lock;
  GUI.drawPopup(renderer, message);
  delay(2500);
}

}  // namespace pdfconvert
