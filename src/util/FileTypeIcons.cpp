#include "FileTypeIcons.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "components/icons/fileTypeIcons.h"

namespace fui = freeink::ui;

namespace {
enum Kind : uint8_t { EPUB, PDF, MOBI, AZW, TXT, MD, XTC, CBZ, KIND_COUNT, NONE = 255 };

uint8_t kindOf(const std::string_view name) {
  using FsHelpers::checkFileExtension;
  if (checkFileExtension(name, ".epub")) return EPUB;
  if (checkFileExtension(name, ".pdf")) return PDF;
  if (checkFileExtension(name, ".mobi") || checkFileExtension(name, ".prc")) return MOBI;
  if (checkFileExtension(name, ".azw") || checkFileExtension(name, ".azw3")) return AZW;
  if (checkFileExtension(name, ".txt")) return TXT;
  if (checkFileExtension(name, ".md")) return MD;
  if (checkFileExtension(name, ".xtc") || checkFileExtension(name, ".xtch")) return XTC;
  if (checkFileExtension(name, ".cbz")) return CBZ;
  return NONE;
}

const freeink::Icon* pageIcon(const uint8_t kind) {
  static const freeink::Icon* const icons[KIND_COUNT] = {&icon_page_epub_44, &icon_page_pdf_44, &icon_page_mobi_44,
                                                         &icon_page_azw_44,  &icon_page_txt_44, &icon_page_md_44,
                                                         &icon_page_xtc_44,  &icon_page_cbz_44};
  return kind < KIND_COUNT ? icons[kind] : nullptr;
}

const freeink::Icon* folderIcon(const uint8_t kind) {
  static const freeink::Icon* const icons[KIND_COUNT] = {
      &icon_folder_epub_44, &icon_folder_pdf_44, &icon_folder_mobi_44, &icon_folder_azw_44,
      &icon_folder_txt_44,  &icon_folder_md_44,  &icon_folder_xtc_44,  &icon_folder_cbz_44};
  return kind < KIND_COUNT ? icons[kind] : nullptr;
}

struct Counted {
  std::string path;
  uint8_t kind;
};

std::vector<Counted>& countedFolders() {
  static std::vector<Counted> cache;
  return cache;
}

uint8_t dominantKind(const std::string& path) {
  auto& cache = countedFolders();
  const auto known = std::find_if(cache.begin(), cache.end(), [&path](const Counted& c) { return c.path == path; });
  if (known != cache.end()) return known->kind;
  std::array<uint16_t, KIND_COUNT> counts{};
  auto dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) return NONE;  // not counted: try again next time
  {
    char name[256];
    int seen = 0;
    for (auto file = dir.openNextFile(); file && seen < 300; file = dir.openNextFile(), ++seen) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      const uint8_t kind = kindOf(name);
      if (kind != NONE) ++counts[kind];
    }
    dir.close();
  }
  uint8_t best = NONE;
  uint16_t most = 0;
  for (uint8_t k = 0; k < KIND_COUNT; ++k) {
    if (counts[k] > most) {
      most = counts[k];
      best = k;
    }
  }
  constexpr size_t MAX_CACHED = 64;
  if (cache.size() >= MAX_CACHED) cache.erase(cache.begin());
  cache.push_back({path, best});
  return best;
}
}  // namespace

namespace filetypeicon {

bool forFile(const std::string_view name, fui::BitmapRef& out) {
  const freeink::Icon* icon = pageIcon(kindOf(name));
  if (!icon) return false;
  out = fui::bitmapFromIcon(*icon);
  return true;
}

void forget() { countedFolders().clear(); }

bool forFolder(const std::string& path, fui::BitmapRef& out) {
  const freeink::Icon* icon = folderIcon(dominantKind(path));
  if (!icon) return false;
  out = fui::bitmapFromIcon(*icon);
  return true;
}

}  // namespace filetypeicon
