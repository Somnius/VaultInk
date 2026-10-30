#include "CbzPages.h"

#include <algorithm>
#include <cctype>

namespace cbz {
namespace {

std::string lowerExt(std::string_view name) {
  const size_t dot = name.find_last_of('.');
  const size_t slash = name.find_last_of('/');
  if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return "";
  std::string ext(name.substr(dot + 1));
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
  return ext;
}

bool isPage(std::string_view ext) { return ext == "jpg" || ext == "jpeg" || ext == "png"; }

bool isOtherImage(std::string_view ext) {
  return ext == "webp" || ext == "gif" || ext == "bmp" || ext == "avif" || ext == "jxl" || ext == "tif" ||
         ext == "tiff" || ext == "heic";
}

// Junk and unsafe names: macOS metadata, Windows thumbnails, hidden files, "..", absolute paths.
bool skipped(std::string_view entry) {
  if (entry.empty() || entry.front() == '/' || entry.back() == '/') return true;
  size_t start = 0;
  while (start <= entry.size()) {
    size_t end = entry.find('/', start);
    if (end == std::string_view::npos) end = entry.size();
    const std::string_view part = entry.substr(start, end - start);
    if (part.empty() || part == "." || part == ".." || part == "__MACOSX" || part.front() == '.') return true;
    start = end + 1;
  }
  const size_t slash = entry.find_last_of('/');
  std::string base(entry.substr(slash == std::string_view::npos ? 0 : slash + 1));
  std::transform(base.begin(), base.end(), base.begin(),
                 [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
  return base == "thumbs.db" || base == "desktop.ini";
}

std::string_view folderOf(std::string_view entry) {
  const size_t slash = entry.find_last_of('/');
  return slash == std::string_view::npos ? std::string_view() : entry.substr(0, slash);
}

}  // namespace

bool mangaRightToLeft(const std::string_view comicInfoXml) {
  std::string xml(comicInfoXml);
  std::transform(xml.begin(), xml.end(), xml.begin(),
                 [](const char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
  const size_t open = xml.find("<manga>");
  if (open == std::string::npos) return false;
  const size_t start = open + 7;
  const size_t close = xml.find("</manga>", start);
  if (close == std::string::npos) return false;
  std::string value = xml.substr(start, close - start);
  value.erase(std::remove_if(value.begin(), value.end(),
                             [](const char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; }),
              value.end());
  return value == "yesandrighttoleft";
}

bool naturalLess(std::string_view a, std::string_view b) {
  size_t i = 0;
  size_t j = 0;
  while (i < a.size() && j < b.size()) {
    const auto ca = static_cast<unsigned char>(a[i]);
    const auto cb = static_cast<unsigned char>(b[j]);
    if (std::isdigit(ca) && std::isdigit(cb)) {
      size_t ei = i;
      size_t ej = j;
      while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
      while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
      // Compare the numbers by value: skip leading zeros, then length, then digits.
      size_t si = i;
      size_t sj = j;
      while (si + 1 < ei && a[si] == '0') ++si;
      while (sj + 1 < ej && b[sj] == '0') ++sj;
      if (ei - si != ej - sj) return ei - si < ej - sj;
      const int cmp = a.substr(si, ei - si).compare(b.substr(sj, ej - sj));
      if (cmp != 0) return cmp < 0;
      if (ei - i != ej - j) return ei - i > ej - j;  // "007" before "7": more zeros first, any fixed rule
      i = ei;
      j = ej;
      continue;
    }
    const int la = std::tolower(ca);
    const int lb = std::tolower(cb);
    if (la != lb) return la < lb;
    ++i;
    ++j;
  }
  return i == a.size() && j < b.size();  // the shorter one, when one is a prefix of the other
}

Book pages(const std::vector<std::string>& entries) {
  Book book;
  for (const auto& entry : entries) {
    std::string lower = entry;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
    if (lower == "comicinfo.xml") book.comicInfo = entry;
    if (skipped(entry)) continue;
    const std::string ext = lowerExt(entry);
    if (isPage(ext)) {
      book.pages.push_back(entry);
    } else if (isOtherImage(ext)) {
      ++book.skipped;
    }
  }
  // Folders first, then the page names inside them, both in natural order.
  std::sort(book.pages.begin(), book.pages.end(), [](const std::string& a, const std::string& b) {
    const std::string_view fa = folderOf(a);
    const std::string_view fb = folderOf(b);
    if (fa != fb) return naturalLess(fa, fb);
    return naturalLess(a, b);
  });
  std::string_view current;
  for (size_t i = 0; i < book.pages.size(); ++i) {
    const std::string_view folder = folderOf(book.pages[i]);
    if (i == 0 || folder != current) {
      const size_t slash = folder.find_last_of('/');
      book.chapters.push_back({std::string(folder.substr(slash == std::string_view::npos ? 0 : slash + 1)), i});
      current = folder;
    }
  }
  // A zip without folders has no chapters to show.
  if (book.chapters.size() == 1 && book.chapters[0].title.empty()) book.chapters.clear();
  return book;
}

}  // namespace cbz
