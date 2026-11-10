#include "FileTypeIcons.h"

#include <FsHelpers.h>

#include <cstdint>

#include "components/icons/fileTypeIcons.h"

namespace fui = freeink::ui;

namespace {
enum Kind : uint8_t { EPUB, PDF, MOBI, AZW, TXT, MD, XTC, CBZ, FB2, HTML, RTF, KIND_COUNT, NONE = 255 };

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
  if (FsHelpers::hasFb2Extension(name)) return FB2;
  if (FsHelpers::hasHtmlExtension(name)) return HTML;
  if (FsHelpers::hasRtfExtension(name)) return RTF;
  return NONE;
}

const freeink::Icon* pageIcon(const uint8_t kind) {
  static const freeink::Icon* const icons[KIND_COUNT] = {
      &icon_page_epub_44, &icon_page_pdf_44, &icon_page_mobi_44, &icon_page_azw_44, &icon_page_txt_44, &icon_page_md_44,
      &icon_page_xtc_44,  &icon_page_cbz_44, &icon_page_fb2_44,  &icon_page_htm_44, &icon_page_rtf_44};
  return kind < KIND_COUNT ? icons[kind] : nullptr;
}

}  // namespace

namespace filetypeicon {

bool forFile(const std::string_view name, fui::BitmapRef& out) {
  const freeink::Icon* icon = pageIcon(kindOf(name));
  if (!icon) return false;
  out = fui::bitmapFromIcon(*icon);
  return true;
}

}  // namespace filetypeicon
