#pragma once

#include <string>

// Covers for CBZ comics (Phase 1c.3): the first page, as the same thumb_<height>.bmp files the
// home and Library use for EPUB and XTC covers, in the book's cache folder.
namespace cbzcover {

std::string cachePathFor(const std::string& bookPath);   // /.crosspoint/cbz_<hash>
std::string thumbTemplate(const std::string& bookPath);  // .../thumb_[HEIGHT].bmp
bool generate(const std::string& bookPath, int height);  // true when the thumbnail is there

}  // namespace cbzcover
