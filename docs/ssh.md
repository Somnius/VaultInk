# SSH terminal and SFTP

VaultInk on the X4 Pro can open a shell on your own server and copy files over SFTP.

Status: released in 0.75.0, written and tested on the computer, **not yet run on a device**. The first
version built with PlatformIO for the X4 Pro; the security fixes made after that build (host key
file, key exchange, time limits) are tested on the computer only. What was checked and what was
not is at the end of this page.

- Where: **Settings → Connectivity → SSH servers**. The same servers also show in **WebDAV and FTP
  servers**, where SSH is a third protocol.
- Boards: the X4 Pro builds only (`VAULTINK_SSH=1`). On the other boards the menu entry is not
  there.
- Wi-Fi comes up when you connect and goes down when you leave the screen. Nothing runs in the
  background, and nothing wakes the device.

## Set up a server

1. Settings → Connectivity → SSH servers → **Add Server**.
2. **Address**: `server`, `server:port` or `server:port/folder` (the folder is where the file
   browser starts; without one it starts in your home folder). An IPv6 address goes in brackets:
   `[2001:db8::1]:22`. Do not put the user name in the address.
3. **Username**.
4. **Sign in with**: *Password, asked each time* or *This device's key*.

Then **Terminal** opens the shell and **Browse files (SFTP)** opens the file browser.

## Signing in

### Password

The password is typed on the device at every connection. It is **never saved**: not on the SD
card, not in the settings, not by the web settings page (the page drops a password sent for an SSH
server). In the terminal it is wiped from memory when the sign-in is over. In the file browser it
is wiped once the sign-in is over too; if the connection drops later, **Retry** asks for the
password again.

The server must allow password sign-in (`PasswordAuthentication yes`). Servers that only offer
"keyboard-interactive" are not supported: use the key.

### This device's key

Choosing *This device's key* the first time makes an Ed25519 key **on the device**, with the
device's own random generator. The private half never leaves the device.

1. Open **Show public key**. The screen shows a QR code and the same line as text:
   `ssh-ed25519 AAAA... vaultink`, and the key's fingerprint.
2. On the server, add that line to `~/.ssh/authorized_keys` of the user you sign in as (one
   line, mode 600):

   ```
   echo 'ssh-ed25519 AAAA... vaultink' >> ~/.ssh/authorized_keys
   ```

   Scan the QR code with a phone to copy the line without typing it.
3. To check: `ssh-keygen -lf ~/.ssh/authorized_keys` on the server prints the same `SHA256:...`
   fingerprint as the device.

One key serves every server. **Make a new key** on that screen replaces it: the old line stops
working everywhere and has to be replaced on each server.

## Host keys

A host key is the server's own key. It is how the device knows it is talking to your server and
not to something in between.

### First connection

The first time you connect to a server the device shows its key fingerprint
(`ssh-ed25519 SHA256:...`) and asks. Compare it with the server's own before you say **Trust**:

```
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
```

Cancel is the preselected answer. After *Trust* the key is saved ("pinned") and every later
connection must show the same key. The device asks for an Ed25519 host key first, then ECDSA
P-256, then RSA (`rsa-sha2-256`).

A server may exchange keys again in the middle of a session (OpenSSH's `RekeyLimit`, or after
about 1 GB). The device then accepts only the very key the session started with.

### The file

The pinned keys are in `/.vaultink/ssh/known_hosts` on the SD card, in OpenSSH's format: one
`host keytype key` line per server (`[host]:port` when the port is not 22). The last line is

```
# vaultink-mac <64 hex digits>
```

a keyed checksum (HMAC-SHA256) over the pinned lines. Its key is made from the device's secret,
which is in the chip and not on the card. So someone who has only the card cannot take a pin out
(to get a fresh "Trust?" question for his own server) or put one in (to have his key accepted
without a question): the device sees that the file is not the one it wrote. Comments and blank
lines are not covered and may be added. Do not edit the key lines by hand: the device will then
refuse the file.

### What the messages mean

| Message | What happened | What to do |
| --- | --- | --- |
| **Unknown server key** | Nothing is pinned for this server yet. | Compare the fingerprint, then *Trust* or Cancel. |
| **Host key changed** | The server showed another key than the pinned one. Not connected. | If the server was reinstalled and the new key is right: **Forget the server's key** on the server's page, then connect and compare again. If not, something is in between: do not connect. |
| **Host key changed during the session** | A key exchange in the middle of a session showed another key. Disconnected. | The same as above. |
| **known_hosts could not be read** | The file is there but could not be read whole, is too big, or is not text. Not connected, and the device does not ask to trust anything. | Check the SD card. To start again: **Forget all host keys**. |
| **known_hosts was changed outside the device** | The checksum line is missing or wrong: the file was edited on a computer, comes from another reader, or the device's flash was erased. Not connected. | **Forget all host keys**, then pin each server again, comparing the fingerprints. |

There is no "connect anyway" for any of these. In the last two cases the device writes nothing to
the file, so the pins in it are not lost by an accident; only *Forget all host keys* removes it.

**Forget all host keys** is the last row of Settings → Connectivity → SSH servers (it shows when there
is a file). It deletes the file after a question; every server then asks again at its next
connection.

## The terminal

- The top line says **Connected to *host* as *user***.
- Connecting has one time limit for the key exchange and the sign-in together (30 seconds). Back,
  or a tap on the screen, cancels it. Opening the network connection itself can take up to 8
  seconds and cannot be cancelled.
- A key the server does not take within 3 seconds ends the session with "The session ended": the
  screen does not hang on a dead connection.
- The text is drawn in the terminal's own fixed-width font: every character takes one cell of the
  same size, so columns line up as on any terminal. **Terminal text size** in the SSH servers list
  picks the cell (a tap goes to the next size); the number of columns follows from it. The
  terminal reads the setting when it opens: change it before you connect.

  | Text size | Cell (pixels) | Columns on the 480-pixel side | Columns on the 800-pixel side |
  | --- | --- | --- | --- |
  | 12 | 6 x 12 | 80 | 132 |
  | 14 | 8 x 14 | 60 | 100 |
  | 16 (default) | 8 x 16 | 60 | 100 |
  | 18 | 10 x 18 | 48 | 80 |

  Only whole cells are drawn, so nothing is cut at the right edge. As many rows as fit; the last
  500 lines can be scrolled back.
- A line longer than the screen continues on the next row, at the column count, as on any
  terminal. Programs that print fixed 80-column text (a login banner, `ls -l`) fit without
  wrapping at size 12 only.
- Box-drawing characters and block elements (U+2500 to U+259F: the frames of `tree`, `systemctl
  status`, progress bars) are drawn as lines and blocks that meet from cell to cell. The few the
  font does not have are drawn as the nearest one: a dashed line as a plain line.
- Output is collected and the panel is redrawn at most twice a second; a full refresh follows a
  `clear` or every 120 lines.

### Keys

On the touch screen, under the terminal:

| Key | What it does |
| --- | --- |
| Kbd | opens the on-screen keyboard for one line; the line is sent with Enter |
| Enter, Esc, Tab | those keys |
| Ctrl | a latch for the next key: Ctrl, then Kbd, then `d` sends Ctrl-D (no Enter is added) |
| ^C | Ctrl-C |
| PgUp, PgDn | look back through the scrollback; typing returns to the live screen |
| `<` `v` `^` `>` | the arrow keys (history, moving in the line) |
| Disconnect | closes the session, after a question |

A Bluetooth keyboard types straight into the terminal: letters, Enter, Backspace, Tab, Esc, the
arrows, Home, End, Delete, and Ctrl with a letter (Ctrl-C, Ctrl-D, Ctrl-Z, Ctrl-L ...). Page Up
and Page Down scroll back. Switch on **Bluetooth keyboard typing** in the settings first, as for
the text fields.

### Limits

- **No full-screen programs.** vim, nano, htop, less, tmux, mc and the like need an alternate
  screen and fast redraws; an e-ink panel has neither. The terminal says so when a program asks
  for one. Use line programs: `ls`, `cat`, `tail`, `grep`, `git`, `apt`, `systemctl`.
- That note takes the place of the top line for about six seconds. A server (or a file you
  `cat`) can therefore hide "Connected to *host* as *user*" for a few seconds by sending the
  alternate-screen sequence (`ESC [ ? 1049 h`). It cannot change what the line says.
- The terminal tells the server it is `xterm` (the library's fixed choice). Sequences it does not
  know are ignored. If a program misbehaves, `export TERM=vt100` on the server helps.
- Black and white: colours are dropped. Inverse is white on black, underline is a line under
  the cell, bold is drawn one pixel heavier (not at size 12, where the cells are too narrow, and
  not for box lines).
- The font has Latin, Greek, Cyrillic, arrows, mathematical signs, box drawing and blocks (1051
  characters). Anything else shows as `?`: CJK, emoji, Braille patterns, Hebrew and Arabic.
  Combining accents are not joined to their letter. Wide characters take one cell, not two.
- The text size cannot be changed while a session is open.
- One session at a time. No port forwarding, no agent, no X11.
- The session ends when the screen closes: Disconnect, the device going to sleep, or USB Drive
  mode. After a wake the lock screen comes first and the session is already gone.

## SFTP

**Browse files (SFTP)** is the same browser as for WebDAV and FTP
([webdav-ftp.md](webdav-ftp.md)): tap a folder to open it, tap a file to download it to the
folder chosen at the top, **Upload a file here**, **New folder**.

- A download is written to `name.part` on the card and renamed when all of it arrived; a failed
  or cancelled one leaves the old file alone.
- Names from the server that the card cannot hold are left out, and a file is never written into
  the device's own folders (`/.crosspoint`, `/.vaultink`); the saved passwords and the SSH key
  are never uploaded.
- A listing shows up to 500 entries; the folder is read in pieces and reading stops there.
- Symbolic links are followed for up to 32 entries per folder; the rest of the links, devices and
  sockets are not shown.
- No delete, rename or resume. An upload replaces a file of the same name on the server.
- A download never writes more than the size the listing gave, and never more than 2 GB. A read
  the server refuses in the middle fails the download (the `.part` is removed); it is not taken
  for the end of the file. A full card fails it the same way.
- Not tried with files over 1 GB.

## Security

- The connection is encrypted and the server is checked by its pinned key. Key exchange:
  curve25519-sha256 or ecdh-sha2-nistp256. Cipher: AES-GCM (`aes256-gcm@openssh.com`,
  `aes128-gcm@openssh.com`). A server that offers none of these (for example only
  chacha20-poly1305 and AES-CTR switched off) cannot be reached; stock OpenSSH offers AES-GCM.
- Files, in `/.vaultink/ssh/` on the SD card:
  - `id_ed25519.key`: the device's key, sealed with AES-256-GCM under a key made from the
    device's secret (which lives in the chip's flash, not on the card). The card alone does not
    give the key away; the card moved to another reader cannot open it. Someone with both the
    card and the device open on a bench can.
  - `known_hosts`: the pinned server keys, in OpenSSH's format, with a checksum line keyed from
    the same device secret (see [Host keys](#host-keys)).
- That folder is closed to the web server, to plugins, to files coming from servers and to the
  developer link.
- If the device is lost: remove its line from `authorized_keys` on your servers.
- Erasing the device's flash makes a new device secret; the key file then cannot be opened and a
  new key has to be made, and `known_hosts` counts as changed (*Forget all host keys*).
- The typed password: the device's copies are wiped when the sign-in is over. The on-screen
  keyboard's own buffer is cleared where the terminal and the browser receive it; memory the
  keyboard screen freed earlier is not.
- Known limit: whether the SD card says "no such file" for a `known_hosts` that is there is up to
  the card and its driver; a card that hides the file looks like a first use, and the device then
  asks before trusting, as on any first connection.

## Memory

- The screen and its scrollback are one block in PSRAM: 4 bytes a character, about 180 KB for 80
  columns.
- The terminal font is 84 KB of the firmware (four sizes, 1051 characters each, one bit a pixel),
  only in the builds with SSH.
- The SSH library's buffers (a 32 KB receive window, 16 KB packets) are single blocks, which this
  board places in PSRAM too.
- The key exchange and the sign-in need small blocks from the internal heap. Before connecting,
  the device checks for 60 KB free with a 24 KB block and refuses with "not enough memory"
  otherwise. **These two numbers are a first choice: the real need has not been measured on a
  device yet.** Too low and a connection fails half way with a memory error; too high and it is
  refused when it would have worked.
- SSH is opened from Settings, not from a book, so the reader's memory is not in use. Leaving the
  screen restarts the device (into Settings from the terminal, to Home from the file browser), as
  the other network screens do, to give the heap back.

## Licence

The SSH library is [wolfSSH](https://github.com/wolfSSL/wolfssh) 1.6.0, under the GPL version 3
or later. It is linked into the X4 Pro builds. The firmware binaries are distributed under the
**GPL-3.0-or-later**: wolfSSL says "version 2 or any later version", wolfSSH needs version 3, and the
Apache-2.0 libraries in the binary (NimBLE-Arduino, JPEGDEC and others) are compatible with version 3 but not
version 2. VaultInk's own source stays MIT. See [licenses.md](licenses.md) and
[NOTICE](../NOTICE).

The terminal's font is made from [Terminus Font](https://terminus-font.sourceforge.net/) 4.49.1 by
Dimitar Toshkov Zhekov, under the SIL Open Font License 1.1 (`lib/Ssh/fonts/OFL.TXT`). The
firmware holds a subset of four sizes as plain bitmaps, which the licence counts as a modified
version, so it is not presented under the name Terminus: the device calls it the terminal font.

## For developers

- `lib/wolfssh`: the library, vendored unchanged, with VaultInk's build files next to it
  (`lib/wolfssh/UPSTREAM.md`).
- `lib/Ssh`: the terminal emulator, known_hosts, key file layout, address parsing and key
  mapping. No platform code.
- `lib/Ssh/SshTermFont.*`: the terminal font (sizes, glyph lookup, stand-ins for missing
  characters, bold, drawing one cell) and the grid arithmetic (`gridFor`). The bitmaps in
  `SshTermFontData.cpp` are written by `scripts/gen_ssh_font.py` from the Terminus BDF files
  (`lib/Ssh/fonts/README.md`). Tests: `test/ssh/SshTermFontTest.cpp`.
- `lib/SshClient`: the session on wolfSSH, through a socket interface.
- `src/network/SshDevice.*`, `SftpClient.cpp`, `src/activities/remote/SshTerminalActivity.*`,
  `SshKeyActivity.*`: the device side.

### Checks on the computer

```
# the platform-free parts (in the normal test tree: test/ssh)
cmake -S test -B build/test && cmake --build build/test --target SshTest RemoteServerJsonTest
build/test/ssh/SshTest && build/test/ssh/RemoteServerJsonTest

# the real client against a real OpenSSH server on 127.0.0.1
pio pkg install -e x4pro        # once, for the wolfSSL sources in .pio/libdeps
test/ssh_live/run.sh
```

`test/ssh_live/run.sh` compiles wolfCrypt 5.7.2 (the sources and the `user_settings.h`
PlatformIO downloaded, with the firmware's flags), the vendored wolfSSH and `lib/SshClient` for
the computer, starts a throwaway `sshd` as the current user on 127.0.0.1 and runs: key
generation and the sealed key file; first connection (unknown key, the fingerprint equal to
`ssh-keygen -l`); a changed key (refused); a wrong key and a wrong password (refused, each
offered once); SFTP home, mkdir, upload, download, rename, stat, listing of 700 entries and a
listing stopped at a cap; a shell through the terminal emulator (pty size, a resize, Ctrl-C,
3000 lines of output, exit); new session keys in the middle of an SFTP session and of a shell,
with typing right behind the request; a read the server refuses (not taken for the end of a
file); the known_hosts checksum with the real HMAC; a server that never answers (timeout,
cancel) and one that sends a byte now and then (the 30-second limit, set to 1.5 s in the test);
and the same first connection against an ECDSA and an RSA host key.

What that does **not** show: anything on the ESP32 (the PlatformIO build, heap use, speed, the
Wi-Fi socket, the screens, the e-ink redraw), a successful password sign-in (the test server
runs without root, so it cannot check passwords), and servers other than OpenSSH.
