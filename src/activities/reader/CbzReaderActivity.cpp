#include "CbzReaderActivity.h"

#include <Epub/blocks/ImageBlock.h>
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
#include "util/CbzCover.h"

namespace {

// A page wider than tall is a spread: two portrait halves, each fitted to the screen on its own.
int halvesOf(const ImageDimensions& dimensions) { return dimensions.width > dimensions.height ? 2 : 1; }

ImageDimensions halfOf(const ImageDimensions& dimensions, const int halves) {
  return halves == 2 ? ImageDimensions{static_cast<int16_t>(dimensions.width / 2), dimensions.height} : dimensions;
}

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
  cachePath = cbzcover::cachePathFor(bookPath);
  Storage.ensureDirectoryExists(cachePath.c_str());
  dropStalePageCaches();
  loadProgress();
  loadDirection(zip);
  LOG_DBG("CBZ", "%s: %u pages, %u chapters", bookPath.c_str(), static_cast<unsigned>(book.pages.size()),
          static_cast<unsigned>(book.chapters.size()));
  return true;
}

std::string CbzReaderActivity::getBookThumbBmpPath() const { return cbzcover::thumbTemplate(bookPath); }

std::string CbzReaderActivity::getBookTitle() const { return FsHelpers::getFileNameWithoutExtension(bookPath); }

bool CbzReaderActivity::handleFormatInput() {
  const bool touchMenu = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !touchMenu) return false;
  std::vector<cbz::Chapter> chapters;
  if (book.chapters.size() >= 2) chapters = book.chapters;
  startActivityForResult(std::make_unique<CbzChapterActivity>(renderer, mappedInput, std::move(chapters), currentPage,
                                                              book.pages.size(), rightToLeft),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             const uint32_t page = std::get<PageResult>(result.data).page;
                             if (page == CbzChapterActivity::TOGGLE_RIGHT_TO_LEFT) {
                               rightToLeft = !rightToLeft;
                               const uint8_t flag = rightToLeft ? 1 : 0;
                               HalFile f;
                               if (Storage.openFileForWrite("CBZ", cachePath + "/rtl.bin", f)) {
                                 f.write(&flag, 1);
                                 f.close();
                               }
                             } else {
                               currentPage = page;
                               currentHalf = 0;
                             }
                           }
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

// The page caches are keyed by the book's path; a different file under the same name (another
// size) gets them dropped.
void CbzReaderActivity::dropStalePageCaches() const {
  uint32_t size = 0;
  HalFile f;
  if (Storage.openFileForRead("CBZ", bookPath, f)) {
    size = static_cast<uint32_t>(f.fileSize());
    f.close();
  }
  // The file size and the cache layout: 0.53.0 caches (no split spreads, a 4-byte stamp) go too.
  constexpr uint32_t CACHE_LAYOUT = 2;
  const uint32_t stamped[2] = {size, CACHE_LAYOUT};
  uint32_t stored[2] = {0, 0};
  const std::string stamp = cachePath + "/size.bin";
  if (Storage.openFileForRead("CBZ", stamp, f)) {
    if (f.read(stored, 8) != 8) stored[0] = stored[1] = 0;
    f.close();
  }
  if (stored[0] == stamped[0] && stored[1] == stamped[1]) return;
  for (size_t page = 0; page < book.pages.size(); ++page) removePageCaches(page);
  if (Storage.openFileForWrite("CBZ", stamp, f)) {
    f.write(stamped, 8);
    f.close();
  }
}

// p<N>.pxc for a portrait page; a spread's halves are p<N>a.pxc (left) and p<N>b.pxc (right), by
// their side in the image, so turning right-to-left on or off keeps them.
std::string CbzReaderActivity::pagePxcPath(const size_t page, const int halves, const uint8_t half) const {
  const char* side = halves == 2 ? (sideOf(half) == 0 ? "a" : "b") : "";
  return cachePath + "/p" + std::to_string(page) + side + ".pxc";
}

void CbzReaderActivity::removePageCaches(const size_t page) const {
  for (const char* suffix : {"", "a", "b"}) {
    const std::string pxc = cachePath + "/p" + std::to_string(page) + suffix + ".pxc";
    if (Storage.exists(pxc.c_str())) Storage.remove(pxc.c_str());
  }
}

// How many halves a page has, from its caches on disk: 0 when none exists yet.
int CbzReaderActivity::cachedHalves(const size_t page) const {
  const std::string stem = cachePath + "/p" + std::to_string(page);
  if (Storage.exists((stem + "a.pxc").c_str()) || Storage.exists((stem + "b.pxc").c_str())) return 2;
  return Storage.exists((stem + ".pxc").c_str()) ? 1 : 0;
}

bool CbzReaderActivity::pageCached(const size_t page, const uint8_t half) const {
  const int halves = cachedHalves(page);
  return halves > 0 && Storage.exists(pagePxcPath(page, halves, half).c_str());
}

// The page (or the half of a spread) fitted to the screen as a 2-bit pixel cache, decoded once;
// false on a page that can't be read. `half` is in reading order and is clamped to the page's
// halves (so 1 on a portrait page becomes 0). Leaves the framebuffer with whatever the decoder
// drew.
bool CbzReaderActivity::preparePage(const size_t page, uint8_t& half, int& halves, int& width, int& height) {
  std::string imagePath;
  const bool jpeg = FsHelpers::hasJpgExtension(book.pages[page]);
  JpegToFramebufferConverter jpegDecoder;
  PngToFramebufferConverter pngDecoder;
  ImageToFramebufferDecoder& decoder =
      jpeg ? static_cast<ImageToFramebufferDecoder&>(jpegDecoder) : static_cast<ImageToFramebufferDecoder&>(pngDecoder);
  ImageDimensions dimensions{0, 0};
  const auto readHeader = [&] {
    if (!extractPage(page, imagePath)) return false;
    if (decoder.getDimensions(imagePath, dimensions) && dimensions.width > 0 && dimensions.height > 0) return true;
    Storage.remove(imagePath.c_str());
    imagePath.clear();
    return false;
  };
  // Whether the page is a spread comes from its caches, else from the image header.
  halves = cachedHalves(page);
  if (halves == 0) {
    if (!readHeader()) return false;
    halves = halvesOf(dimensions);
  }
  if (half >= halves) half = static_cast<uint8_t>(halves - 1);
  const std::string pxc = pagePxcPath(page, halves, half);
  HalFile f;
  if (Storage.openFileForRead("CBZ", pxc, f)) {
    uint16_t dims[2] = {0, 0};
    const bool ok = f.read(dims, 4) == 4 && dims[0] > 0 && dims[1] > 0 &&
                    f.fileSize() >= 4 + static_cast<size_t>((dims[0] + 3) / 4) * dims[1];
    f.close();
    if (ok) {
      if (!imagePath.empty()) Storage.remove(imagePath.c_str());
      width = dims[0];
      height = dims[1];
      return true;
    }
    Storage.remove(pxc.c_str());
  }
  if (imagePath.empty() && !readHeader()) return false;
  auto config = fitPage(halfOf(dimensions, halves), renderer);
  config.cachePath = pxc;
  if (halves == 2) config.sourceHalf = sideOf(half) == 0 ? 1 : 2;
  renderer.clearScreen();
  bool ok = decoder.decodeToFramebuffer(imagePath, renderer, config);
  width = config.maxWidth;
  height = config.maxHeight;
  Storage.remove(imagePath.c_str());
  if (!ok) {
    Storage.remove(pxc.c_str());
    return false;
  }
  // The decoder may round the box by a pixel: take the size it wrote.
  if (Storage.openFileForRead("CBZ", pxc, f)) {
    uint16_t dims[2] = {0, 0};
    if (f.read(dims, 4) == 4 && dims[0] > 0 && dims[1] > 0) {
      width = dims[0];
      height = dims[1];
    } else {
      ok = false;
    }
    f.close();
  } else {
    ok = false;
  }
  return ok;
}

// Like the full-screen picture viewer: black and white first, then the two gray planes, all from
// the page's pixel cache.
bool CbzReaderActivity::drawPage(const size_t page, uint8_t& half, int& halves) {
  struct PxcSlotGuard {
    ~PxcSlotGuard() { ImageBlock::releaseRenderCache(); }
  } pxcSlotGuard;
  int width = 0;
  int height = 0;
  if (!preparePage(page, half, halves, width, height)) {
    // No pixel cache (card full, a write failed): decode for every pass, as before 0.53.0.
    std::string imagePath;
    return extractPage(page, imagePath) && drawDecoded(imagePath, half, halves);
  }
  const std::string pxc = pagePxcPath(page, halves, half);
  const int x = (renderer.getScreenWidth() - width) / 2;
  const int y = (renderer.getScreenHeight() - height) / 2;
  return drawPasses([&] { return renderImagePixelCache(renderer, pxc, x, y, width, height); });
}

// Black and white first, then the two gray planes, then black and white again for the next turn.
bool CbzReaderActivity::drawPasses(const std::function<bool()>& draw) {
  renderer.clearScreen();
  if (!draw()) return false;

  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  bool ready = true;
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(absolute ? 0xFF : 0x00);
    renderer.setRenderMode(mode);
    if (!draw()) {
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
  if (!draw()) ready = false;
  renderer.cleanupGrayscaleWithFrameBuffer();
  return ready;
}

// Without a pixel cache: the decoder runs for every pass.
bool CbzReaderActivity::drawDecoded(const std::string& imagePath, uint8_t& half, int& halves) {
  const bool jpeg = FsHelpers::hasJpgExtension(imagePath);
  JpegToFramebufferConverter jpegDecoder;
  PngToFramebufferConverter pngDecoder;
  ImageToFramebufferDecoder& decoder =
      jpeg ? static_cast<ImageToFramebufferDecoder&>(jpegDecoder) : static_cast<ImageToFramebufferDecoder&>(pngDecoder);
  ImageDimensions dimensions;
  if (!decoder.getDimensions(imagePath, dimensions) || dimensions.width <= 0 || dimensions.height <= 0) return false;
  halves = halvesOf(dimensions);
  if (half >= halves) half = static_cast<uint8_t>(halves - 1);
  auto config = fitPage(halfOf(dimensions, halves), renderer);
  if (halves == 2) config.sourceHalf = sideOf(half) == 0 ? 1 : 2;
  return drawPasses([&] { return decoder.decodeToFramebuffer(imagePath, renderer, config); });
}

// After a page is up: the next half or page in reading order is decoded into its pixel cache, so
// the turn only reads the cache. The framebuffer is kept around it. Caches more than two pages
// away go.
void CbzReaderActivity::prefetchNext() {
  for (const int far : {-3, 3}) {
    const int64_t p = static_cast<int64_t>(currentPage) + far;
    if (p >= 0 && p < static_cast<int64_t>(book.pages.size())) removePageCaches(static_cast<size_t>(p));
  }
  int64_t next = currentPage;
  uint8_t half = 1;
  if (currentHalf != 0 || currentHalves != 2) {
    next += rightToLeft ? -1 : 1;
    half = 0;
  }
  if (next < 0 || next >= static_cast<int64_t>(book.pages.size())) return;
  if (pageCached(static_cast<size_t>(next), half)) return;
  if (!renderer.storeBwBuffer()) return;
  int halves = 0;
  int width = 0;
  int height = 0;
  if (!preparePage(static_cast<size_t>(next), half, halves, width, height)) {
    LOG_DBG("CBZ", "Couldn't prepare page %lld ahead", static_cast<long long>(next + 1));
  }
  renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
}

void CbzReaderActivity::renderBook() {
  if (book.pages.empty() || currentPage >= book.pages.size()) return;
  currentHalves = 1;
  const bool drawn = drawPage(currentPage, currentHalf, currentHalves);
  if (!drawn) {
    currentHalf = 0;
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
  }
  LOG_DBG("CBZ", "Page %lu/%u%s", static_cast<unsigned long>(currentPage + 1), static_cast<unsigned>(book.pages.size()),
          currentHalves == 2 ? (currentHalf == 0 ? " (first half)" : " (second half)") : "");
  saveProgress();
  markPageRendered();
  publishLiveBookProgress(getScreenshotInfo().progressPercent);
  if (drawn) prefetchNext();
}

// Right to left (manga, Phase 1c.4): the book's own choice in rtl.bin, else its ComicInfo.xml.
void CbzReaderActivity::loadDirection(ZipFile& zip) {
  HalFile f;
  if (Storage.openFileForRead("CBZ", cachePath + "/rtl.bin", f)) {
    uint8_t flag = 0;
    if (f.read(&flag, 1) == 1) {
      rightToLeft = flag != 0;
      return;
    }
  }
  if (book.comicInfo.empty()) return;
  size_t size = 0;
  uint8_t* xml = zip.readFileToMemory(book.comicInfo.c_str(), &size, true);
  if (!xml) return;
  rightToLeft = cbz::mangaRightToLeft(std::string_view(reinterpret_cast<const char*>(xml), size));
  free(xml);
}

// A spread's halves are turned like pages: forward from its first half lands on its second, back
// from the next page on its second half (clamped to the first on a portrait page when drawn).
bool CbzReaderActivity::pageTurn(bool isForward) {
  if (rightToLeft) isForward = !isForward;  // the page order stays, next and previous swap
  if (isForward) {
    if (currentHalf == 0 && currentHalves == 2) {
      currentHalf = 1;
      return true;
    }
    if (currentPage < book.pages.size()) {
      ++currentPage;
      currentHalf = 0;
      return true;
    }
  } else if (currentHalf != 0) {
    currentHalf = 0;
    return true;
  } else if (currentPage > 0) {
    --currentPage;
    currentHalf = 1;
    return true;
  }
  return false;
}

bool CbzReaderActivity::skipPages(const int amount) {
  const int last = static_cast<int>(book.pages.size());
  const int target = std::clamp(static_cast<int>(currentPage) + (rightToLeft ? -amount : amount), 0, last);
  if (target == static_cast<int>(currentPage)) return false;
  currentPage = static_cast<uint32_t>(target);
  currentHalf = 0;
  return true;
}

bool CbzReaderActivity::isAtEndOfBook() const { return !book.pages.empty() && currentPage >= book.pages.size(); }

void CbzReaderActivity::onReturnFromEndOfBook() {
  currentPage = book.pages.empty() ? 0 : static_cast<uint32_t>(book.pages.size() - 1);
  currentHalf = 1;  // the last half, as on any turn back
}

void CbzReaderActivity::saveProgress() const {
  // The page, then the page count, so the home and Library can show progress without the zip.
  const auto pages = static_cast<uint32_t>(book.pages.size());
  const uint8_t data[8] = {static_cast<uint8_t>(currentPage & 0xFF),
                           static_cast<uint8_t>((currentPage >> 8) & 0xFF),
                           static_cast<uint8_t>((currentPage >> 16) & 0xFF),
                           static_cast<uint8_t>((currentPage >> 24) & 0xFF),
                           static_cast<uint8_t>(pages & 0xFF),
                           static_cast<uint8_t>((pages >> 8) & 0xFF),
                           static_cast<uint8_t>((pages >> 16) & 0xFF),
                           static_cast<uint8_t>((pages >> 24) & 0xFF)};
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
