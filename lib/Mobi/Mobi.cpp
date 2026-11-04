#include "Mobi.h"

#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <cstring>

namespace {

int readAtFile(void* ctx, uint32_t offset, uint8_t* buf, size_t size) {
  auto* file = static_cast<HalFile*>(ctx);
  if (file->position() != offset && !file->seek(offset)) return -1;
  return file->read(buf, size);
}

mobi::Reader readerFor(HalFile& file) {
  mobi::Reader reader;
  reader.ctx = &file;
  reader.readAt = readAtFile;
  reader.fileSize = static_cast<uint32_t>(file.size());
  return reader;
}

struct ImageCtx {
  const mobi::Reader* reader;
  const mobi::Info* info;
  const std::string* cachePath;
};

// <img recindex="N"> is PDB record firstImageIndex + N - 1. Written once per book into the cache
// folder; the reader's note-image branch then reads it back by SD path.
bool extractImage(void* c, uint32_t recindex, std::string& path) {
  auto* ctx = static_cast<ImageCtx*>(c);
  const uint32_t first = ctx->info->firstImageIndex;
  if (recindex == 0 || first == 0 || first == 0xFFFFFFFF) return false;
  const uint32_t index = first + recindex - 1;
  if (index >= ctx->info->numRecords) return false;
  uint32_t offset, length;
  if (!mobi::recordSpan(*ctx->reader, static_cast<uint16_t>(index), offset, length) || length < 8) return false;
  uint8_t magic[8];
  if (ctx->reader->readAt(ctx->reader->ctx, offset, magic, sizeof(magic)) != static_cast<int>(sizeof(magic))) {
    return false;
  }
  const char* ext = nullptr;
  if (magic[0] == 0xFF && magic[1] == 0xD8)
    ext = ".jpg";
  else if (memcmp(magic, "\x89PNG", 4) == 0)
    ext = ".png";
  else if (memcmp(magic, "GIF8", 4) == 0)
    ext = ".gif";
  else if (memcmp(magic, "BM", 2) == 0)
    ext = ".bmp";
  if (!ext) return false;  // not a picture record (fonts, FLIS/FCIS, etc.)
  path = *ctx->cachePath + "/mobi_img_" + std::to_string(recindex) + ext;
  if (Storage.exists(path.c_str())) return true;
  HalFile dst;
  if (!Storage.openFileForWrite("MOBI", path, dst)) return false;
  uint8_t buf[512];
  uint32_t at = 0;
  bool ok = true;
  while (at < length) {
    const size_t want = length - at < sizeof(buf) ? length - at : sizeof(buf);
    const int n = ctx->reader->readAt(ctx->reader->ctx, offset + at, buf, want);
    if (n <= 0 || dst.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
    at += static_cast<uint32_t>(n);
  }
  dst.close();
  if (!ok) Storage.remove(path.c_str());
  return ok;
}

}  // namespace

bool Mobi::isMobi(std::string_view path) { return FsHelpers::hasMobiExtension(path); }

mobi::Status Mobi::probe(const std::string& filepath, mobi::Info* info) {
  HalFile file;
  if (!Storage.openFileForRead("MOBI", filepath, file)) return mobi::Status::IoError;
  mobi::Info local;
  const mobi::Status status = mobi::open(readerFor(file), info ? *info : local);
  file.close();
  if (status != mobi::Status::Ok) LOG_ERR("MOBI", "%s: %s", filepath.c_str(), mobi::statusText(status));
  return status;
}

bool Mobi::streamHtml(const std::string& filepath, Print& out, const std::string& cachePath) {
  HalFile file;
  if (!Storage.openFileForRead("MOBI", filepath, file)) return false;
  const mobi::Reader reader = readerFor(file);
  mobi::Info info;
  mobi::Status status = mobi::open(reader, info);
  if (status != mobi::Status::Ok) {
    LOG_ERR("MOBI", "%s: %s", filepath.c_str(), mobi::statusText(status));
    return false;
  }
  if (info.title.empty()) info.title = FsHelpers::getFileNameWithoutExtension(filepath);
  mobi::Sink sink;
  sink.ctx = &out;
  sink.write = [](void* ctx, const char* data, size_t size) {
    return static_cast<Print*>(ctx)->write(reinterpret_cast<const uint8_t*>(data), size) == size;
  };
  ImageCtx imageCtx{&reader, &info, &cachePath};
  mobi::ImageResolver images;
  images.ctx = &imageCtx;
  images.resolve = extractImage;
  status = mobi::streamHtml(reader, info, info.title, sink, cachePath.empty() ? nullptr : &images);
  file.close();
  LOG_DBG("MOBI", "Streamed %s: %s (%u text records)", filepath.c_str(), mobi::statusText(status),
          info.textRecordCount);
  return status == mobi::Status::Ok;
}

bool Mobi::buildCache(const std::string& filepath, const std::string& cachePath,
                      std::unique_ptr<BookMetadataCache>& bookMetadataCache) {
  mobi::Info info;
  if (probe(filepath, &info) != mobi::Status::Ok) return false;  // DRM, unknown compression or damaged: refused

  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  } else {
    Storage.removeDir((cachePath + "/html").c_str());
    Storage.removeDir((cachePath + "/sections").c_str());
  }
  if (!bookMetadataCache->beginWrite() || !bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR("MOBI", "Could not begin writing cache");
    return false;
  }
  bookMetadataCache->createSpineEntry("content.html");
  if (!bookMetadataCache->endContentOpfPass() || !bookMetadataCache->beginTocPass()) {
    LOG_ERR("MOBI", "Could not write spine");
    return false;
  }
  std::string title = info.title.empty() ? FsHelpers::getFileNameWithoutExtension(filepath) : info.title;
  bookMetadataCache->createTocEntry(title, "content.html", "", 0);
  if (!bookMetadataCache->endTocPass() || !bookMetadataCache->endWrite()) {
    LOG_ERR("MOBI", "Could not end writing cache");
    return false;
  }

  BookMetadataCache::BookMetadata bookMetadata;
  bookMetadata.title = utf8ComposeNfc(title);
  bookMetadata.author = utf8ComposeNfc(info.author);
  bookMetadata.language = info.language.empty() ? "en" : info.language.substr(0, 2);
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata)) {
    LOG_ERR("MOBI", "Could not build book.bin");
    return false;
  }
  bookMetadataCache->cleanupTmpFiles();
  bookMetadataCache = makeUniqueNoThrow<BookMetadataCache>(cachePath);
  return bookMetadataCache && bookMetadataCache->load();
}
