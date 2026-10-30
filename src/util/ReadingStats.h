#pragma once

#include <ReadingLog.h>

#include <cstdint>
#include <string>

// The reading log on the SD card (kos25k #23): sessions are added when a book closes or the
// device sleeps; the statistics sleep screen reads them back in local time.
namespace readingstats {

void addSession(const std::string& path, int64_t start, uint32_t seconds, uint16_t progressBp);
// Totals, recent books and this month's calendar at `now` (local time from the clock's timezone).
readinglog::Stats load(int64_t now, const std::string& currentBook);

}  // namespace readingstats
