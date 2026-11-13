#include "MarkupBook.h"

#include <ChmBook.h>
#include <DocToHtml.h>
#include <Epub/BookMetadataCache.h>
#include <Fb2ToHtml.h>
#include <FsHelpers.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <HtmlBook.h>
#include <Logging.h>
#include <Memory.h>
#include <RtfToHtml.h>
#include <Utf8.h>

#include "Txt.h"

namespace {

int readFile(void* ctx, uint8_t* buf, size_t size) { return static_cast<HalFile*>(ctx)->read(buf, size); }

bool writePrint(void* ctx, const char* data, size_t size) {
  return static_cast<Print*>(ctx)->write(reinterpret_cast<const uint8_t*>(data), size) == size;
}

// DOC and CHM are containers: they are read at any offset, not front to back.
int readFileAt(void* ctx, uint32_t offset, uint8_t* buf, size_t size) {
  auto* file = static_cast<HalFile*>(ctx);
  if (!file->seek(offset)) return -1;
  return file->read(buf, size);
}

blocksource::Source sourceOf(HalFile& file) {
  const size_t size = file.size();
  return blocksource::Source{&file, readFileAt, size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size)};
}

// The CHM reader's big blocks (the LZX window, 64 KB as a rule and 2 MB at most, and its tables) come from PSRAM
// only: with none free the book is refused, the internal heap is left alone.
void* psramAlloc(size_t size) { return HalMemory::allocatePsram(size).release(); }
void psramRelease(void* block) { HalMemory::PsramDeleter()(static_cast<uint8_t*>(block)); }
constexpr chm::Memory CHM_MEMORY{psramAlloc, psramRelease};

}  // namespace

bool MarkupBook::isMarkupBook(std::string_view path) { return FsHelpers::hasMarkupBookExtension(path); }

const char* MarkupBook::cacheVersionTag(std::string_view path) {
  if (FsHelpers::hasFb2Extension(path)) return fb2::CACHE_VERSION_TAG;
  if (FsHelpers::hasRtfExtension(path)) return rtf::CACHE_VERSION_TAG;
  if (FsHelpers::hasDocExtension(path)) return doc::CACHE_VERSION_TAG;
  if (FsHelpers::hasChmExtension(path)) return chm::CACHE_VERSION_TAG;
  return htmlbook::CACHE_VERSION_TAG;
}

bool MarkupBook::probe(const std::string& filepath, Info* info, bool* encrypted) {
  if (encrypted) *encrypted = false;
  HalFile file;
  if (!Storage.openFileForRead("MKB", filepath, file)) return false;
  Info local;
  Info& result = info ? *info : local;
  const char* problem = nullptr;
  if (FsHelpers::hasFb2Extension(filepath)) {
    fb2::Info found;
    const fb2::Status status = fb2::readInfo(&file, readFile, found);
    if (status != fb2::Status::Ok) problem = fb2::statusText(status);
    result = Info{std::move(found.title), std::move(found.author), std::move(found.language)};
  } else if (FsHelpers::hasRtfExtension(filepath)) {
    rtf::Info found;
    const rtf::Status status = rtf::readInfo(&file, readFile, found);
    if (status != rtf::Status::Ok) problem = rtf::statusText(status);
    result = Info{std::move(found.title), std::move(found.author), std::move(found.language)};
  } else if (FsHelpers::hasDocExtension(filepath)) {
    doc::Info found;
    const doc::Status status = doc::readInfo(sourceOf(file), found);
    if (status != doc::Status::Ok) problem = doc::statusText(status);
    if (status == doc::Status::Encrypted && encrypted) *encrypted = true;
    result = Info{std::move(found.title), std::move(found.author), std::move(found.language)};
  } else if (FsHelpers::hasChmExtension(filepath)) {
    chm::Info found;
    const chm::Status status = chm::readInfo(sourceOf(file), found, CHM_MEMORY);
    if (status != chm::Status::Ok) problem = chm::statusText(status);
    result = Info{std::move(found.title), "", std::move(found.language)};
  } else {
    htmlbook::Info found;
    const htmlbook::Status status = htmlbook::readInfo(&file, readFile, found);
    if (status != htmlbook::Status::Ok) problem = htmlbook::statusText(status);
    result = Info{std::move(found.title), "", std::move(found.language)};
  }
  file.close();
  if (problem) LOG_ERR("MKB", "%s: %s", filepath.c_str(), problem);
  return problem == nullptr;
}

bool MarkupBook::streamHtml(const std::string& filepath, Print& out) {
  Info info;
  if (!probe(filepath, &info)) return false;
  if (info.title.empty()) info.title = FsHelpers::getFileNameWithoutExtension(filepath);
  HalFile file;
  if (!Storage.openFileForRead("MKB", filepath, file)) return false;
  bool ok;
  if (FsHelpers::hasFb2Extension(filepath)) {
    const fb2::Status status = fb2::streamHtml(&file, readFile, info.title, writePrint, &out);
    ok = status == fb2::Status::Ok || status == fb2::Status::Truncated;  // a file cut short shows what it has
    LOG_DBG("MKB", "Streamed %s: %s", filepath.c_str(), fb2::statusText(status));
  } else if (FsHelpers::hasRtfExtension(filepath)) {
    const rtf::Status status = rtf::streamHtml(&file, readFile, info.title, writePrint, &out);
    ok = status == rtf::Status::Ok;
    LOG_DBG("MKB", "Streamed %s: %s", filepath.c_str(), rtf::statusText(status));
  } else if (FsHelpers::hasDocExtension(filepath)) {
    const doc::Status status = doc::streamHtml(sourceOf(file), info.title, writePrint, &out);
    ok = status == doc::Status::Ok || status == doc::Status::Truncated;  // a damaged file shows what it has
    LOG_DBG("MKB", "Streamed %s: %s", filepath.c_str(), doc::statusText(status));
  } else if (FsHelpers::hasChmExtension(filepath)) {
    const chm::Status status = chm::streamHtml(sourceOf(file), info.title, writePrint, &out, CHM_MEMORY);
    ok = status == chm::Status::Ok || status == chm::Status::Truncated;
    LOG_DBG("MKB", "Streamed %s: %s", filepath.c_str(), chm::statusText(status));
  } else {
    const htmlbook::Status status = htmlbook::streamHtml(&file, readFile, info.title, writePrint, &out);
    ok = status == htmlbook::Status::Ok;
    LOG_DBG("MKB", "Streamed %s: %s", filepath.c_str(), htmlbook::statusText(status));
  }
  file.close();
  return ok;
}

bool MarkupBook::buildCache(const std::string& filepath, const std::string& cachePath,
                            std::unique_ptr<BookMetadataCache>& bookMetadataCache) {
  Info info;
  if (!probe(filepath, &info)) return false;

  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  } else {
    Storage.removeDir((cachePath + "/html").c_str());
    Storage.removeDir((cachePath + "/sections").c_str());
  }
  if (!bookMetadataCache->beginWrite() || !bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR("MKB", "Could not begin writing cache");
    return false;
  }
  bookMetadataCache->createSpineEntry("content.html");
  if (!bookMetadataCache->endContentOpfPass() || !bookMetadataCache->beginTocPass()) {
    LOG_ERR("MKB", "Could not write spine");
    return false;
  }
  const std::string title = info.title.empty() ? FsHelpers::getFileNameWithoutExtension(filepath) : info.title;
  bookMetadataCache->createTocEntry(title, "content.html", "", 0);
  if (!bookMetadataCache->endTocPass() || !bookMetadataCache->endWrite()) {
    LOG_ERR("MKB", "Could not end writing cache");
    return false;
  }

  BookMetadataCache::BookMetadata bookMetadata;
  bookMetadata.title = utf8ComposeNfc(title);
  bookMetadata.author = utf8ComposeNfc(info.author);
  bookMetadata.language = info.language.size() < 2 ? "en" : info.language.substr(0, 2);
  // Like a .txt: a picture next to the book with its name, or called cover, is its cover.
  const std::string companionCover = Txt::findCompanionCoverImage(filepath);
  if (!companionCover.empty()) bookMetadata.coverItemHref = companionCover;
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata)) {
    LOG_ERR("MKB", "Could not build book.bin");
    return false;
  }
  bookMetadataCache->cleanupTmpFiles();
  bookMetadataCache = makeUniqueNoThrow<BookMetadataCache>(cachePath);
  return bookMetadataCache && bookMetadataCache->load();
}
