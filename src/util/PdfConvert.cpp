#include "PdfConvert.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <InflateStream.h>
#include <Logging.h>
#include <PdfText.h>

#include <functional>

#include "activities/RenderLock.h"
#include "components/UITheme.h"
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
  uint8_t buffer[4096];
  while (true) {
    size_t produced = 0;
    const auto status = stream.readAtMost(buffer, sizeof(buffer), &produced);
    out.append(reinterpret_cast<const char*>(buffer), produced);
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
  const uint64_t stamp = vaultfiles::sdFs().stamp(pdfPath);
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
      &result);
  flushPending("");
  out.flush();
  out.close();
  source.close();
  if (!opened) {
    Storage.remove(tmp.c_str());
    return result.encrypted ? Status::Encrypted : Status::Unreadable;
  }
  if (!writeOk) {
    Storage.remove(tmp.c_str());
    return Status::SdError;
  }
  if (result.textPages == 0) {
    Storage.remove(tmp.c_str());
    return Status::NoText;
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
  RenderLock lock;
  GUI.drawPopup(renderer, message);
  delay(2500);
}

}  // namespace pdfconvert
