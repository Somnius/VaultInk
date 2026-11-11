#include "Ftp.h"

#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace ftp {

namespace {

char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
bool isDigit(const char c) { return c >= '0' && c <= '9'; }

bool equalsNoCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
  }
  return true;
}

bool parseNumber(const std::string_view s, uint64_t& out) {
  if (s.empty() || s.size() > 19) return false;
  uint64_t v = 0;
  for (const char c : s) {
    if (!isDigit(c)) return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  out = v;
  return true;
}

unsigned number(const std::string_view s) {
  uint64_t v = 0;
  return parseNumber(s, v) && v <= 0xFFFFFFFFu ? static_cast<unsigned>(v) : 0u;
}

struct Token {
  size_t start;
  size_t end;  // one past the last character
};

// The first `max` space-separated words of a line, with their positions.
std::vector<Token> tokens(const std::string_view line, const size_t max) {
  std::vector<Token> out;
  size_t pos = 0;
  while (out.size() < max) {
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size()) break;
    const size_t start = pos;
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') ++pos;
    out.push_back(Token{start, pos});
  }
  return out;
}

std::string_view word(const std::string_view line, const Token& t) { return line.substr(t.start, t.end - t.start); }

unsigned monthOf(const std::string_view s) {
  static const char MONTHS[] = "janfebmaraprmayjunjulaugsepoctnovdec";
  if (s.size() != 3) return 0;
  for (unsigned m = 0; m < 12; ++m) {
    if (lowerAscii(s[0]) == MONTHS[m * 3] && lowerAscii(s[1]) == MONTHS[m * 3 + 1] &&
        lowerAscii(s[2]) == MONTHS[m * 3 + 2]) {
      return m + 1;
    }
  }
  return 0;
}

// The name of a LIST line: everything after one space behind the token at `end`.
std::string_view nameAfter(const std::string_view line, const size_t end) {
  if (end + 1 >= line.size()) return {};
  return line.substr(end + 1);
}

bool parseUnixLine(const std::string_view line, remotefiles::Entry& out) {
  const char type = line.front();
  if (type != '-' && type != 'd' && type != 'l') return false;
  const std::vector<Token> t = tokens(line, 9);
  if (t.size() < 6 || t[0].end - t[0].start < 10) return false;
  // Some servers leave the group out, so find the date instead of counting columns:
  // "<size> <Mon> <day> <hh:mm or year>".
  for (size_t i = 3; i + 2 < t.size(); ++i) {
    const unsigned month = monthOf(word(line, t[i]));
    if (!month) continue;
    uint64_t day = 0;
    uint64_t size = 0;
    if (!parseNumber(word(line, t[i + 1]), day) || day < 1 || day > 31) continue;
    if (!parseNumber(word(line, t[i - 1]), size)) continue;
    const std::string_view when = word(line, t[i + 2]);
    uint64_t year = 0;
    const bool isYear = when.size() == 4 && parseNumber(when, year);
    const bool isTime = (when.size() == 5 || when.size() == 4) && when.find(':') != std::string_view::npos;
    if (!isYear && !isTime) continue;

    std::string_view name = nameAfter(line, t[i + 2].end);
    if (type == 'l') {
      const size_t arrow = name.find(" -> ");
      if (arrow != std::string_view::npos) name = name.substr(0, arrow);
    }
    if (name.empty() || name == "." || name == "..") return false;
    out = remotefiles::Entry{};
    out.name = std::string(name);
    out.isDir = type == 'd';
    out.size = out.isDir ? 0 : size;
    // A line with a time has no year, so its date is left unknown.
    out.mtime =
        isYear ? remotefiles::epochUtc(static_cast<unsigned>(year), month, static_cast<unsigned>(day), 0, 0, 0) : 0;
    return true;
  }
  return false;
}

// "01-12-26  10:00AM       <DIR>          name" (also a four-digit year).
bool parseDosLine(const std::string_view line, remotefiles::Entry& out) {
  const std::vector<Token> t = tokens(line, 3);
  if (t.size() < 3) return false;
  const std::string_view date = word(line, t[0]);
  const std::string_view time = word(line, t[1]);
  const std::string_view what = word(line, t[2]);
  if ((date.size() != 8 && date.size() != 10) || date[2] != '-' || date[5] != '-') return false;
  if (time.size() != 7 || time[2] != ':') return false;
  const unsigned month = number(date.substr(0, 2));
  const unsigned day = number(date.substr(3, 2));
  unsigned year = number(date.substr(6));
  if (date.size() == 8) year += year < 70 ? 2000 : 1900;
  unsigned hour = number(time.substr(0, 2));
  const unsigned minute = number(time.substr(3, 2));
  const bool pm = lowerAscii(time[5]) == 'p';
  if (lowerAscii(time[5]) != 'a' && !pm) return false;
  if (hour == 12) hour = 0;
  if (pm) hour += 12;

  const bool isDir = equalsNoCase(what, "<DIR>");
  uint64_t size = 0;
  if (!isDir && !parseNumber(what, size)) return false;
  // The columns are padded with spaces (a folder's name sits far behind "<DIR>").
  std::string_view name = nameAfter(line, t[2].end);
  while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
  if (name.empty() || name == "." || name == "..") return false;
  out = remotefiles::Entry{};
  out.name = std::string(name);
  out.isDir = isDir;
  out.size = size;
  out.mtime = remotefiles::epochUtc(year, month, day, hour, minute, 0);
  return true;
}

// Finds the next run of digits at or after `pos`.
bool nextNumber(const std::string_view s, size_t& pos, unsigned& out) {
  while (pos < s.size() && !isDigit(s[pos])) ++pos;
  if (pos >= s.size()) return false;
  unsigned v = 0;
  size_t n = 0;
  while (pos < s.size() && isDigit(s[pos])) {
    if (++n > 5) return false;
    v = v * 10 + static_cast<unsigned>(s[pos] - '0');
    ++pos;
  }
  out = v;
  return true;
}

bool lineBreakFree(const std::string_view s) {
  for (const char c : s) {
    if (c == '\r' || c == '\n' || c == '\0') return false;
  }
  return true;
}

}  // namespace

bool parseUrl(const std::string_view url, Url& out) {
  std::string_view rest = url;
  while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
  while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) rest.remove_suffix(1);
  const size_t schemeEnd = rest.find("://");
  if (schemeEnd != std::string_view::npos) {
    if (!equalsNoCase(rest.substr(0, schemeEnd), "ftp")) return false;
    rest = rest.substr(schemeEnd + 3);
  }
  const size_t slash = rest.find('/');
  const std::string_view authority = rest.substr(0, slash);
  if (authority.empty() || authority.find_first_of("@[]") != std::string_view::npos) return false;

  Url parsed;
  std::string_view host = authority;
  const size_t colon = authority.find(':');
  if (colon != std::string_view::npos) {
    host = authority.substr(0, colon);
    uint64_t port = 0;
    if (!parseNumber(authority.substr(colon + 1), port) || port == 0 || port > 65535) return false;
    parsed.port = static_cast<uint16_t>(port);
  }
  if (host.empty()) return false;
  for (const char c : host) {
    const char l = lowerAscii(c);
    if (!((l >= 'a' && l <= 'z') || isDigit(l) || l == '.' || l == '-' || l == '_')) return false;
    parsed.host += l;
  }
  if (slash != std::string_view::npos) {
    const std::string_view path = rest.substr(slash);
    if (!lineBreakFree(path) || !remotefiles::normalizePath(path, parsed.path)) return false;
  }
  out = std::move(parsed);
  return true;
}

std::string command(const std::string_view verb, const std::string_view arg) {
  if (verb.empty() || !lineBreakFree(arg)) return {};
  for (const char c : verb) {
    if (c < 'A' || c > 'Z') return {};
  }
  std::string out(verb);
  if (!arg.empty()) {
    out += ' ';
    out.append(arg);
  }
  out += "\r\n";
  return out;
}

void ReplyParser::reset() {
  reply_ = Reply{};
  lines_ = 0;
  open_ = 0;
}

ReplyParser::State ReplyParser::feedLine(const std::string_view line) {
  if (++lines_ > MAX_REPLY_LINES) return State::Bad;
  const bool hasCode = line.size() >= 3 && isDigit(line[0]) && isDigit(line[1]) && isDigit(line[2]);
  const int code = hasCode ? (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0') : 0;
  if (open_) {
    // Inside a multi-line reply anything goes until "<the same code><space>".
    if (!hasCode || code != open_ || (line.size() > 3 && line[3] != ' ')) return State::More;
  } else {
    if (!hasCode || code < 100 || code > 599) return State::Bad;
    if (line.size() > 3 && line[3] == '-') {
      open_ = code;
      return State::More;
    }
    if (line.size() > 3 && line[3] != ' ') return State::Bad;
  }
  reply_.code = code;
  reply_.text = line.size() > 4 ? std::string(line.substr(4)) : std::string();
  open_ = 0;
  return State::Done;
}

bool parsePasv(const std::string_view text, uint16_t& port) {
  // Six numbers "h1,h2,h3,h4,p1,p2", with or without the brackets. Skip a leading reply code.
  size_t pos = 0;
  if (text.size() >= 3 && isDigit(text[0]) && isDigit(text[1]) && isDigit(text[2])) pos = 3;
  while (pos < text.size()) {
    size_t p = pos;
    unsigned v[6];
    bool ok = true;
    for (int i = 0; i < 6 && ok; ++i) {
      if (i > 0) {
        if (p >= text.size() || text[p] != ',') {
          ok = false;
          break;
        }
        ++p;
      }
      if (p >= text.size() || !isDigit(text[p])) {
        ok = false;
        break;
      }
      ok = nextNumber(text, p, v[i]) && v[i] <= 255;
    }
    if (ok) {
      const unsigned value = v[4] * 256 + v[5];
      if (value == 0) return false;
      port = static_cast<uint16_t>(value);
      return true;
    }
    // Move on to the next run of digits.
    while (pos < text.size() && isDigit(text[pos])) ++pos;
    while (pos < text.size() && !isDigit(text[pos])) ++pos;
  }
  return false;
}

bool parseEpsv(const std::string_view text, uint16_t& port) {
  const size_t open = text.find('(');
  if (open == std::string_view::npos || open + 5 >= text.size()) return false;
  const char d = text[open + 1];
  if (text[open + 2] != d || text[open + 3] != d) return false;
  size_t pos = open + 4;
  unsigned value = 0;
  if (!isDigit(text[pos]) || !nextNumber(text, pos, value)) return false;
  if (pos + 1 >= text.size() || text[pos] != d || text[pos + 1] != ')') return false;
  if (value == 0 || value > 65535) return false;
  port = static_cast<uint16_t>(value);
  return true;
}

bool parseListLine(const std::string_view rawLine, remotefiles::Entry& out) {
  std::string_view line = rawLine;
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
  if (line.empty()) return false;
  if (isDigit(line.front())) return parseDosLine(line, out);
  return parseUnixLine(line, out);
}

bool parseMlsdLine(const std::string_view rawLine, remotefiles::Entry& out) {
  std::string_view line = rawLine;
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
  const size_t space = line.find(' ');
  if (space == std::string_view::npos || space + 1 >= line.size()) return false;
  std::string_view facts = line.substr(0, space);
  const std::string_view name = line.substr(space + 1);
  if (name == "." || name == "..") return false;

  remotefiles::Entry entry;
  bool haveType = false;
  while (!facts.empty()) {
    size_t end = facts.find(';');
    if (end == std::string_view::npos) end = facts.size();
    const std::string_view fact = facts.substr(0, end);
    facts.remove_prefix(end < facts.size() ? end + 1 : end);
    const size_t eq = fact.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view key = fact.substr(0, eq);
    const std::string_view value = fact.substr(eq + 1);
    if (equalsNoCase(key, "type")) {
      if (equalsNoCase(value, "file")) {
        entry.isDir = false;
      } else if (equalsNoCase(value, "dir")) {
        entry.isDir = true;
      } else {
        return false;  // cdir, pdir, links and other kinds
      }
      haveType = true;
    } else if (equalsNoCase(key, "size")) {
      uint64_t size = 0;
      if (parseNumber(value, size)) entry.size = size;
    } else if (equalsNoCase(key, "modify") && value.size() >= 14) {
      entry.mtime =
          remotefiles::epochUtc(number(value.substr(0, 4)), number(value.substr(4, 2)), number(value.substr(6, 2)),
                                number(value.substr(8, 2)), number(value.substr(10, 2)), number(value.substr(12, 2)));
    }
  }
  if (!haveType) return false;
  if (entry.isDir) entry.size = 0;
  entry.name = std::string(name);
  out = std::move(entry);
  return true;
}

// ---- Session ----

bool Session::readControlLine(std::string& line, Error& error) {
  line.clear();
  for (;;) {
    while (ctrlPos_ < ctrlLen_) {
      const char c = static_cast<char>(ctrlBuf_[ctrlPos_++]);
      if (c == '\n') {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
      }
      if (line.size() >= MAX_LINE_BYTES) {
        error = Error::Protocol;
        return false;
      }
      line += c;
    }
    if (transport_.aborted()) {
      error = Error::Aborted;
      return false;
    }
    const int n = transport_.control().read(ctrlBuf_, sizeof(ctrlBuf_));
    if (n <= 0) {
      error = transport_.aborted() ? Error::Aborted : Error::Network;
      return false;
    }
    ctrlLen_ = static_cast<size_t>(n);
    ctrlPos_ = 0;
  }
}

Error Session::readReply() {
  ReplyParser parser;
  std::string line;
  reply_ = Reply{};
  for (;;) {
    Error error = Error::Ok;
    if (!readControlLine(line, error)) return error;
    switch (parser.feedLine(line)) {
      case ReplyParser::State::Done:
        reply_ = parser.reply();
        return Error::Ok;
      case ReplyParser::State::Bad:
        return Error::Protocol;
      case ReplyParser::State::More:
      default:
        break;
    }
  }
}

Error Session::send(const std::string_view verb, const std::string_view arg) {
  const std::string cmd = command(verb, arg);
  if (cmd.empty()) return Error::BadName;
  if (!transport_.control().write(reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size())) return Error::Network;
  return Error::Ok;
}

Error Session::exchange(const std::string_view verb, const std::string_view arg) {
  const Error sent = send(verb, arg);
  if (sent != Error::Ok) return sent;
  return readReply();
}

Error Session::login(const std::string_view user, const std::string_view password) {
  // The greeting; a 1xx ("wait") may come before the 220.
  for (int i = 0; i < 3; ++i) {
    const Error e = readReply();
    if (e != Error::Ok) return e;
    if (!reply_.preliminary()) break;
  }
  if (reply_.code != 220) return Error::Protocol;

  const bool anonymous = user.empty();
  Error e = exchange("USER", anonymous ? std::string_view("anonymous") : user);
  if (e != Error::Ok) return e;
  if (reply_.code == 331) {
    e = exchange("PASS", anonymous ? std::string_view("vaultink@") : password);
    if (e != Error::Ok) return e;
  }
  if (!reply_.positive()) return Error::Login;

  e = exchange("TYPE", "I");
  if (e != Error::Ok) return e;
  return reply_.positive() ? Error::Ok : Error::Protocol;
}

Error Session::changeDir(const std::string_view dir) {
  const Error e = exchange("CWD", dir.empty() ? std::string_view("/") : dir);
  if (e != Error::Ok) return e;
  return reply_.positive() ? Error::Ok : Error::Refused;
}

Error Session::openPassive(Channel*& data) {
  data = nullptr;
  uint16_t port = 0;
  bool havePort = false;
  if (!epsvUnsupported_) {
    const Error e = exchange("EPSV");
    if (e != Error::Ok) return e;
    havePort = reply_.code == 229 && parseEpsv(reply_.text, port);
    if (!havePort) epsvUnsupported_ = true;
  }
  if (!havePort) {
    const Error e = exchange("PASV");
    if (e != Error::Ok) return e;
    if (reply_.code != 227) return Error::Refused;
    if (!parsePasv(reply_.text, port)) return Error::Protocol;
  }
  data = transport_.openData(port);
  return data ? Error::Ok : Error::Network;
}

Error Session::finishTransfer(Channel* data, const Error transferError, const bool ignoreFinalCode) {
  data->close();
  // The server answers the transfer once the data connection is closed (226, or 426 when we
  // stopped early).
  const Error e = readReply();
  if (transferError != Error::Ok) return transferError;
  if (e != Error::Ok) return e;
  if (!ignoreFinalCode && !reply_.positive()) return Error::Refused;
  return Error::Ok;
}

Error Session::list(const std::string_view dir, remotefiles::Listing& out) {
  Error e = changeDir(dir);
  if (e != Error::Ok) return e;

  const std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[MAX_LINE_BYTES]);
  if (!buf) return Error::NoMemory;

  for (int attempt = 0; attempt < 2; ++attempt) {
    Channel* data = nullptr;
    e = openPassive(data);
    if (e != Error::Ok) return e;
    const bool mlsd = !mlsdUnsupported_;
    e = exchange(mlsd ? "MLSD" : "LIST");
    if (e != Error::Ok) {
      data->close();
      return e;
    }
    if (!reply_.preliminary()) {
      data->close();
      if (mlsd && reply_.code >= 500 && reply_.code <= 504) {
        mlsdUnsupported_ = true;  // an older server: ask again with LIST
        continue;
      }
      return Error::Refused;
    }

    std::string line;
    bool overlong = false;
    Error transfer = Error::Ok;
    const auto takeLine = [&] {
      remotefiles::Entry entry;
      if (!overlong && (mlsd ? parseMlsdLine(line, entry) : parseListLine(line, entry))) out.add(std::move(entry));
      line.clear();
      overlong = false;
    };
    while (!out.truncated) {
      if (transport_.aborted()) {
        transfer = Error::Aborted;
        break;
      }
      const int n = data->read(buf.get(), MAX_LINE_BYTES);
      if (n == 0) break;
      if (n < 0) {
        transfer = transport_.aborted() ? Error::Aborted : Error::Network;
        break;
      }
      for (int i = 0; i < n && !out.truncated; ++i) {
        const char c = static_cast<char>(buf[i]);
        if (c == '\n') {
          takeLine();
        } else if (line.size() >= MAX_LINE_BYTES) {
          overlong = true;
        } else {
          line += c;
        }
      }
    }
    if (transfer == Error::Ok && !out.truncated && !line.empty()) takeLine();  // a last line without a line break
    return finishTransfer(data, transfer, out.truncated);
  }
  return Error::Refused;
}

Error Session::retrieve(const std::string_view dir, const std::string_view name, const Sink& sink) {
  if (name.empty() || !lineBreakFree(name)) return Error::BadName;
  Error e = changeDir(dir);
  if (e != Error::Ok) return e;
  const std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[IO_BUFFER_BYTES]);
  if (!buf) return Error::NoMemory;

  Channel* data = nullptr;
  e = openPassive(data);
  if (e != Error::Ok) return e;
  e = exchange("RETR", name);
  if (e != Error::Ok || !reply_.preliminary()) {
    data->close();
    return e != Error::Ok ? e : Error::Refused;
  }
  Error transfer = Error::Ok;
  for (;;) {
    if (transport_.aborted()) {
      transfer = Error::Aborted;
      break;
    }
    const int n = data->read(buf.get(), IO_BUFFER_BYTES);
    if (n == 0) break;
    if (n < 0) {
      transfer = transport_.aborted() ? Error::Aborted : Error::Network;
      break;
    }
    if (!sink(buf.get(), static_cast<size_t>(n))) {
      transfer = Error::Local;
      break;
    }
  }
  return finishTransfer(data, transfer, false);
}

Error Session::store(const std::string_view dir, const std::string_view name, const Source& source) {
  if (name.empty() || !lineBreakFree(name)) return Error::BadName;
  Error e = changeDir(dir);
  if (e != Error::Ok) return e;
  const std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[IO_BUFFER_BYTES]);
  if (!buf) return Error::NoMemory;

  const std::string part = std::string(name) + ".part";
  Channel* data = nullptr;
  e = openPassive(data);
  if (e != Error::Ok) return e;
  e = exchange("STOR", part);
  if (e != Error::Ok || !reply_.preliminary()) {
    data->close();
    return e != Error::Ok ? e : Error::Refused;
  }
  Error transfer = Error::Ok;
  for (;;) {
    if (transport_.aborted()) {
      transfer = Error::Aborted;
      break;
    }
    const int n = source(buf.get(), IO_BUFFER_BYTES);
    if (n == 0) break;
    if (n < 0) {
      transfer = Error::Local;
      break;
    }
    if (!data->write(buf.get(), static_cast<size_t>(n))) {
      transfer = transport_.aborted() ? Error::Aborted : Error::Network;
      break;
    }
  }
  e = finishTransfer(data, transfer, false);
  if (e != Error::Ok) {
    // Leave no half file behind when the control connection still answers.
    // (reply_ is empty when the control connection gave no answer to the transfer.)
    const Reply failed = reply_;
    if (failed.code != 0 && e != Error::Protocol) exchange("DELE", part);
    reply_ = failed;
    return e;
  }

  for (int attempt = 0; attempt < 2; ++attempt) {
    e = exchange("RNFR", part);
    if (e != Error::Ok) return e;
    if (!reply_.intermediate()) break;
    e = exchange("RNTO", name);
    if (e != Error::Ok) return e;
    if (reply_.positive()) return Error::Ok;
    if (attempt == 0) {
      // Some servers do not rename over a file that is there: the new copy is whole and on the
      // server, so the old one can go.
      const Reply refused = reply_;
      e = exchange("DELE", name);
      if (e != Error::Ok) return e;
      if (!reply_.positive()) {
        reply_ = refused;
        break;
      }
    }
  }
  const Reply refused = reply_;
  if (exchange("DELE", part) != Error::Ok) return Error::Network;
  reply_ = refused;
  return Error::Refused;
}

Error Session::makeDir(const std::string_view dir, const std::string_view name) {
  if (name.empty() || !lineBreakFree(name)) return Error::BadName;
  Error e = changeDir(dir);
  if (e != Error::Ok) return e;
  e = exchange("MKD", name);
  if (e != Error::Ok) return e;
  return reply_.positive() ? Error::Ok : Error::Refused;
}

void Session::quit() { send("QUIT"); }

}  // namespace ftp
