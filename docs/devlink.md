# Developer link

A few commands over the USB cable for working on the firmware: ask the reader for its state, reboot
it, put it in flashing mode without holding a button, and log its state to a CSV file during a test.

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

`x4pro-dev` is `x4pro` (same board, partition table, flags and `-Os`) plus three things:

- `-DVAULTINK_DEVLINK=1` compiles the commands in.
- `-DLOG_DBG_RUNTIME=1` makes debug log lines switchable, so `CMD:LOG` has something to switch. They
  start off, unless Settings → System → Debug Logs is on.
- `debug_tool = esp-builtin` for JTAG (below).

## Commands

Send a line `CMD:<NAME> [arguments]` ending in a newline, at any baud rate (it is a USB port). The
reader answers with one line of JSON: `{"ok":true,...}` or `{"ok":false,"error":"..."}`. Log lines
share the port, so read lines until one parses as a JSON object with an `ok` field.
`scripts/devlink.py` does that.

| Command | Reply |
| --- | --- |
| `PING` | `{"ok":true,"pong":true,"uptime_ms":...}` |
| `HELP` | `{"ok":true,"commands":"PING HELP STATUS LOG REBOOT BOOTLOADER SCREENSHOT"}` |
| `STATUS` | the fields below |
| `LOG <0-3>` | 2 and 3 turn debug lines on, 0 and 1 turn them off. Errors and info lines always print: the levels are fixed when the firmware is compiled, and the reply's `note` says so |
| `REBOOT` | `{"ok":true,"action":"reboot"}`, then a restart 100 ms later. Like the reset button: nothing is saved first |
| `BOOTLOADER` | `{"ok":true,"action":"bootloader"}`, then a restart into the chip's download mode, where `esptool` and `pio run -t upload` can flash it. Leaving it without flashing needs a reset of the chip; how to do that on an X4 Pro is not checked |
| `SCREENSHOT` | not JSON: `SCREENSHOT_START:<n>`, n raw bytes, `SCREENSHOT_END` (every build) |
| anything else | `{"ok":false,"error":"unknown"}` |

A line longer than 256 bytes is dropped whole. Names are case-sensitive.

`STATUS` fields:

| Field | Meaning |
| --- | --- |
| `version` | the firmware version, e.g. `0.74.0-x4pro` |
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
| `dropped_lines` | lines thrown away for being over 256 bytes |

## The host script

`scripts/devlink.py` needs Python 3 and nothing else on Linux and macOS.

```sh
scripts/devlink.py status                     # STATUS, a field a line
scripts/devlink.py cmd LOG 2                  # any command, the JSON reply
scripts/devlink.py screenshot shot.png        # or .bmp; upright, like Power + Down
scripts/devlink.py bench --every 60 --minutes 30 --out idle.csv
```

It uses `--port`, else `$DEVLINK_PORT`, else the first `/dev/serial/by-id/*Espressif*JTAG*`, else the
first `/dev/ttyACM*`. The exit code is 0 for `ok:true`, 1 for `ok:false`, 2 when there is no port or
no answer.

`bench` opens the port again for every poll and writes a row each time: `time, epoch, link,
battery_mv, battery_percent, heap_free, cpu_mhz, light_on, light_brightness, activity, usb, charging,
idle_ms, uptime_ms`. When the reader does not answer, the row has `link` = `none` and the rest empty,
and the next poll tries again.

## Limits

- **The link dies at sleep.** The firmware closes the USB serial port before deep sleep, and
  `/dev/ttyACM*` goes away. It comes back when the reader wakes. So the link cannot watch sleep, and
  a test longer than the auto-sleep time has gaps unless you set auto-sleep to Never.
- **USB Drive mode takes the port.** While File Transfer → USB Drive is open there is no serial port
  and no JTAG. They return when you leave it (the reader restarts).
- **The cable feeds the reader.** The link needs USB, so every `battery_mv` and `battery_percent` it
  reads is taken with the cable in. They do not show what the battery loses on its own.
- **A command needs its newline.** Before, `CMD:SCREENSHOT` without one still ran after a second's
  wait, during which the reader did nothing else. Now nothing waits and a line counts when it ends.
- One reply is 767 bytes at most; a longer one comes back as `{"ok":false,"error":"reply too long"}`.

## Security

A debug port that can read the SD card can read vault notes and saved passwords, with no more than
a cable. So:

- The commands are compiled into `x4pro-dev` only, and it must stay that way: never add
  `VAULTINK_DEVLINK` to a release build.
- This first slice only reads the device's state and restarts it. It has no access to files,
  settings or input.

**Next slice, not built:** list, read and write SD files; read and change settings; key and touch
injection (`CMD:KEY`, `CMD:TAP x y`); `CMD:FLASH <sd path>` through the existing SD firmware
flasher. If any of it ever ships to users it goes behind a Developer setting that is off by default.

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

A separate run with auto-sleep at 5 minutes instead of 10 shows what the shorter setting saves.

## What is not checked yet

Everything on the device:

- that `x4pro-dev` compiles and fits, and what it costs in flash;
- every command's reply from a real reader, and that a reply is never cut or mixed with a log line
  (replies wait for room in a 1024-byte send queue and go out as one write);
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
