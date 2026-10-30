#include "CbzReaderActivity.h"

#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <ZipFile.h>

#include <algorithm>
#include <functional>

#include "CbzChapterActivity.h"
#include "MappedInputManager.h"
#include "ProgressFile.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookProgress.h"

namespace {

RenderConfig fitPage(const ImageDimensions& dimensions, const GfxRenderer& renderer) {
  const float scale = std::min({static_cast<float>(renderer.getScreenWidth()) / dimensions.width,
                                static_cast<float>(renderer.getScreenHeight()) / dimensions.height, 1.0f});
  const int width = std::max(1, std::min(renderer.getScreenWidth(), static_cast<int>(dimensions.width * scale)));
  const int height = std::max(1, std::min(renderer.getScreenHeight(), static_cast<int>(dimensions.height * scale)));
  RenderConfig config{(renderer.getScreenWidth() - width) / 2, (renderer.getScreenHeight() - height) / 2, width,
                      height};
  config.useExactDimensions = true;
  return config;
}

}  // namespace

bool CbzReaderActivity::loadBook() {
  ZipFile zip(bookPath);
  std::vector<std::string> entries;
  if (!zip.enumerateFilePaths([&entries](std::string_view path) { entries.emplace_back(path); })) {
    LOG_ERR("CBZ", "Not a readable zip: %s", bookPath.c_str());
    return false;
  }
  book = cbz::pages(entries);
  if (book.pages.empty()) {
    LOG_ERR("CBZ", "No JPEG or PNG pages in %s (%u others)", bookPath.c_str(), static_cast<unsigned>(book.skipped));
    return false;
  }
  cachePath = "/.crosspoint/cbz_" + std::to_string(std::hash<std::string>{}(bookPath));
  Storage.ensureDirectoryExists(cachePath.c_str());
  loadProgress();
  LOG_DBG("CBZ", "%s: %u pages, %u chapters", bookPath.c_str(), static_cast<unsigned>(book.pages.size()),
          static_cast<unsigned>(book.chapters.size()));
  return true;
}

std::string CbzReaderActivity::getBookTitle() const { return FsHelpers::getFileNameWithoutExtension(bookPath); }

bool CbzReaderActivity::handleFormatInput() {
  const bool touchMenu = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !touchMenu) return false;
  if (book.chapters.size() < 2) return true;  // nothing to choose between
  startActivityForResult(std::make_unique<CbzChapterActivity>(renderer, mappedInput, book.chapters, currentPage),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) currentPage = std::get<PageResult>(result.data).page;
                           requestUpdate();
                         });
  return true;
}

void CbzReaderActivity::applyInitialOrientation() { renderer.setOrientation(GfxRenderer::Orientation::Portrait); }

bool CbzReaderActivity::extractPage(const size_t page, std::string& outPath) const {
  const std::string& entry = book.pages[page];
  const bool jpeg = FsHelpers::hasJpgExtension(entry);
  outPath = cachePath + (jpeg ? "/page.jpg" : "/page.png");
  ZipFile zip(bookPath);
  if (!zip.open()) return false;
  HalFile out;
  bool ok = Storage.openFileForWrite("CBZ", outPath, out);
  ok = ok && zip.readFileToStream(entry.c_str(), out, 1024);
  out.close();
  zip.close();
  if (!ok) LOG_ERR("CBZ", "Couldn't read page %u (%s)", static_cast<unsigned>(page + 1), entry.c_str());
  return ok;
}

// Like the full-screen picture viewer: black and white first, then the two gray planes.
bool CbzReaderActivity::drawPage(const std::string& imagePath) {
  const bool jpeg = FsHelpers::hasJpgExtension(imagePath);
  JpegToFramebufferConverter jpegDecoder;
  PngToFramebufferConverter pngDecoder;
  ImageToFramebufferDecoder& decoder =
      jpeg ? static_cast<ImageToFramebufferDecoder&>(jpegDecoder) : static_cast<ImageToFramebufferDecoder&>(pngDecoder);
  ImageDimensions dimensions;
  if (!decoder.getDimensions(imagePath, dimensions) || dimensions.width <= 0 || dimensions.height <= 0) return false;
  const auto config = fitPage(dimensions, renderer);
  renderer.clearScreen();
  if (!decoder.decodeToFramebuffer(imagePath, renderer, config)) return false;

  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  bool ready = true;
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(absolute ? 0xFF : 0x00);
    renderer.setRenderMode(mode);
    if (!decoder.decodeToFramebuffer(imagePath, renderer, config)) {
      ready = false;
      break;
    }
    if (mode == GfxRenderer::GRAYSCALE_LSB) {
      renderer.copyGrayscaleLsbBuffers();
    } else {
      renderer.copyGrayscaleMsbBuffers();
    }
  }
  if (ready) renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  if (!decoder.decodeToFramebuffer(imagePath, renderer, config)) ready = false;
  renderer.cleanupGrayscaleWithFrameBuffer();
  return ready;
}

void CbzReaderActivity::renderBook() {
  if (book.pages.empty() || currentPage >= book.pages.size()) return;
  std::string imagePath;
  if (!extractPage(currentPage, imagePath) || !drawPage(imagePath)) {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
  }
  LOG_DBG("CBZ", "Page %lu/%u", static_cast<unsigned long>(currentPage + 1), static_cast<unsigned>(book.pages.size()));
  saveProgress();
  markPageRendered();
  publishLiveBookProgress(getScreenshotInfo().progressPercent);
}

bool CbzReaderActivity::pageTurn(const bool isForward) {
  if (isForward) {
    if (currentPage < book.pages.size()) {
      ++currentPage;
      return true;
    }
  } else if (currentPage > 0) {
    --currentPage;
    return true;
  }
  return false;
}

bool CbzReaderActivity::skipPages(const int amount) {
  const int last = static_cast<int>(book.pages.size());
  const int target = std::clamp(static_cast<int>(currentPage) + amount, 0, last);
  if (target == static_cast<int>(currentPage)) return false;
  currentPage = static_cast<uint32_t>(target);
  return true;
}

bool CbzReaderActivity::isAtEndOfBook() const { return !book.pages.empty() && currentPage >= book.pages.size(); }

void CbzReaderActivity::onReturnFromEndOfBook() {
  currentPage = book.pages.empty() ? 0 : static_cast<uint32_t>(book.pages.size() - 1);
}

void CbzReaderActivity::saveProgress() const {
  const uint8_t data[4] = {static_cast<uint8_t>(currentPage & 0xFF), static_cast<uint8_t>((currentPage >> 8) & 0xFF),
                           static_cast<uint8_t>((currentPage >> 16) & 0xFF),
                           static_cast<uint8_t>((currentPage >> 24) & 0xFF)};
  if (!ProgressFile::writeAtomic(cachePath, data, sizeof(data))) LOG_ERR("CBZ", "Failed to save progress");
}

void CbzReaderActivity::loadProgress() {
  HalFile f;
  if (!Storage.openFileForRead("CBZ", cachePath + "/progress.bin", f)) return;
  uint8_t data[4];
  if (f.read(data, 4) == 4) {
    currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (static_cast<uint32_t>(data[3]) << 24);
    if (currentPage >= book.pages.size()) currentPage = static_cast<uint32_t>(book.pages.size() - 1);
  }
}

ScreenshotInfo CbzReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Cbz;
  snprintf(info.title, sizeof(info.title), "%s", getBookTitle().c_str());
  const auto pages = static_cast<uint32_t>(book.pages.size());
  const uint32_t page = pages > 0 && currentPage >= pages ? pages - 1 : currentPage;
  info.totalPages = static_cast<int>(pages);
  info.currentPage = static_cast<int>(page) + 1;
  info.progressPercent = pages > 0 ? static_cast<int>((page + 1) * 100 / pages) : 0;
  return info;
}
