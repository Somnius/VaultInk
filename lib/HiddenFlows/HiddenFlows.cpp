#include "HiddenFlows.h"

#include <algorithm>

namespace hiddenflows {

namespace {
int itemCount(const std::vector<uint32_t>& cumulative) { return static_cast<int>(cumulative.size()); }

float clampFraction(const float fraction) {
  // NaN fails the first comparison and ends up 0.
  if (!(fraction > 0.0f)) return 0.0f;
  return fraction < 1.0f ? fraction : 1.0f;
}

uint32_t part(const uint32_t size, const float fraction) {
  const double bytes = static_cast<double>(size) * static_cast<double>(clampFraction(fraction));
  return std::min(size, static_cast<uint32_t>(bytes + 0.5));
}

uint32_t rawSize(const std::vector<uint32_t>& cumulative) { return cumulative.empty() ? 0 : cumulative.back(); }

uint32_t shownSize(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden) {
  uint32_t total = 0;
  for (int i = 0; i < itemCount(cumulative); i++) {
    if (!isHidden(hidden, i)) total += itemSize(cumulative, i);
  }
  return total;
}

Location rawLocate(const std::vector<uint32_t>& cumulative, const uint32_t bytes) {
  const int count = itemCount(cumulative);
  if (count == 0) return {};
  int index = count - 1;
  for (int i = 0; i < count; i++) {
    if (cumulative[i] >= bytes) {
      index = i;
      break;
    }
  }
  const uint32_t before = index > 0 ? cumulative[index - 1] : 0;
  const uint32_t size = itemSize(cumulative, index);
  if (size == 0 || bytes <= before) return {index, 0.0f};
  return {index, clampFraction(static_cast<float>(static_cast<double>(bytes - before) / size))};
}
}  // namespace

uint32_t itemSize(const std::vector<uint32_t>& cumulative, const int index) {
  if (index < 0 || index >= itemCount(cumulative)) return 0;
  const uint32_t before = index > 0 ? cumulative[index - 1] : 0;
  return cumulative[index] > before ? cumulative[index] - before : 0;
}

bool isHidden(const std::vector<uint8_t>& hidden, const int index) {
  return index >= 0 && index < static_cast<int>(hidden.size()) && hidden[index] != 0;
}

bool active(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden) {
  const uint32_t shown = shownSize(cumulative, hidden);
  return shown > 0 && shown < rawSize(cumulative);
}

uint32_t visibleSize(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden) {
  const uint32_t shown = shownSize(cumulative, hidden);
  return shown > 0 ? shown : rawSize(cumulative);
}

uint32_t visibleBytesAt(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden,
                        const int spineIndex, const float fraction) {
  const int count = itemCount(cumulative);
  if (count == 0 || spineIndex < 0) return 0;
  if (!active(cumulative, hidden)) {
    if (spineIndex >= count) return rawSize(cumulative);
    const uint32_t before = spineIndex > 0 ? cumulative[spineIndex - 1] : 0;
    return before + part(itemSize(cumulative, spineIndex), fraction);
  }
  uint32_t before = 0;
  for (int i = 0; i < spineIndex && i < count; i++) {
    if (!isHidden(hidden, i)) before += itemSize(cumulative, i);
  }
  if (spineIndex >= count || isHidden(hidden, spineIndex)) return before;
  return before + part(itemSize(cumulative, spineIndex), fraction);
}

float visibleFraction(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden, const int spineIndex,
                      const float fraction) {
  const uint32_t total = visibleSize(cumulative, hidden);
  if (total == 0) return 0.0f;
  const uint32_t at = visibleBytesAt(cumulative, hidden, spineIndex, fraction);
  return clampFraction(static_cast<float>(static_cast<double>(at) / total));
}

Location locate(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden,
                const uint32_t visibleBytes) {
  if (!active(cumulative, hidden)) return rawLocate(cumulative, visibleBytes);
  const int count = itemCount(cumulative);
  uint32_t before = 0;
  int last = -1;
  for (int i = 0; i < count; i++) {
    if (isHidden(hidden, i)) continue;
    const uint32_t size = itemSize(cumulative, i);
    // An empty shown item only wins at byte 0 of the book, like the raw lookup's first item.
    if (before + size >= visibleBytes && (size > 0 || visibleBytes == 0)) {
      if (size == 0 || visibleBytes <= before) return {i, 0.0f};
      return {i, clampFraction(static_cast<float>(static_cast<double>(visibleBytes - before) / size))};
    }
    before += size;
    if (size > 0) last = i;
  }
  // Past the end: active() means at least one shown item has bytes.
  return {last < 0 ? 0 : last, 1.0f};
}

Location locateFraction(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden,
                        const float visibleFraction) {
  const uint32_t total = visibleSize(cumulative, hidden);
  const double bytes = static_cast<double>(total) * static_cast<double>(clampFraction(visibleFraction));
  return locate(cumulative, hidden, std::min(total, static_cast<uint32_t>(bytes + 0.5)));
}

std::string encode(const std::vector<std::string>& hrefs) {
  std::string body;
  uint16_t count = 0;
  for (const auto& href : hrefs) {
    if (href.empty() || href.size() > MAX_HREF_LENGTH) continue;
    if (count >= MAX_HREFS) break;
    body.push_back(static_cast<char>(href.size() & 0xFF));
    body.push_back(static_cast<char>((href.size() >> 8) & 0xFF));
    body += href;
    count++;
  }
  std::string out;
  out.reserve(body.size() + 3);
  out.push_back(static_cast<char>(FILE_VERSION));
  out.push_back(static_cast<char>(count & 0xFF));
  out.push_back(static_cast<char>((count >> 8) & 0xFF));
  out += body;
  return out;
}

bool decode(const uint8_t* data, const size_t size, std::vector<std::string>& hrefs) {
  hrefs.clear();
  if (!data || size < 3 || data[0] != FILE_VERSION) return false;
  const size_t count = data[1] | (static_cast<size_t>(data[2]) << 8);
  if (count > MAX_HREFS) return false;
  size_t pos = 3;
  for (size_t i = 0; i < count; i++) {
    if (size - pos < 2) {
      hrefs.clear();
      return false;
    }
    const size_t length = data[pos] | (static_cast<size_t>(data[pos + 1]) << 8);
    pos += 2;
    if (length == 0 || length > MAX_HREF_LENGTH || size - pos < length) {
      hrefs.clear();
      return false;
    }
    hrefs.emplace_back(reinterpret_cast<const char*>(data + pos), length);
    pos += length;
  }
  return true;
}

}  // namespace hiddenflows
