# The debug page

A page that reads a reader's status, log and screen over the USB cable and makes a report file,
for when you want to help find a bug: <https://somnius.github.io/VaultInk/debug.html>

It needs the **developer build** on the reader. That is the same firmware plus a serial link we
call read-only. Exactly: it can reboot the reader, change the log level, take a screenshot and write a small bench log to the SD card; it cannot read or write your files or settings. It cannot be told to press keys or install anything
either ([docs/devlink.md](devlink.md#the-build-users-may-install) has the details).

**Nothing is uploaded.** The page talks to the reader inside your browser. A report is a file you
save and then attach to a GitHub issue or send by e-mail yourself.

Nothing on this page has been tried on a real reader yet: the parsing, the picture and the
redaction are tested on a computer (`node test/debuglib_test.js`), the page itself is not.
[What is not checked yet](#what-is-not-checked-yet) lists it.

The browser remembers the permission for the reader's port for the whole site
`somnius.github.io`, which all GitHub Pages of that account share, not for this page alone.

## What you need

- An Xteink X4 Pro and a USB-C **data** cable (some cables only charge).
- A computer with desktop **Chrome** or **Edge** (or another Chromium-based browser). The page uses
  Web Serial. Firefox, Safari and browsers on Android phones are not supported yet.

## 1. Install the developer build

1. Open the [install page](https://somnius.github.io/VaultInk/#developer-build) and the section
   **Developer build (read-only debugging)**.
2. Put the reader into boot mode and connect it, as for a normal install.
3. Tick the box and press **Install the developer build**.

Installing it replaces your current firmware. The version on the boot screen and in Settings then
ends in `-dev`. Your SD card is not touched.

**Going back:** press the normal **Install VaultInk** button on the same page. From the reader,
Settings → System → Check for Updates offers the next release when one is out (a release with the
same version number is not offered) and installing it puts the normal firmware back.

Keep the normal firmware on a reader that leaves your hands: with the developer build, anyone with
the reader and a cable can read its status and log and take a screenshot of what is on the screen.

## 2. Connect

1. Wake the reader and plug the cable in. It must not be in Transfer & Remotes → USB Drive.
2. Open the [debug page](https://somnius.github.io/VaultInk/debug.html) and press **Connect**.
3. Choose the reader in the browser's list and confirm.

The link drops when the reader sleeps and in USB Drive mode. When the reader wakes the page
connects again by itself if the browser still allows it; otherwise press Connect. For a long
session set Settings → System → Time to Sleep to a longer time.

## 3. Look

- **STATUS** shows the version, memory, battery, the name of the screen in front and a few more
  numbers as a table. Only the developer build answers it; with the normal firmware the page says
  there was no answer.
- **Log** shows what the reader prints. **Log level 2** adds debug lines (more detail, and more
  names: see below); level 1 turns them off again. **Pause** stops the view from moving, **Save
  log** writes the lines to a text file as they are, not redacted.
- **Take a screenshot** shows the screen as it is now. If it fails ("SCREENSHOT_END did not
  follow", or "The picture stopped at ..."), take it again with the log level at 1: a log line can
  land inside the picture's bytes.
- **Reboot** restarts the reader, like its reset button. The page asks first.

## 4. Create a report

1. Reproduce the problem with the page connected, so the log has it.
2. Under **Create a report**, choose what goes in:
   - **STATUS** is ticked.
   - **The last N log lines** is not ticked. Tick it when the log shows the problem.
   - A **screenshot** is in the report only if you tick "Include in report" under that picture.
     Save it with its own button; the report lists its file name.
3. Write what happened, and under "Also hide these words" add names the page cannot know (a book
   title, your own name).
4. Press **Create report**. The text appears in a box, redacted. **Read it** and remove what you
   do not want public: you can edit the box.
5. Press **Download the report**. It saves a `.txt` file to your computer.
6. Open a [debug report issue](https://github.com/Somnius/VaultInk/issues/new?template=debug_report.yml),
   paste the text and attach the pictures. Or e-mail them to **me@lefteros.com** if you would
   rather not post them.

## What a report can show about you

- **STATUS**: the firmware version, memory and battery numbers, and the name of the screen in
  front (the screen's name in the code, not what it shows).
- **Log lines**: with debug lines on they can hold the names of the books, notes and folders you
  open, the names of Wi-Fi networks and the addresses of servers (LiveSync, OPDS, KOReader sync,
  WebDAV, FTP, SSH). Passwords are not written to the log ([docs/debug-logs.md](debug-logs.md)).
- **A screenshot**: whatever was on the screen.

The redaction replaces what it recognises with a word in angle brackets:

| It becomes | What |
| --- | --- |
| `<ssid>` | a Wi-Fi name, where the log says "SSID: ...", "saved network: ...", "connected to ..." and the like |
| `<ip>`, `<mac>` | IPv4 and IPv6 addresses, hardware addresses |
| `<host>` | a server name, on its own or in a URL (the `https://` stays) |
| `<path>`, `<file>` | a path on the SD card or a file name; the ending (`.epub`, `.md`) stays |
| `<email>`, `<user>` | an e-mail address, a user name (`user lef`, `lef@mynas`) |
| `<name>` | a title, author, folder, server or font name where the log labels it (`title=...`, `Title: ...`, a name in single quotes) |
| `<hidden>` | a word from your own list |

**It is best effort.** It works from the wording of the log and from patterns, so it can miss a
name (a book title printed without its file name, a Wi-Fi name in a message it does not know) and
it can hide something harmless. A screenshot is never changed. **GitHub issues are public**: read
the report before you post it.

## Linux

If Connect fails with "Failed to open serial port", your user probably may not open the port. It
(`/dev/ttyACM0`, usually) belongs to a group: `uucp` on Arch-based systems, `dialout` on Debian,
Ubuntu and Fedora. `ls -l /dev/ttyACM0` shows it. The usual ways to get access:

```sh
sudo usermod -aG uucp $USER     # or dialout; then log out and in again
```

or a udev rule, in a file such as `/etc/udev/rules.d/70-vaultink.rules`:

```
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", TAG+="uaccess"
```

then `sudo udevadm control --reload`, and plug the reader in again.

On the one Linux computer this was looked at, the port was group `uucp` and needed one of these.
The udev rule above is the usual form and is **not verified** by us on a reader. A browser
installed as a Snap or Flatpak may add limits of its own: not checked.

The same error appears when another program has the port: a serial monitor, `scripts/devlink.py`,
the installer's Logs & Console, or the debug page in another tab.

## Windows

The reader should appear as a COM port. Whether Windows 10 and 11 need a driver for it is **not
verified**: the page has not been tried on Windows. Please try it and tell us what happened.

## macOS, Android, other browsers

- macOS with Chrome or Edge: not tried.
- Android: not supported yet. Chrome on Android does not offer USB serial devices to Web Serial in
  the versions we read about; not tried on a phone.
- Firefox and Safari: not supported yet.

## What is not checked yet

- The page on a real reader, on any system: connecting, STATUS, the log, a screenshot, Reboot.
- Whether opening the port from the browser restarts the reader.
- Reconnecting after sleep: the page listens for the browser's "connect" event and opens the port
  again; whether the browser keeps the permission across a sleep on each system is not known.
- The screenshot over a real cable (the reader sends 48,000 bytes in one write; bytes can be lost,
  and the page then reports it).
- The "Developer build" button: `manifest-dev.json` and the image are written by
  `scripts/deploy_web_installer.sh`, which has not been run with these changes
  (`scripts/deploy_web_installer.sh --dry-run` puts the files together without publishing).
- That the developer build compiles and fits.
- Windows, macOS, Android.

## For developers

- `site/debug.html` is the page, `site/debuglib.js` its functions without a page in them (the
  line reader, the reply and screenshot parsing, the picture, the redaction, the report).
  `node test/debuglib_test.js` tests them; no packages needed. The picture test compares against
  `scripts/devlink.py`'s `rotate_framebuffer`.
- To try the page locally: `python3 -m http.server -d site 8000` and open
  `http://localhost:8000/debug.html` (Web Serial works on `localhost`).
- The page sends `STATUS`, `LOG`, `REBOOT` and `SCREENSHOT` and nothing else. A reply is taken
  only by the command it answers (`replyMatches`): a `STATUS` that answers after its 3 seconds is
  logged as late, not shown as the answer to the next command.
