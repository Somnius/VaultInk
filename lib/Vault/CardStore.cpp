#include "CardStore.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace cardstore {
namespace {

bool parseHex(std::string_view s, uint64_t& out) {
  if (s.size() != 16) return false;
  uint64_t v = 0;
  for (const char c : s) {
    v <<= 4;
    if (c >= '0' && c <= '9') {
      v |= static_cast<uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      v |= static_cast<uint64_t>(c - 'a' + 10);
    } else {
      return false;
    }
  }
  out = v;
  return true;
}

bool parseNumber(std::string_view s, uint32_t max, uint32_t& out) {
  if (s.empty() || s.size() > 10) return false;
  uint64_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  if (v > max) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

std::string toLine(const uint64_t id, const cardsched::CardState& s) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%016llx\t%lu\t%u\t%u\t%u\t%u\t%lu", static_cast<unsigned long long>(id),
           static_cast<unsigned long>(s.due), static_cast<unsigned>(s.interval), static_cast<unsigned>(s.ease),
           static_cast<unsigned>(s.reps), static_cast<unsigned>(s.lapses), static_cast<unsigned long>(s.last));
  return buf;
}

bool fromLine(std::string_view line, Entry& out) {
  std::string_view f[7];
  for (int i = 0; i < 7; ++i) {
    const size_t tab = line.find('\t');
    if (tab == std::string_view::npos) {
      if (i != 6) return false;
      f[i] = line;
    } else {
      if (i == 6) return false;  // too many fields
      f[i] = line.substr(0, tab);
      line.remove_prefix(tab + 1);
    }
  }
  Entry e;
  uint32_t due, interval, ease, reps, lapses, last;
  if (!parseHex(f[0], e.id) || !parseNumber(f[1], UINT32_MAX, due) || !parseNumber(f[2], 65535, interval) ||
      !parseNumber(f[3], 65535, ease) || !parseNumber(f[4], 255, reps) || !parseNumber(f[5], 255, lapses) ||
      !parseNumber(f[6], UINT32_MAX, last)) {
    return false;
  }
  e.state.due = due;
  e.state.interval = static_cast<uint16_t>(interval);
  e.state.ease = static_cast<uint16_t>(ease);
  e.state.reps = static_cast<uint8_t>(reps);
  e.state.lapses = static_cast<uint8_t>(lapses);
  e.state.last = last;
  out = e;
  return true;
}

bool Store::readAll(const std::function<bool(uint64_t)>& wanted, std::vector<Entry>& into) {
  into.clear();
  bytesRead = 0;
  headerOk = false;
  bool first = true;
  const bool opened = fs.lines(path, [&](std::string_view line) {
    bytesRead += line.size() + 1;
    if (first) {
      first = false;
      headerOk = line == HEADER;
      return headerOk;  // another version: read nothing
    }
    Entry e;
    if (!fromLine(line, e)) return true;  // a torn last line or junk: skip it
    if (wanted && !wanted(e.id)) return true;
    into.push_back(e);  // sorted once at the end: inserting one by one moves the whole tail each time
    return true;
  });
  if (!opened) {
    into.clear();
    return false;
  }
  // Stable by id keeps the journal's order inside a card, so the last of each run is the newest.
  std::stable_sort(into.begin(), into.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
  size_t out = 0;
  for (size_t i = 0; i < into.size(); ++i) {
    if (i + 1 < into.size() && into[i + 1].id == into[i].id) continue;
    into[out++] = into[i];
  }
  into.resize(out);
  return true;
}

bool Store::load(const std::function<bool(uint64_t)>& wanted) {
  readAll(wanted, entries);  // a missing file is just an empty store
  return true;
}

const cardsched::CardState* Store::find(const uint64_t id) const {
  const auto at =
      std::lower_bound(entries.begin(), entries.end(), id, [](const Entry& a, uint64_t v) { return a.id < v; });
  return at != entries.end() && at->id == id ? &at->state : nullptr;
}

bool Store::writeHeader() {
  if (!fs.begin(path)) return false;
  const std::string header = std::string(HEADER) + "\n";
  if (!fs.append(header)) {
    fs.commit(false);
    return false;
  }
  if (!fs.commit(true)) return false;
  headerOk = true;
  return true;
}

bool Store::save(const uint64_t id, const cardsched::CardState& state) {
  if (!headerOk && !writeHeader()) return false;
  const std::string line = toLine(id, state) + "\n";
  if (!fs.appendTo(path, line)) return false;
  auto at = std::lower_bound(entries.begin(), entries.end(), id, [](const Entry& a, uint64_t v) { return a.id < v; });
  if (at != entries.end() && at->id == id) {
    at->state = state;
  } else {
    entries.insert(at, Entry{id, state});
  }
  return true;
}

bool Store::compact(const uint32_t today, const std::function<bool(uint64_t)>& present) {
  std::vector<Entry> all;
  if (!readAll(nullptr, all)) return false;
  std::vector<Entry> keep;
  keep.reserve(all.size());
  for (const Entry& e : all) {
    const bool stale = !e.state.isNew() && e.state.last + FORGET_AFTER_DAYS < today;
    if (stale && !(present && present(e.id))) continue;
    keep.push_back(e);
  }
  if (keep.size() > MAX_CARDS) {
    std::sort(keep.begin(), keep.end(), [](const Entry& a, const Entry& b) { return a.state.last > b.state.last; });
    keep.resize(MAX_CARDS);
    std::sort(keep.begin(), keep.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
  }
  if (!fs.begin(path)) return false;
  bool ok = fs.append(std::string(HEADER) + "\n");
  for (const Entry& e : keep) {
    if (!ok) break;
    ok = fs.append(toLine(e.id, e.state) + "\n");
  }
  if (!fs.commit(ok)) return false;
  if (!ok) return false;
  headerOk = true;
  entries = std::move(keep);
  bytesRead = 0;
  return true;
}

}  // namespace cardstore
