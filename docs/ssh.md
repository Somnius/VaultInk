# SSH terminal and SFTP

VaultInk on the X4 Pro can open a shell on your own server and copy files over SFTP.

Status: written and tested on the computer, **not yet built with PlatformIO, run on a device or
released**. What was checked and what was not is at the end of this page.

- Where: **Settings → Network → SSH servers**. The same servers also show in **WebDAV and FTP
  servers**, where SSH is a third protocol.
- Boards: the X4 Pro builds only (`VAULTINK_SSH=1`). On the other boards the menu entry is not
  there.
- Wi-Fi comes up when you connect and goes down when you leave the screen. Nothing runs in the
  background, and nothing wakes the device.

## Set up a server

1. Settings → Network → SSH servers → **Add Server**.
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
is kept in memory while the browser is open, so a dropped connection can be opened again, and
wiped when you leave.

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

## The server's key

The first time you connect to a server the device shows its key fingerprint
(`ssh-ed25519 SHA256:...`) and asks. Compare it with the server's own before you say **Trust**:

```
ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub
```

Cancel is the preselected answer. After *Trust* the key is saved and every later connection must
show the same key.

If the server later shows another key, the device stops with **Host key changed** and does not
connect. There is no "connect anyway". If the server was reinstalled and the new key is right,
open the server's page and use **Forget the server's key**; the next connection asks again.

The device asks for an Ed25519 host key first, then ECDSA P-256, then RSA (`rsa-sha2-256`).

## The terminal

- The top line always says **Connected to *host* as *user***.
- The screen is 80 columns wide when the panel's long side is across (60 on the short side), and
  as many rows as fit; the last 500 lines can be scrolled back.
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
- The terminal tells the server it is `xterm` (the library's fixed choice). Sequences it does not
  know are ignored. If a program misbehaves, `export TERM=vt100` on the server helps.
- Black and white: colours are dropped, bold is drawn heavier, underline and inverse are drawn
  as an underline.
- Characters outside the built-in small font show as `?` or stay empty. Wide characters (CJK,
  emoji) are not supported.
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
  - `known_hosts`: the pinned server keys, in OpenSSH's format.
- That folder is closed to the web server, to plugins, to files coming from servers and to the
  developer link.
- If the device is lost: remove its line from `authorized_keys` on your servers.
- Erasing the device's flash makes a new device secret; the key file then cannot be opened and a
  new key has to be made.

## Memory

- The screen and its scrollback are one block in PSRAM: 4 bytes a character, about 170 KB for 80
  columns.
- The SSH library's buffers (a 32 KB receive window, 16 KB packets) are single blocks, which this
  board places in PSRAM too.
- The key exchange and the sign-in need small blocks from the internal heap. Before connecting,
  the device checks for 60 KB free with a 24 KB block and refuses with "not enough memory"
  otherwise. These two numbers are a first choice: the real need has not been measured on a
  device yet.
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

## For developers

- `lib/wolfssh`: the library, vendored unchanged, with VaultInk's build files next to it
  (`lib/wolfssh/UPSTREAM.md`).
- `lib/Ssh`: the terminal emulator, known_hosts, key file layout, address parsing and key
  mapping. No platform code.
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
3000 lines of output, exit); a server that never answers (timeout, cancel); and the same first
connection against an ECDSA and an RSA host key.

What that does **not** show: anything on the ESP32 (the PlatformIO build, heap use, speed, the
Wi-Fi socket, the screens, the e-ink redraw), a successful password sign-in (the test server
runs without root, so it cannot check passwords), and servers other than OpenSSH.
