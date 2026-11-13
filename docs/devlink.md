# Developer link

A few commands over the USB cable for working on the firmware: ask the reader for its state, reboot
it, put it in flashing mode without holding a button, log its state to a CSV file during a test,
press its keys and touch its screen from a script, fetch its bench logs and screenshots, and flash
a firmware file that is on its SD card.

**It is for developers only.** The commands exist only in the `x4pro-dev` build. That build is never
uploaded as a release, to the web installer or to anyone's device. The builds people install
(`x4pro-gh_release`) answer one command, `CMD:SCREENSHOT`, as they always did, and ignore the rest.

Nothing here has run on a device yet: the parser, the replies and the host script are tested on a
computer, the firmware part is written from the source of the Arduino core and has not been compiled
or flashed. [What is not checked yet](#what-is-not-checked-yet) lists it.

## Build and flash

```sh
pio run -e x4pro-dev -t upload
```

`x4pro-dev` is `x4pro` (same board, partition table, flags and `-Os`) plus four things:

- The version text ends in `-dev` (`0.74.0-x4pro-dev`), see [The developer label](#the-developer-label).
- `-DVAULTINK_DEVLINK=1` compiles the commands in.
- `-DLOG_DBG_RUNTIME=1` makes debug log lines switchable, so `CMD:LOG` has something to switch. They
  start off, unless Settings → System → Debug Logs is on.
- `debug_tool = esp-builtin` for JTAG (below).

A second build, `x4pro-dev-verify`, is `x4pro-dev` with GitHub's certificate checked. It is for one
test: [Live test: GitHub's certificate](#live-test-githubs-certificate).

## The developer label

So that a developer build is never taken for a release:

- Its version is `0.74.0-x4pro-dev` (the release is `0.74.0-x4pro`). It is the one version text of
  the firmware, so About, the boot screen, the Settings title, the web server's status, the
  `User-Agent` of its requests and the update screen all show it.
- `STATUS` has `"version":"0.74.0-x4pro-dev"` and `"build":"dev"`.
- **The update check.** It compares the three numbers only, as in every build. A release with the
  same numbers is not offered (0.74.0 over 0.74.0-x4pro-dev: "no update"). A release with higher
  numbers is offered, and installing it replaces the developer build with the release build, so the
  developer build says so: the confirmation screen has the line "Developer build: Update installs
  the release build" under the two versions, and the log has
  `[OTA] Developer build 0.74.0-x4pro-dev, latest release <tag>: offered, installing it replaces this developer build`
  (or `not newer, not offered`). Nothing checks for updates on its own: the check runs only when you
  open the update screen in Settings.

## Commands

Send a line `CMD:<NAME> [arguments]` ending in a newline, at any baud rate (it is a USB port). The
reader answers with one line of JSON: `{"ok":true,...}` or `{"ok":false,"error":"..."}`. Log lines
share the port, so read lines until one parses as a JSON object with an `ok` field.
`scripts/devlink.py` does that.

| Command | Reply |
| --- | --- |
| `PING` | `{"ok":true,"pong":true,"uptime_ms":...}` |
| `HELP` | `{"ok":true,"commands":"PING HELP STATUS LOG BENCH KEY TAP SWIPE FLASH LS GET PUT REBOOT BOOTLOADER SCREENSHOT"}` |
| `STATUS` | the fields below |
| `LOG <0-3>` | 2 and 3 turn debug lines on, 0 and 1 turn them off. Errors and info lines always print: the levels are fixed when the firmware is compiled, and the reply's `note` says so |
| `BENCH` | the state of the SD bench log: `{"ok":true,"minutes":5,"on":true,"file":"/.vaultink/bench.csv","file_bytes":...,"rows":...,"failed":...}` (`rows` and `failed` count since boot) |
| `BENCH <minutes>` | sets the interval, 0 to 1440; 0 is off. The same reply plus `"saved"`: the setting is on the card. See [The SD bench log](#the-sd-bench-log) |
| `KEY <name> [hold_ms]` | presses BACK, CONFIRM, LEFT, RIGHT, UP or DOWN: `{"ok":true,"queued":2}`. See [Keys and touches](#keys-and-touches) |
| `TAP x y [hold_ms]` | touches a screen pixel: `{"x":240,"y":400,"ok":true,"queued":2}` |
| `SWIPE x1 y1 x2 y2 [ms]` | a swipe between two screen pixels: `{"x":..,"y":..,"x2":..,"y2":..,"ok":true,"queued":2}` |
| `LS <dir>` | one line an entry, `{"name":"a.txt","dir":false,"size":3}`, then `{"ok":true,"count":1,"truncated":false}`. See [Files on the SD card](#files-on-the-sd-card) |
| `GET <path> <offset> <len>` | up to 2880 bytes of a file as base64: `{"offset":0,"size":9,"data":"...","len":9,"eof":true,"ok":true}` |
| `PUT <path> <offset> <base64>` | writes up to 180 bytes: `{"ok":true,"written":9,"size":9}` |
| `FLASH <sd path>` | checks a `.bin` on the card and writes it to the slot that is not running; progress lines, then `{"ok":true,"action":"flash","slot":"app1",...}`. It does not restart. See [Flashing from the SD card](#flashing-from-the-sd-card) |
| `REBOOT` | `{"ok":true,"action":"reboot"}`, then a restart 100 ms later. Like the reset button: nothing is saved first |
| `BOOTLOADER` | `{"ok":true,"action":"bootloader"}`, then a restart into the chip's download mode, where `esptool` and `pio run -t upload` can flash it. Leaving it without flashing needs a reset of the chip; how to do that on an X4 Pro is not checked |
| `SCREENSHOT` | not JSON: `SCREENSHOT_START:<n>`, n raw bytes, `SCREENSHOT_END` (every build) |
| anything else | `{"ok":false,"error":"unknown"}` |

A line longer than 256 bytes is dropped whole. Names are case-sensitive.

`LS` and `FLASH` send more than one line. Their extra lines (an entry, a progress step) are JSON
objects **without** an `ok` field; the line with `ok` is always the last one. So the rule above
still holds: read until a line has `ok`.

`STATUS` fields:

| Field | Meaning |
| --- | --- |
| `version` | the firmware version, `0.74.0-x4pro-dev` |
| `build` | always `dev`: only the developer build has `STATUS` |
| `uptime_ms` | milliseconds since boot (a wake from sleep is a boot) |
| `heap_free`, `heap_min_free`, `heap_largest` | internal RAM: free now, the lowest it has been, the largest free block |
| `psram_free` | free PSRAM |
| `battery_mv`, `battery_percent` | read the way the battery log reads them |
| `usb` | a cable is in (expected to be true whenever the link works) |
| `charging` | what the battery chip reports |
| `wake_cause` | the number the battery log gets: 0 power key, 1 after flashing, 2 USB power, 3 other, 4 the power key was not held long enough |
| `esp_wake_cause`, `reset_reason` | the chip's own numbers (`esp_sleep_get_wakeup_cause`, `esp_reset_reason`) |
| `cpu_mhz` | CPU speed now. The firmware lowers it to 80 after 3 s without input |
| `light_present`, `light_on`, `light_brightness`, `light_warmth` | the frontlight |
| `activity` | the name of the screen in front |
| `sleep_timeout_ms` | the auto-sleep setting, 0 for never |
| `idle_ms` | milliseconds since the last input (or since a screen last asked to stay awake) |
| `log_debug` | debug lines are printing |
| `bench_minutes` | the interval of the SD bench log, 0 when it is off |
| `dropped_lines` | lines thrown away for being over 256 bytes |

## The host script

`scripts/devlink.py` needs Python 3 and nothing else on Linux and macOS.

```sh
scripts/devlink.py status                     # STATUS, a field a line
scripts/devlink.py cmd LOG 2                  # any command, the JSON reply
scripts/devlink.py screenshot shot.png        # or .bmp; upright, like Power + Down
scripts/devlink.py bench --every 60 --minutes 30 --out idle.csv
scripts/devlink.py bench-file bench.csv       # the summary of a bench.csv copied from the card
scripts/devlink.py key CONFIRM                # --hold 800 for a long press
scripts/devlink.py tap 240 400                # a screen pixel, as in a screenshot; --hold 600
scripts/devlink.py swipe 400 300 80 300       # --ms 250
scripts/devlink.py ls /debug
scripts/devlink.py get /.vaultink/bench.csv bench.csv
scripts/devlink.py put note.txt /debug/note.txt
scripts/devlink.py flash /firmware.bin        # a file already on the card; then: cmd REBOOT
```

`flash` copies nothing: its argument is the path of a `.bin` **on the reader's SD card**. Put the
file there first (USB Drive mode, or a card reader). `get` asks for 2880 bytes a time and asks
again, three times at most, for a part that came back cut. `put` sends as much as fits a 256-byte
line each time (about 170 bytes with a short path). `key` refuses POWER before anything is sent.

It uses `--port`, else `$DEVLINK_PORT`, else the first `/dev/serial/by-id/*Espressif*JTAG*`, else the
first `/dev/ttyACM*`. The exit code is 0 for `ok:true`, 1 for `ok:false`, 2 when there is no port or
no answer.

`bench` opens the port again for every poll and writes a row each time: `time, epoch, link,
battery_mv, battery_percent, heap_free, cpu_mhz, light_on, light_brightness, activity, usb, charging,
idle_ms, uptime_ms`. When the reader does not answer, the row has `link` = `none` and the rest empty,
and the next poll tries again.

## The SD bench log

For drain tests with the cable out: the cable charges the battery and the link needs the cable, so
the reader writes the numbers to its own card.

```sh
scripts/devlink.py cmd BENCH 5     # a row every 5 minutes, from now
scripts/devlink.py cmd BENCH       # what it is set to, and how many rows this boot
scripts/devlink.py cmd BENCH 0     # off
```

- **Off by default.** The interval is kept in `/.vaultink/bench.cfg` on the card (the number of
  minutes; no file means off), so it survives sleep, a restart and a power-off. Deleting that file
  turns the log off too.
- While the log is on and the reader is awake, it appends one line to `/.vaultink/bench.csv` every
  N minutes: one at once after `BENCH <minutes>`, one in the first moments after every boot or
  wake, then one each time the interval has passed.
- **Only while awake.** It runs in the main loop. It sets no timer, does not wake the reader, and
  the sleep path is unchanged: nothing is written on the way to sleep or during it. It is skipped
  while USB Drive mode has the card.
- **No radio.** It reads whether Wi-Fi and Bluetooth are on; it starts neither.
- **Cheap.** One fixed 192-byte buffer, the file is opened, written with one small write (two for
  the first line of a file) and closed each time. A write that fails is logged
  (`[ERR] [BENCH] Row not written to /.vaultink/bench.csv`), counted in `failed`, and tried again
  only at the next interval.
- **Capped.** Before a row would take `bench.csv` past 256 KB, the file is renamed to
  `bench.old.csv` (replacing the last one) and a new file starts, so the two never hold more than
  about 512 KB. A row is about 75 bytes, so at one row every 5 minutes a file holds about 12 days
  awake.
- **The battery reading.** The same two readings `STATUS` takes (millivolts from the battery
  monitor, percent as the status bar shows it), but without waiting: `STATUS` waits for the screen
  task to finish drawing, the bench log looks whether that task is busy and, if so, tries again on
  the next pass of the loop. So a row never holds up the screen, and it is taken between redraws.
  On a board that reads the battery from a pin, asking for the percent feeds its smoothing once
  more, as every status bar draw does.

Columns:

| Column | Meaning |
| --- | --- |
| `epoch` | Unix time, or 0 when the clock is not trusted |
| `uptime_ms` | milliseconds since boot; it starts again after every sleep or restart |
| `battery_mv`, `battery_percent` | as in `STATUS` |
| `usb` | 1 with a cable in |
| `light_on`, `light_brightness`, `light_warmth` | the frontlight |
| `cpu_mhz` | CPU speed at that moment |
| `heap_free`, `heap_min`, `psram_free` | free internal RAM now, the lowest it has been, free PSRAM |
| `activity` | the screen in front (at most 32 bytes; a comma, a quote or a byte outside plain ASCII becomes `_`) |
| `wifi`, `ble` | 1 when the Wi-Fi mode is not off / the Bluetooth stack is running |
| `wake_cause` | as in `STATUS` |

Afterwards copy `bench.csv` from the card (USB Drive mode, or a card reader) and run:

```sh
scripts/devlink.py bench-file bench.csv
```

```
bench.csv: 9 rows, 7 awake stretches, 1 breaks (sleep or restart), 0 lines skipped

Drop per hour while awake (positive: the battery went down)
state          stretches  minutes  percent/h   mV/h
light off              2     10.0       6.00   36.0
light on               2     10.0      12.00  120.0
light changed          1      5.0       0.00   48.0
cable in               2     20.0      -3.00  -90.0
```

(The numbers above are from the test file, not from a reader.) A stretch is two rows in a row with
the reader awake in between; its length comes from `uptime_ms`, so an unset clock does not matter.
Where the uptime starts again the reader slept or restarted: that pair is a break and is not
counted. A stretch with a cable in at either end is put under `cable in`, one where the light
changed under `light changed`. The figure of a group is its whole drop over its whole time. Under
the summary the script lists every stretch. Percent moves in whole steps, so over a few minutes the
millivolts say more; use runs of half an hour or longer for the percent.

## Keys and touches

`KEY`, `TAP` and `SWIPE` put made-up input into a short queue. The firmware plays the queue at the
place where it reads the real buttons and the touch controller, so a screen cannot tell the
difference: the same press and release, the same held time, the same tap, long press and swipe,
and the auto-sleep timer counts it as input (`idle_ms` in `STATUS` starts again).

- **Keys.** `KEY <BACK|CONFIRM|LEFT|RIGHT|UP|DOWN> [hold_ms]`, held 120 ms unless you say (20 to
  5000). The key is added to what the input library reads from the pins, through the library's own
  hook for extra buttons, so its debounce and its timing apply. The names are the physical buttons:
  a remapping in Settings applies to them as it does to a finger.
- **POWER is refused**, by name: `{"ok":false,"error":"POWER is not allowed: the link never presses
  the power key"}`. The queue cannot hold a power press at all, so the link cannot put the reader to
  sleep or turn it off. There is no Home key command either.
- **Touches.** `TAP x y [hold_ms]` (80 ms; 500 or more is a long press) and
  `SWIPE x1 y1 x2 y2 [ms]` (250 ms; over 700 ms it is a drag, not a swipe). The coordinates are
  screen pixels as a screenshot shows them, in the orientation the screen has when the command
  arrives; a value outside the screen is moved to its edge, and the reply says which pixel was
  used. The thresholds are the touch controller's: a tap may move 59 pixels, a swipe is 60 or more.
  A board without a touch screen answers `this board has no touch screen`.
- **Bounded.** The queue holds 16 events; a key, a tap and a swipe are two each (down, up), so
  eight can wait. A ninth gets `queue full (16 events): wait and send it again`. `queued` in the
  reply is the number of events waiting, this one included.
- **Paced.** 50 ms pass between the end of one and the start of the next, and every state lasts at
  least 10 ms and two passes of the input loop, so a press is never lost to the debounce however
  fast the commands come. The times are counted on the firmware's own clock (`millis`) from the
  moment the event before was played, not from when the command arrived: a slow pass of the loop
  delays a release, it does not shorten a hold.
- A tap or swipe reports the time you asked for as its held time, even when its release was played
  late, so a tap never turns into a long press and a swipe never turns into a drag because the
  reader was busy.

```sh
scripts/devlink.py key DOWN && scripts/devlink.py key CONFIRM
scripts/devlink.py tap 240 400
scripts/devlink.py screenshot after.png
```

A command returns when the event is queued, not when the screen has changed: wait for the redraw
(or poll `STATUS` for `activity`) before the screenshot.

## Files on the SD card

`LS`, `GET` and `PUT` reach three folders and nothing else. The rule is an allow-list
(`src/util/DevLinkPaths.h`, tested in `test/dev_link`):

| Path | LS | GET | PUT |
| --- | --- | --- | --- |
| `/debug` and everything under it (the debug logs) | yes | yes | yes |
| `/screenshots` and everything under it | yes | yes | no |
| `/.vaultink` itself | yes (names and sizes) | | |
| `/.vaultink/bench.csv`, `bench.old.csv`, `battery-log.tsv` | | yes | no |
| `/.vaultink/bench.cfg` | | yes | yes |
| everything else | no | no | no |

Everything else is `{"ok":false,"error":"path refused (docs/devlink.md, Security)"}`: `/.crosspoint`
(settings, Wi-Fi and server passwords), `/Vaults`, `/books`, the card's root, and the rest of
`/.vaultink` (it also holds the lock PIN, the LiveSync profile and setup text and, in
`/.vaultink/ssh`, the device's SSH key and the pinned server keys, so only the four files above can
be read there; `/.vaultink/ssh` is refused a second time by the credential-path rule). `LS /.vaultink` shows that those files exist, not what is in them.

A path has one spelling. It is refused when it does not start with `/`, holds `..` anywhere, a `.`
part, `//`, a slash at the end (`LS` allows one), a backslash, a colon or a control byte, a part
that starts with a blank or ends in a dot or a blank (the card would open it under another name),
or a part that looks like a short 8.3 alias (`CROSSP~1`: a tilde followed by a digit). Upper and
lower case are the same, as on the card. A path is 200 bytes at most and may hold blanks.

- `LS <dir>` lists at most 200 entries; `truncated` says there were more.
- `GET <path> <offset> <len>`: `len` is 1 to 2880, so a reply line stays under 4 KB (3840 base64
  characters). `size` is the size of the file, `len` the bytes in this reply, `eof` whether the
  file ends here. `len` and `ok` come after the data, and the data leaves the reader 540 bytes at a
  time: check that the decoded data is `len` bytes long (`devlink.py get` does, and asks again).
- `PUT <path> <offset> <base64>`: 180 bytes of data at most (a command line is 256 bytes). Offset
  0 starts the file again; any other offset must be the size the file has now, so a part sent twice
  or out of order is refused (the error carries `size`). A file written over the link is 1 MB at
  most. An empty file cannot be written. Folders under `/debug` are made as needed. Writing
  `/.vaultink/bench.cfg` sets the bench interval the same way `BENCH <minutes>` does (the logger
  reads the file again).

```sh
scripts/devlink.py ls /screenshots
scripts/devlink.py get /screenshots/screenshot-1234.bmp shot.bmp
scripts/devlink.py get /.vaultink/bench.csv bench.csv && scripts/devlink.py bench-file bench.csv
```

So the bench log no longer needs USB Drive mode or a card reader.

## Flashing from the SD card

`FLASH <sd path>` is Settings → SD firmware update without the screens. It runs the same code
(`firmware_flash`) with the same checks, in the same order:

1. The path: absolute, a `.bin`, one spelling as above, anywhere on the card except `/.crosspoint`
   and `/.vaultink`. The file is only read as a firmware image; nothing of it is sent back.
2. The battery: refused under 30% unless USB power is in (`battery below 30% and no USB power`).
3. The size against the slot that is not running, then the whole image: header, segment table,
   checksum, SHA-256, the chip, and the board tag. A refused image answers
   `{"ok":false,"error":"image refused","reason":"WRONG_BOARD","slot":"app1"}` (the reason is the
   flasher's own name: `TOO_SMALL`, `TOO_LARGE`, `BAD_MAGIC`, `BAD_SEGMENTS`, `BAD_CHECKSUM`,
   `BAD_SHA`, `BAD_CHIP`, `WRONG_BOARD`, `BAD_SIZE`) and nothing is written.
4. `{"flash":"checked","bytes":...,"slot":"app1"}`, then the image is checked once more and written,
   with a line every tenth: `{"flash":"writing","written":...,"total":...,"percent":40}`.
5. `{"ok":true,"action":"flash","slot":"app1","bytes":...,"rebooted":false,"next":"send CMD:REBOOT
   to start it"}`. `slot` is the label of the partition that was written.

**It does not restart.** The new firmware starts at the next restart, whatever causes it: send
`CMD:REBOOT` when you are ready. Until then the reader runs the firmware it had. A failed write
answers `{"ok":false,"error":"flash failed","reason":"..."}`.

The reader does nothing else while it writes (no screen updates, no other command), about as long
as the Settings screen takes. `devlink.py flash` waits up to ten minutes and prints the progress.

```sh
scripts/devlink.py flash /firmware.bin && scripts/devlink.py cmd REBOOT
scripts/devlink.py status        # "version" is the new build's
```

## Limits

- **The link dies at sleep.** The firmware closes the USB serial port before deep sleep, and
  `/dev/ttyACM*` goes away. It comes back when the reader wakes. So the link cannot watch sleep, and
  a test longer than the auto-sleep time has gaps unless you set auto-sleep to Never.
- **USB Drive mode takes the port.** While File Transfer → USB Drive is open there is no serial port
  and no JTAG. They return when you leave it (the reader restarts).
- **The cable feeds the reader.** The link needs USB, so every `battery_mv` and `battery_percent` it
  reads is taken with the cable in. They do not show what the battery loses on its own. The
  [SD bench log](#the-sd-bench-log) is for that.
- **A command needs its newline.** Before, `CMD:SCREENSHOT` without one still ran after a second's
  wait, during which the reader did nothing else. Now nothing waits and a line counts when it ends.
- One reply is 767 bytes at most; a longer one comes back as `{"ok":false,"error":"reply too long"}`.
  `GET` is the exception: its line is up to 4 KB and leaves in several writes, so a log line from
  another task can land inside it. The line then does not parse, or its data is not `len` bytes
  long: ask again.
- **A touch is one finger and a straight line.** No Home key, no two-finger gestures, no power key.
- **Injected input and a real finger at once** are not kept apart: use one or the other.
- **While `FLASH` writes, the reader answers nothing.**

## Security

A debug port that can read the SD card can read vault notes and saved passwords, with no more than
a cable. So:

- **None of this is in a release.** Every command but `SCREENSHOT` is compiled into `x4pro-dev`
  (and `x4pro-dev-verify`) only, and it must stay that way: never add `VAULTINK_DEVLINK` to a
  release build. In the builds people install, the code for `KEY`, `TAP`, `SWIPE`, `LS`, `GET`,
  `PUT` and `FLASH` does not exist: their names are not even in the command table, and a line with
  one of them is ignored like any other unknown line.
- A reader with the developer build obeys anyone who has the cable. What that person can and cannot
  do:

| | Allowed | Why |
| --- | --- | --- |
| State | `STATUS`, `LOG`, `BENCH`, `REBOOT`, `BOOTLOADER` | what the link is for |
| Input | six keys, one-finger taps and swipes | to drive a test from a script. Never the power key: no sleep, no power-off from the cable. Injected input can do what a finger on that reader can do, no more: it opens the same screens, and it meets the same lock screen |
| Read files | `/debug`, `/screenshots`, the bench log and its setting, the battery log | the logs and pictures a test produces |
| Write files | `/debug/...`, `/.vaultink/bench.cfg` | test notes, and the bench interval |
| Flash | a `.bin` on the card that passes every check of the Settings screen (chip, board, checksum, SHA-256) | the same door the Settings screen has; a wrong or broken image is refused, and the reader does not restart on its own |
| **Never** | `/.crosspoint` (settings, Wi-Fi, OPDS, KOReader, WebDAV and FTP passwords), `/Vaults`, `/books`, the lock PIN, the LiveSync profile, anything else on the card | notes and passwords must not leave over a debug port |

- The file rule is an allow-list: a path is refused unless it is one of the entries above, so a new
  folder on the card is closed until someone opens it here. The path checks reuse
  `src/network/ProtectedPaths.h` (the web server's rule for the password files and the device
  folders), and traversal, case, 8.3 aliases and double slashes are tested.
- There are no settings commands: the link cannot read or change a setting, except through the
  screen like a finger.
- What injected input shows on screen can be photographed with `SCREENSHOT`, in every build as
  before. A developer reader with an open vault on screen shows that vault to the cable. Keep the
  developer build off a reader that holds things you would not show.
- `x4pro-dev-verify` checks more than a release does, not less. It is still a developer build.

**Next slice, not built:** read and change settings; a Home key and two-finger gestures for the
input queue. If any part of the link ever ships to users it goes behind a Developer setting that
is off by default.

## JTAG, one try

The ESP32-S3 has a JTAG adapter on the same USB port (USB Serial/JTAG; the chip's D- and D+ are
GPIO19 and GPIO20). The firmware's serial log already goes through that peripheral
(`ARDUINO_USB_MODE=1`), so the socket that shows the log is the one JTAG uses. JTAG itself has not
been tried on an X4 Pro.

1. Reader awake, cable in, not in USB Drive mode.
2. `pio debug -e x4pro-dev --interface=gdb -- -x .pioinit`
   The first run downloads `tool-openocd-esp32`. It then builds (still `-Os`, with symbols), **flashes
   that build through JTAG**, resets and stops at `app_main`.
3. In gdb: `continue`, Ctrl-C, `bt`, `info threads`. `thb <function>` sets a one-time hardware
   breakpoint (the code runs from flash, so use hardware breakpoints).

If OpenOCD cannot open the adapter on Linux, check the USB permissions first (PlatformIO has udev
rules, `99-platformio-udev.rules`; whether they are needed here is not checked). To debug what is
already on the reader without flashing, add `--load-mode manual` (the symbols then match only if
that firmware came from this same debug build). JTAG stops at sleep and in USB Drive mode like the
serial port does. Write down what happened in `notes/`: this has not been tried.

## Live test: GitHub's certificate

Releases do not check GitHub's certificate yet (`tlspolicy::VERIFY_GITHUB` is false; ROADMAP,
Release B gate). `x4pro-dev-verify` is the build that does, so the check can be seen working on a
reader before it is switched on for everyone:

```sh
pio run -e x4pro-dev-verify -t upload
scripts/devlink.py cmd LOG 2          # debug lines on, or Settings → System → Debug Logs
pio device monitor -e x4pro-dev-verify
```

It is `x4pro-dev` plus `-DVAULTINK_VERIFY_GITHUB=1`. No other build sets that flag, and without it
the constant is false as before.

Then, with the log on screen:

1. The update screen in Settings (the update check, `api.github.com`).
2. The font download screen: open the list and download one family (the list comes from GitHub;
   the font files come over plain http after GitHub's redirect and are checked by their CRC, so
   only the list and the redirect are a certificate test).
3. A Wikipedia lookup (always checked, in every build): the control.
4. If a newer release exists: install it from the update screen. That downloads the firmware with
   the check on, and replaces this build with the release.

What to look for (tags: `OTA`, `HTTP`, `TLS`, `FONT`):

| Line | Meaning |
| --- | --- |
| `[DBG] [OTA] Checking for update (current: 0.74.0-x4pro-dev)` | the check started |
| `[DBG] [HTTP] wolfSSL GET: https://api.github.com/... (heap N, max block M)` | the request is about to be sent; note the two heap numbers, the P-384 handshake needs room |
| `[DBG] [OTA] Parser results: tag=yes firmware=yes` | **pass**: the answer arrived over the checked connection |
| `[INF] [OTA] Developer build ..., latest release ...` | the result of the check |
| `[ERR] [TLS] The date could not be set, so the server's certificate cannot be checked. (api.github.com)` | **fail before the handshake**: the clock is not set and the time sync failed |
| `[ERR] [HTTP] wolfSSL request failed: status -1: https://...` then `[ERR] [OTA] Release check fetch failed` | **fail**: no answer. The source gives -1 for a bad URL, DNS, connect and TLS alike, so repeat the step on `x4pro-dev` (same build, check off): if it works there, the certificate check is what failed |
| `[ERR] [FONT] Failed to fetch manifest from ...` | the font list failed, read the `HTTP` line above it |
| a restart during the request | not seen yet, so not known what it prints; ROADMAP names too little heap for the P-384 handshake as the risk. Note the heap numbers of the `wolfSSL GET` line before it |

There is no log line that says "certificate checked". The proof is the pair: the request works on
`x4pro-dev-verify`, and `STATUS` there shows the version of this build. Which line a failed
handshake prints on a real reader is not known yet; the table is read from the source. Write the
result, the heap numbers and the log in `notes/`.

## Recipe: what costs power while awake

The cable charges the battery, so the battery numbers come from unplugged runs and the link fills
in the rest.

What the link shows with the cable in, a few minutes each:

```sh
scripts/devlink.py bench --every 10 --minutes 5 --out awake-light-off.csv
```

- `cpu_mhz` should fall to 80 once `idle_ms` passes 3000, and stay there on a page nobody touches.
  If it stays at 160, something holds the CPU up: `activity` says which screen.
- `light_on` and `light_brightness` confirm the state of each run.
- With auto-sleep at 5 minutes, the rows turn to `link` = `none` when `idle_ms` passes 300000.

The A/B runs for the drain itself, one change at a time, each from a similar battery level:

1. Note `battery_mv` and `battery_percent` (`devlink.py status`), then unplug.
2. Leave the reader 30 minutes on the same page, auto-sleep set to Never: once with the light off,
   once with it on, once with Wi-Fi off if it was on.
3. Plug in and run `devlink.py status` at once: the reading still has the cable in, so take it in the
   first seconds and treat it as rough. The battery log (`/.vaultink/battery-log.tsv`) and the
   Battery page are the unplugged record.

Or let the reader keep the record: `devlink.py cmd BENCH 5` before unplugging, do the runs one after
the other, then `devlink.py bench-file` on the copied file ([The SD bench log](#the-sd-bench-log)).
Turn it off afterwards with `BENCH 0`.

A separate run with auto-sleep at 5 minutes instead of 10 shows what the shorter setting saves.

## What is not checked yet

Everything on the device:

- that `x4pro-dev` and `x4pro-dev-verify` compile and fit, and what they cost in flash; that the
  `x4pro` build is the size it was (its flags were moved to a shared `[x4pro_flags]` section in
  platformio.ini, and the command table grew by one name);
- the SD bench log on a reader: that the rows arrive, what one row costs, the rename at 256 KB, and
  that the setting file is read after a power-off;
- the label on the About and update screens, and the whole
  [live test](#live-test-githubs-certificate);
- every command's reply from a real reader, and that a reply is never cut or mixed with a log line
  (replies wait for room in a 1024-byte send queue and go out as one write);
- `KEY`, `TAP` and `SWIPE` on a reader: that an injected key passes the input library's debounce
  through its button hook (tested on a computer against a copy of the debounce rule, not against
  the library), that a tap lands on the pixel asked for in each orientation, that `idle_ms` starts
  again, and that the pacing is right for the real loop;
- `LS`, `GET` and `PUT` on a real card: the names a listing returns, how long a 4 KB `GET` takes
  and how often a log line cuts it, appending with `PUT`;
- `FLASH` on a reader: the slot label it reports, how long it takes, that the reader keeps running
  the old firmware until `REBOOT` and starts the new one after it, and that the serial port stays
  up while the flash is written;
- that the `x4pro` build did not grow: the seven new names and all new code are inside
  `#ifdef VAULTINK_DEVLINK` (lib/DevLink, lib/hal/HalGPIO, src/util/DevLinkDevice), which only
  `x4pro-dev` and `x4pro-dev-verify` define. No device build was run;
- `BOOTLOADER`: it sets `RTC_CNTL_FORCE_DOWNLOAD_BOOT` and restarts, the same write the Arduino core
  makes in `usb_persist_restart(RESTART_BOOTLOADER)`. That the X4 Pro then shows up for `esptool` is
  untested;
- `charging` on the X4 Pro's battery chip;
- whether opening or closing the port from the script resets the reader (the script leaves DTR and
  RTS alone on close);
- the screenshot over a real cable: the firmware sends 48,000 bytes in one write with a 1 ms send
  timeout, so bytes can be lost. The script then reports that `SCREENSHOT_END` did not follow;
- JTAG as a whole;
- the script on Windows (pyserial path).

## Known limits from the review (0.74.0 + 1)

- **BOOTLOADER on battery:** the ROM download mode does not run the app's power hold. With the cable plugged in it waits for esptool; on battery it either powers off or sits there and drains the pack until a chip reset. Use it with the cable in. Nothing is saved before REBOOT or BOOTLOADER. Not checked on a real X4 Pro.
- **STATUS can pause the screen for a moment:** it takes the render lock and reads the battery gauge over I2C, and sends its reply with the 1 ms TX timeout. A STATUS during a long redraw waits for it, and a polling host (the `bench` mode) may log a `none` row for that poll. Poll no faster than once a minute.
- **CMD:LOG 0** switches off the same debug flag the SD "Debug Logs" setting uses, so the SD log loses its debug lines until the next boot.
- **A stray fragment** (bytes without a newline, such as a typed key) is forgotten after one second of silence; `devlink.py` also sends a bare newline before each command.
- **Screenshot framing:** the 48,000 bytes go out in one write under the 1 ms TX timeout, and log lines from other tasks can land inside them (this was always so). The script reports a missing SCREENSHOT_END; take the screenshot again, with debug logs off.
- **devlink.py port search:** with no `--port` it uses `/dev/ttyACM*`; if another device is plugged in, name the port.
