#include "SshTerminalActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/SshDevice.h"

#if defined(VAULTINK_SSH) && VAULTINK_SSH && !defined(SIMULATOR)

#include <BlePageTurner.h>
#include <EpdFontData.h>
#include <SshAddress.h>
#include <SshInput.h>
#include <SshKey.h>
#include <SshSession.h>
#include <SshTerminal.h>
#include <TextInput.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#include "HapticFeedback.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "network/SshWifiSocket.h"
#include "network/WifiPowerSaveGuard.h"

namespace {

constexpr uint16_t SCROLLBACK_LINES = 500;
// Opening the TCP connection (WiFiClient cannot be interrupted, so this one is short), the longest
// wait for one answer during the key exchange and the sign-in, and the limit for all of it together
// (sshclient::Login::deadlineMs), which holds however the server spaces its answers.
constexpr uint32_t TCP_CONNECT_TIMEOUT_MS = 8000;
constexpr uint32_t ANSWER_TIMEOUT_MS = 15000;
constexpr uint32_t SIGN_IN_DEADLINE_MS = 30000;
// A typed key that the server does not take within this ends the session with a message: the
// screen never hangs on a dead connection for longer.
constexpr uint32_t KEY_SEND_TIMEOUT_MS = 3000;
// Per loop pass: at most this much output is taken from the server, so keys are still read while
// a program floods the screen (the rest waits in the TCP window).
constexpr int READS_PER_LOOP = 4;
constexpr size_t READ_CHUNK = 1024;
constexpr size_t LINE_MAX_BYTES = 200;
constexpr int KEY_ROW_HEIGHT = 46;
constexpr unsigned long NOTE_MS = 6000;

void* psramAlloc(const size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void psramFree(void* block) { heap_caps_free(block); }

enum class Key : uint8_t {
  Keyboard,
  Enter,
  Esc,
  Tab,
  Ctrl,
  CtrlC,
  PageUp,
  PageDown,
  Left,
  Down,
  Up,
  Right,
  Disconnect
};

struct KeyBox {
  Key key;
  const char* label;  // not translated: these are the names printed on a keyboard
  uint8_t weight;     // width in units of the row
};
constexpr KeyBox ROW_ONE[] = {{Key::Keyboard, "Kbd", 3}, {Key::Enter, "Enter", 3},  {Key::Esc, "Esc", 2},
                              {Key::Tab, "Tab", 2},      {Key::Ctrl, "Ctrl", 2},    {Key::CtrlC, "^C", 2},
                              {Key::PageUp, "PgUp", 2},  {Key::PageDown, "PgDn", 2}};
constexpr KeyBox ROW_TWO[] = {
    {Key::Left, "<", 2}, {Key::Down, "v", 2}, {Key::Up, "^", 2}, {Key::Right, ">", 2}, {Key::Disconnect, nullptr, 10}};

template <size_t N>
int totalWeight(const KeyBox (&row)[N]) {
  int total = 0;
  for (const KeyBox& box : row) total += box.weight;
  return total;
}

}  // namespace

struct SshTerminalActivity::Impl {
  enum class State : uint8_t { Start, Connecting, Running, Message };

  SshWifiSocket socket;
  sshclient::Session session{socket};
  sshterm::Terminal term;
  sshterm::RedrawPolicy policy{500, 120};
  sshaddr::Address address;

  State state = State::Start;
  std::string message;  // State::Message: why there is no session
  std::string header;   // "Connected to host as user"
  bool connectPending = false;
  bool cancelConnect = false;  // Back (or a tap) while "Connecting..." is up
  bool wifiUsed = false;
  bool fullRefresh = true;
  bool ctrlLatch = false;
  uint16_t viewOffset = 0;  // lines scrolled back; 0 is the live screen
  uint32_t altSeen = 0;
  unsigned long noteUntil = 0;  // the "no full-screen programs" note is up until then

  // The layout, worked out once in onEnter().
  int cellW = 8;
  int cellH = 17;
  int ascender = 13;
  int gridLeft = 0;
  int gridTop = 0;
  int headerTop = 0;
  int keysTop = 0;
  uint16_t cols = 80;
  uint16_t rows = 24;
};

SshTerminalActivity::SshTerminalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RemoteServer server)
    : Activity("SshTerminal", renderer, mappedInput), impl_(makeUniqueNoThrow<Impl>()), server_(std::move(server)) {}

SshTerminalActivity::~SshTerminalActivity() { sshkey::wipe(server_.password.data(), server_.password.size()); }

bool SshTerminalActivity::preventAutoSleep() {
  return impl_ && (impl_->state == Impl::State::Connecting || impl_->connectPending);
}

void SshTerminalActivity::showMessage(const char* text) {
  {
    RenderLock lock(*this);
    impl_->state = Impl::State::Message;
    impl_->message = text ? text : "";
    impl_->fullRefresh = true;
  }
  sshkey::wipe(server_.password.data(), server_.password.size());
  server_.password.clear();
  bleturner::setTextEntry(false);
  requestUpdate();
}

void SshTerminalActivity::onEnter() {
  Activity::onEnter();
  if (!impl_) {
    finish();
    return;
  }
  Impl& s = *impl_;

  // The grid: the small built-in font, one glyph per cell (a glyph wider than the cell is drawn
  // narrower). 10-pixel cells give 80 columns on the 800-pixel side; 8-pixel cells give 60 on the
  // 480-pixel side.
  const auto metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  s.cellW = width >= 800 ? 10 : 8;
  s.cellH = renderer.getLineHeight(SMALL_FONT_ID);
  s.ascender = renderer.getFontAscenderSize(SMALL_FONT_ID);
  s.headerTop = metrics.topPadding;
  s.gridTop = s.headerTop + s.cellH + 6;
  const int keysHeight = mappedInput.hasTouch() ? KEY_ROW_HEIGHT * 2 + 4 : metrics.buttonHintsHeight;
  s.keysTop = height - keysHeight;
  s.cols = static_cast<uint16_t>(std::clamp(width / s.cellW, 20, static_cast<int>(sshterm::Terminal::MAX_COLS)));
  s.rows = static_cast<uint16_t>(
      std::clamp((s.keysTop - s.gridTop - 2) / s.cellH, 4, static_cast<int>(sshterm::Terminal::MAX_ROWS)));
  s.gridLeft = (width - s.cols * s.cellW) / 2;

  if (!sshaddr::parse(server_.url, s.address) || server_.username.empty()) {
    showMessage(tr(STR_SSH_ERR_NO_USER));
    return;
  }
  if (!s.term.begin(s.cols, s.rows, SCROLLBACK_LINES, sshterm::Memory{psramAlloc, psramFree})) {
    showMessage(tr(STR_MEMORY_ERROR));
    return;
  }
  if (server_.keyAuth) {
    if (!sshdevice::hasKey()) {
      showMessage(tr(STR_SSH_KEY_FAILED));
      return;
    }
    startWifi();
  } else {
    askPassword();
  }
}

void SshTerminalActivity::askPassword() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD), std::string(),
                                                           remoteserver::MAX_FIELD_BYTES, InputType::Password);
  if (!keyboard) {
    showMessage(tr(STR_MEMORY_ERROR));
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      finish();
      return;
    }
    server_.password = std::get<KeyboardResult>(result.data).text;
    // The keyboard's copy of the password is not needed past this line.
    sshkey::wipeConst(std::get<KeyboardResult>(result.data).text);
    if (server_.password.empty()) {
      finish();
      return;
    }
    startWifi();
  });
}

void SshTerminalActivity::startWifi() {
  Impl& s = *impl_;
  // Past this point the radio may have been on, so the way out switches it off (onExit()).
  s.wifiUsed = true;
  if (WiFi.status() == WL_CONNECTED) {
    s.connectPending = true;
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    showMessage(tr(STR_MEMORY_ERROR));
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      showMessage(tr(STR_WIFI_CONN_FAILED));
      return;
    }
    impl_->connectPending = true;
  });
}

// Blocks for the key exchange and the sign-in (a few seconds), with "Connecting..." on the screen.
void SshTerminalActivity::connectNow() {
  Impl& s = *impl_;
  {
    RenderLock lock(*this);
    s.state = Impl::State::Connecting;
    s.fullRefresh = true;
  }
  requestUpdateAndWait();

  if (!sshdevice::heapAllowsConnection()) {
    showMessage(tr(STR_MEMORY_ERROR));
    return;
  }
  sshclient::Login login;
  login.host = s.address.host;
  login.port = s.address.port;
  login.user = server_.username;
  login.timeoutMs = ANSWER_TIMEOUT_MS;
  login.deadlineMs = SIGN_IN_DEADLINE_MS;
  // Back, or a tap, while this blocks: the socket's idle hook reads the buttons between the
  // server's answers and the session stops at the flag, like a cancelled file transfer.
  s.cancelConnect = false;
  login.cancel = &s.cancelConnect;
  if (server_.keyAuth) {
    login.useKey = true;
    if (!sshdevice::loadKey(login.keySeed, login.keyPublic)) {
      showMessage(tr(STR_SSH_KEY_FAILED));
      return;
    }
  } else {
    login.password = server_.password;
  }
  // The pinned keys must be readable and the device's own. If not: no connection, no question
  // about trusting a key, and nothing written; "Forget all host keys" is the way out.
  const sshdevice::HostKeys pins = sshdevice::knownHosts(login.knownHosts);
  if (pins != sshdevice::HostKeys::Ok) {
    sshkey::wipe(login.keySeed, sizeof(login.keySeed));
    sshkey::wipe(login.password.data(), login.password.size());
    showMessage(pins == sshdevice::HostKeys::Changed ? tr(STR_SSH_KNOWN_HOSTS_CHANGED)
                                                     : tr(STR_SSH_KNOWN_HOSTS_UNREADABLE));
    return;
  }

  sshclient::Error error = sshclient::Error::Network;
  {
    // Wi-Fi power save is off for the key exchange and the sign-in only (it adds seconds to them);
    // an open, idle session runs with the radio's normal power save.
    WifiPowerSaveGuard powerSave;
    s.socket.setIdleHook(
        [](void* ctx) {
          auto* self = static_cast<SshTerminalActivity*>(ctx);
          int x = 0;
          int y = 0;
          self->mappedInput.update(true);
          if (self->mappedInput.wasReleased(MappedInputManager::Button::Back) ||
              self->mappedInput.wasScreenTapped(x, y)) {
            self->impl_->cancelConnect = true;
          }
        },
        this);
    if (s.socket.connect(s.address.host, s.address.port, TCP_CONNECT_TIMEOUT_MS)) {
      error = s.session.openShell(login, s.cols, s.rows);  // wipes the secrets in `login`
    } else {
      sshkey::wipe(login.keySeed, sizeof(login.keySeed));
      sshkey::wipe(login.password.data(), login.password.size());
    }
    s.socket.setIdleHook(nullptr, nullptr);
  }
  s.session.setShellWriteTimeout(KEY_SEND_TIMEOUT_MS);
  if (error != sshclient::Error::Ok) {
    LOG_ERR("SSH", "Connection failed: %d (wolfSSH %d)", static_cast<int>(error), s.session.lastLibraryError());
    s.socket.stop();
  }

  switch (error) {
    case sshclient::Error::Ok:
      break;
    case sshclient::Error::HostKeyUnknown:
      askHostKey();  // keeps the typed password for the second try
      return;
    case sshclient::Error::HostKeyChanged:
      // Never accepted from here: the way on is "Forget the server's key" on the server's page,
      // after checking with whoever runs the server.
      showMessage(tr(STR_SSH_HOST_KEY_CHANGED));
      return;
    case sshclient::Error::Aborted:
      finish();  // cancelled by the user: back to the server's page (onExit wipes the password)
      return;
    case sshclient::Error::Auth:
      showMessage(tr(STR_REMOTE_ERR_AUTH));
      return;
    case sshclient::Error::LowMemory:
      showMessage(tr(STR_MEMORY_ERROR));
      return;
    case sshclient::Error::NoCommonCipher:
      showMessage(tr(STR_SSH_ERR_CIPHER));
      return;
    case sshclient::Error::Refused:
      showMessage(tr(STR_REMOTE_ERR_REFUSED));
      return;
    default:
      showMessage(tr(STR_REMOTE_ERR_NETWORK));
      return;
  }

  // Signed in: the typed password has done its work.
  sshkey::wipe(server_.password.data(), server_.password.size());
  server_.password.clear();
  {
    RenderLock lock(*this);
    s.header = std::string(tr(STR_SSH_CONNECTED_TO)) + " " + sshaddr::display(s.address) + " " + tr(STR_SSH_AS) + " " +
               server_.username;
    s.state = Impl::State::Running;
    s.fullRefresh = true;
  }
  enableTyping();
  s.policy.noteOther();
}

void SshTerminalActivity::askHostKey() {
  Impl& s = *impl_;
  const sshclient::HostKey key = s.session.hostKey();
  const std::string body =
      sshaddr::display(s.address) + "\n" + key.type + "\n" + key.fingerprint + "\n\n" + tr(STR_SSH_HOST_KEY_BODY);
  auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SSH_HOST_KEY_TITLE), body,
                                                        std::string(), tr(STR_SSH_TRUST));
  if (!dialog) {
    showMessage(tr(STR_MEMORY_ERROR));
    return;
  }
  // Cancel is the preselected answer.
  startActivityForResult(std::move(dialog), [this, key](const ActivityResult& result) {
    if (result.isCancelled) {
      finish();
      return;
    }
    if (!sshdevice::trustHost(impl_->address.host, impl_->address.port, key.blob)) {
      showMessage(tr(STR_REMOTE_ERR_FILE));
      return;
    }
    // Again from the start: the server must now show the key that was just pinned.
    impl_->connectPending = true;
  });
}

// A Bluetooth keyboard types here like in a text field, with the terminal's extra keys.
void SshTerminalActivity::enableTyping() {
  textinput::global().clear();
  bleturner::setTextEntry(true);
  bleturner::setTerminalKeys(true);
}

void SshTerminalActivity::onExit() {
  if (impl_) {
    Impl& s = *impl_;
    s.session.close();
    s.socket.stop();
  }
  bleturner::setTextEntry(false);
  textinput::global().clear();
  sshkey::wipe(server_.password.data(), server_.password.size());
  server_.password.clear();
  Activity::onExit();
  if (impl_ && impl_->wifiUsed) {
    // The radio goes off with the session, on every way out (Disconnect, sleep, USB Drive). On the
    // way to sleep the restart below does nothing (silentRestartTo checks for it); otherwise it
    // gives the settings screen a clean heap, as after the other network screens.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(30);
    silentRestartToSettings();
  }
}

void SshTerminalActivity::sendBytes(const std::string& bytes) {
  Impl& s = *impl_;
  if (bytes.empty() || s.state != Impl::State::Running) return;
  if (s.viewOffset != 0) {
    RenderLock lock(*this);
    s.viewOffset = 0;  // typing goes back to the live screen
    s.policy.noteOther();
  }
  // Waits at most KEY_SEND_TIMEOUT_MS; a server that does not take the bytes by then is treated as
  // gone and the session is dropped with a message.
  if (s.session.writeShell(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()) != sshclient::Error::Ok &&
      !s.session.connected()) {
    s.socket.stop();
    showMessage(s.session.hostKeyChangedMidSession() ? tr(STR_SSH_HOST_KEY_CHANGED_NOW) : tr(STR_SSH_CLOSED));
  }
}

void SshTerminalActivity::scrollView(const int pages) {
  Impl& s = *impl_;
  const int step = std::max(1, s.rows - 2);
  const int wanted =
      std::clamp(static_cast<int>(s.viewOffset) + pages * step, 0, static_cast<int>(s.term.scrollbackLines()));
  if (wanted == s.viewOffset) return;
  {
    RenderLock lock(*this);
    s.viewOffset = static_cast<uint16_t>(wanted);
  }
  s.policy.noteOther();
}

void SshTerminalActivity::openKeyboard() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SSH_TYPE_LINE), std::string(),
                                                           LINE_MAX_BYTES, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    // The keyboard screen switched Bluetooth typing off when it closed.
    if (impl_->state == Impl::State::Running) enableTyping();
    impl_->fullRefresh = true;
    impl_->policy.noteOther();
    if (result.isCancelled) return;
    const std::string text = std::get<KeyboardResult>(result.data).text;
    // A line may be a password typed at a prompt (sudo): the keyboard's copy is cleared.
    sshkey::wipeConst(std::get<KeyboardResult>(result.data).text);
    // With Ctrl latched the line's first character becomes the control character and no Enter is
    // added ("c" sends Ctrl-C); otherwise the line is sent with Enter.
    const bool ctrl = impl_->ctrlLatch;
    impl_->ctrlLatch = false;
    sendBytes(sshinput::bytesForLine(text, ctrl, !ctrl));
  });
}

void SshTerminalActivity::askDisconnect() {
  auto dialog =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SSH_DISCONNECT),
                                              tr(STR_SSH_DISCONNECT_BODY), std::string(), tr(STR_SSH_DISCONNECT));
  if (!dialog) {
    finish();
    return;
  }
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      finish();
      return;
    }
    if (impl_->state == Impl::State::Running) enableTyping();
    impl_->fullRefresh = true;
    impl_->policy.noteOther();
  });
}

void SshTerminalActivity::pressKey(const uint8_t keyValue) {
  Impl& s = *impl_;
  const Key key = static_cast<Key>(keyValue);
  const bool app = s.term.applicationCursorKeys();
  const auto special = [&](const textinput::Special sp) {
    sendBytes(sshinput::bytesFor(textinput::Event{0, sp}, false, app));
  };
  switch (key) {
    case Key::Keyboard:
      openKeyboard();
      break;
    case Key::Enter:
      special(textinput::Special::Enter);
      break;
    case Key::Esc:
      special(textinput::Special::Escape);
      break;
    case Key::Tab:
      special(textinput::Special::Tab);
      break;
    case Key::Ctrl: {
      RenderLock lock(*this);
      s.ctrlLatch = !s.ctrlLatch;
      s.policy.noteOther();
      break;
    }
    case Key::CtrlC:
      sendBytes(std::string(1, '\x03'));
      break;
    case Key::PageUp:
      scrollView(1);
      break;
    case Key::PageDown:
      scrollView(-1);
      break;
    case Key::Left:
      special(textinput::Special::Left);
      break;
    case Key::Down:
      special(textinput::Special::Down);
      break;
    case Key::Up:
      special(textinput::Special::Up);
      break;
    case Key::Right:
      special(textinput::Special::Right);
      break;
    case Key::Disconnect:
      askDisconnect();
      break;
  }
}

bool SshTerminalActivity::keyAt(const int x, const int y, uint8_t& keyOut) const {
  const Impl& s = *impl_;
  if (!mappedInput.hasTouch() || y < s.keysTop) return false;
  const int width = renderer.getScreenWidth();
  const auto find = [&](const KeyBox* row, const size_t count, const int total) {
    int left = 0;
    int used = 0;
    for (size_t i = 0; i < count; ++i) {
      used += row[i].weight;
      const int right = width * used / total;
      if (x >= left && x < right) {
        keyOut = static_cast<uint8_t>(row[i].key);
        return true;
      }
      left = right;
    }
    return false;
  };
  if (y < s.keysTop + KEY_ROW_HEIGHT + 2) return find(ROW_ONE, std::size(ROW_ONE), totalWeight(ROW_ONE));
  return find(ROW_TWO, std::size(ROW_TWO), totalWeight(ROW_TWO));
}

void SshTerminalActivity::loop() {
  if (!impl_) return;
  Impl& s = *impl_;

  if (s.connectPending) {
    s.connectPending = false;
    connectNow();
    return;
  }

  int x = 0;
  int y = 0;
  if (s.state == Impl::State::Message) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      finish();
    }
    return;
  }
  if (s.state != Impl::State::Running) return;

  // --- keys ---
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    askDisconnect();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openKeyboard();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    scrollView(1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
             mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    scrollView(-1);
  }
  uint8_t key = 0;
  if (mappedInput.wasScreenTapped(x, y) && keyAt(x, y, key)) {
    haptic_feedback::touchAction();
    pressKey(key);
    if (s.state != Impl::State::Running) return;
  }

  // --- the Bluetooth keyboard ---
  textinput::Event event;
  std::string typed;
  for (int guard = 0; guard < 32 && textinput::global().pop(event); ++guard) {
    if (event.special == textinput::Special::PageUp) {
      scrollView(1);
    } else if (event.special == textinput::Special::PageDown) {
      scrollView(-1);
    } else {
      typed += sshinput::bytesFor(event, s.ctrlLatch, s.term.applicationCursorKeys());
      if (s.ctrlLatch) {
        RenderLock lock(*this);
        s.ctrlLatch = false;
        s.policy.noteOther();
      }
    }
  }
  sendBytes(typed);
  if (s.state != Impl::State::Running) return;

  // --- output from the server ---
  uint8_t buffer[READ_CHUNK];
  bool ended = false;
  for (int i = 0; i < READS_PER_LOOP; ++i) {
    const int n = s.session.readShell(buffer, sizeof(buffer));
    if (n < 0) {
      ended = true;
      break;
    }
    if (n == 0) break;
    std::string reply;
    {
      RenderLock lock(*this);  // the render task reads the cells
      s.term.feed(buffer, static_cast<size_t>(n));
      s.policy.note(s.term.takeChanges());
      reply = s.term.takeReply();
      if (s.term.alternateScreenRequests() != s.altSeen) {
        s.altSeen = s.term.alternateScreenRequests();
        s.noteUntil = millis() + NOTE_MS;
      }
    }
    if (!reply.empty()) s.session.writeShell(reinterpret_cast<const uint8_t*>(reply.data()), reply.size());
  }
  if (ended) {
    // A later key exchange that showed another host key ends the session with its own words.
    showMessage(s.session.hostKeyChangedMidSession() ? tr(STR_SSH_HOST_KEY_CHANGED_NOW) : tr(STR_SSH_CLOSED));
    return;
  }
  if (s.noteUntil != 0 && static_cast<long>(millis() - s.noteUntil) >= 0) {
    s.noteUntil = 0;
    s.policy.noteOther();
  }

  // --- the panel ---
  const sshterm::RedrawPolicy::Action action = s.policy.poll(millis());
  if (action != sshterm::RedrawPolicy::Action::None) {
    if (action == sshterm::RedrawPolicy::Action::Full) s.fullRefresh = true;
    requestUpdate();
  }
}

void SshTerminalActivity::drawGrid() {
  Impl& s = *impl_;
  for (uint16_t row = 0; row < s.rows; ++row) {
    const sshterm::Cell* cells = s.term.line(row, s.viewOffset);
    if (!cells) continue;
    const int top = s.gridTop + row * s.cellH;
    const int baseline = top + s.ascender;
    for (uint16_t col = 0; col < s.cols; ++col) {
      const sshterm::Cell& cell = cells[col];
      const int x = s.gridLeft + col * s.cellW;
      if (cell.ch != ' ') {
        uint32_t cp = cell.ch;
        int32_t advance = 0;
        int glyphTop = 0;
        if (!renderer.getCodepointMetrics(SMALL_FONT_ID, cp, EpdFontFamily::REGULAR, advance, glyphTop)) {
          cp = '?';
          if (!renderer.getCodepointMetrics(SMALL_FONT_ID, cp, EpdFontFamily::REGULAR, advance, glyphTop)) continue;
        }
        const int natural = std::max(1, fp4::toPixel(advance));
        const int scale = natural > s.cellW ? s.cellW * 256 / natural : 256;
        const int offset = (s.cellW - natural * scale / 256) / 2;
        renderer.drawScaledCodepoint(SMALL_FONT_ID, cp, EpdFontFamily::REGULAR, x + offset, baseline, scale);
        // Bold: the glyph once more, one pixel to the right.
        if (cell.attr & sshterm::ATTR_BOLD)
          renderer.drawScaledCodepoint(SMALL_FONT_ID, cp, EpdFontFamily::REGULAR, x + offset + 1, baseline, scale);
      }
      // Underline and inverse are both drawn as an underline: the glyphs can only be drawn black.
      if (cell.attr & (sshterm::ATTR_UNDERLINE | sshterm::ATTR_INVERSE))
        renderer.drawLine(x, top + s.cellH - 2, x + s.cellW - 1, top + s.cellH - 2);
    }
  }
  if (s.viewOffset == 0 && s.term.cursorVisible()) {
    const int x = s.gridLeft + s.term.cursorCol() * s.cellW;
    const int top = s.gridTop + s.term.cursorRow() * s.cellH;
    renderer.fillRect(x, top + s.cellH - 3, s.cellW, 3);
  }
}

void SshTerminalActivity::drawKeys() {
  Impl& s = *impl_;
  const int width = renderer.getScreenWidth();
  const auto drawRow = [&](const KeyBox* row, const size_t count, const int total, const int top) {
    int left = 0;
    int used = 0;
    for (size_t i = 0; i < count; ++i) {
      used += row[i].weight;
      const int right = width * used / total;
      const char* label = row[i].label ? row[i].label : tr(STR_SSH_DISCONNECT);
      const bool lit = row[i].key == Key::Ctrl && s.ctrlLatch;
      renderer.drawRect(left + 1, top, right - left - 2, KEY_ROW_HEIGHT - 2);
      if (lit) renderer.fillRect(left + 1, top, right - left - 2, KEY_ROW_HEIGHT - 2);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
      renderer.drawText(UI_10_FONT_ID, left + (right - left - textWidth) / 2,
                        top + (KEY_ROW_HEIGHT - renderer.getLineHeight(UI_10_FONT_ID)) / 2 - 1, label, !lit);
      left = right;
    }
  };
  drawRow(ROW_ONE, std::size(ROW_ONE), totalWeight(ROW_ONE), s.keysTop + 2);
  drawRow(ROW_TWO, std::size(ROW_TWO), totalWeight(ROW_TWO), s.keysTop + KEY_ROW_HEIGHT + 4);
}

void SshTerminalActivity::render(RenderLock&&) {
  if (!impl_) return;
  Impl& s = *impl_;
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  if (s.state != Impl::State::Running) {
    const char* text = s.state == Impl::State::Message ? s.message.c_str() : tr(STR_SSH_CONNECTING);
    int y = height / 2 - renderer.getLineHeight(UI_12_FONT_ID);
    for (const std::string& line : renderer.wrappedText(UI_12_FONT_ID, text, width - 40, 6)) {
      renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }
    // Back leaves a message, and cancels a connection attempt.
    const auto labels =
        mappedInput.mapLabels(s.state == Impl::State::Message ? tr(STR_BACK) : tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    s.fullRefresh = false;
    return;
  }

  // The header line: who and where, always on the screen while the session is open.
  std::string header = s.noteUntil != 0 ? std::string(tr(STR_SSH_NO_FULLSCREEN)) : s.header;
  if (s.viewOffset != 0) header = "[-" + std::to_string(s.viewOffset) + "] " + header;
  renderer.drawText(SMALL_FONT_ID, 6, s.headerTop + 1, header.c_str());
  renderer.drawLine(0, s.gridTop - 3, width - 1, s.gridTop - 3);

  drawGrid();
  if (mappedInput.hasTouch()) {
    drawKeys();
  } else {
    const auto labels =
        mappedInput.mapLabels(tr(STR_SSH_DISCONNECT), tr(STR_SSH_TYPE_LINE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  const bool full = s.fullRefresh;
  s.fullRefresh = false;
  renderer.displayBuffer(full ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

#else  // no SSH in this build: the screen only says so

struct SshTerminalActivity::Impl {};

SshTerminalActivity::SshTerminalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RemoteServer server)
    : Activity("SshTerminal", renderer, mappedInput), server_(std::move(server)) {}
SshTerminalActivity::~SshTerminalActivity() = default;

bool SshTerminalActivity::preventAutoSleep() { return false; }
void SshTerminalActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}
void SshTerminalActivity::onExit() { Activity::onExit(); }
void SshTerminalActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}
void SshTerminalActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_SSH_ERR_UNAVAILABLE));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
void SshTerminalActivity::showMessage(const char*) {}
void SshTerminalActivity::askPassword() {}
void SshTerminalActivity::startWifi() {}
void SshTerminalActivity::connectNow() {}
void SshTerminalActivity::askHostKey() {}
void SshTerminalActivity::enableTyping() {}
void SshTerminalActivity::sendBytes(const std::string&) {}
void SshTerminalActivity::scrollView(int) {}
void SshTerminalActivity::openKeyboard() {}
void SshTerminalActivity::askDisconnect() {}
void SshTerminalActivity::pressKey(uint8_t) {}
bool SshTerminalActivity::keyAt(int, int, uint8_t&) const { return false; }
void SshTerminalActivity::drawGrid() {}
void SshTerminalActivity::drawKeys() {}

#endif
