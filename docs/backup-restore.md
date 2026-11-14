# Back up and restore the X4 Pro's flash

A page that copies everything stored in the reader's own memory into one file on your computer,
and writes such a file back: <https://somnius.github.io/VaultInk/backup.html>

"Everything" is the whole 16 MB flash chip: the bootloader, the partition table, the firmware
(stock or VaultInk), and the settings the firmware keeps in the reader. The SD card is not part
of it.

**Experimental: not tested on a real reader yet.** The page's logic is tested on a computer with a
simulated reader (`node test/backup_test.js`), and the page was opened in a desktop browser without
a reader. Reading a backup, writing one back, the restart after it and the automatic download mode
have not been tried on an X4 Pro. [What is not checked yet](#what-is-not-checked-yet) lists it.

**Nothing is uploaded.** The page talks to the reader inside your browser, and the backup is a file
you save yourself. When you press Connect the browser downloads one library from the public
jsDelivr server (esptool-js, see [Credits](#credits)); that request carries nothing about your
reader.

## Why make a backup

Installing VaultInk replaces the stock firmware. A backup made **before** the install is a copy of
that stock firmware and of everything around it, and writing it back puts the reader in the state
it was in. Keep the file: a backup of the stock firmware is the only way back to it that this
project can offer.

## What you need

- An Xteink X4 Pro (an ESP32-S3 with 16 MB of flash; the page refuses anything else) and a USB-C
  **data** cable (some cables only charge).
- A computer with desktop **Chrome**, **Edge** or **Opera**. The page uses Web Serial. Firefox,
  Safari and browsers on phones and tablets are not supported.
- A charged battery, for a restore.

## 1. Connect

Press **Connect** and choose the reader in the browser's list (the list shows only devices with
Espressif's USB vendor id, 0x303a). The page restarts the chip into its download mode, and shows
the chip, the flash size and the MAC address.

The page tries to enter download mode by itself, with the USB reset of esptool-js. We assume that
works while VaultInk is running and do not know whether it works from the stock firmware; neither
is tested. If Connect fails, put the reader into boot mode by hand, as for installing:

1. With the reader switched off, press and hold **Power**, then also press and hold **both
   page-turn buttons**, and keep holding them.
2. Connect it to your computer with the USB-C data cable.
3. Press **Connect** again.

While connected the reader stays in download mode and its screen does not change. **Restart the
reader** leaves that mode and disconnects. If the reader does not start, restart it with Power.

## 2. Read the flash (backup)

Press **Read the flash (backup)**. The page reads all 16,777,216 bytes, in 64 pieces of 256 kB,
and shows the bytes read and the rate. It takes several minutes; how many is not measured yet. Do
not unplug while the bar moves. If a piece comes back short, the page stops and saves nothing.

Then it shows the file's SHA-256 and a download link. The name is
`vaultink-x4pro-backup-<mac>-<YYYY-MM-DD>.bin`, with the reader's MAC address and the day. The file
is only in the browser tab until you download it, so download it before you close the tab. Write
the SHA-256 down, or keep it next to the file: it tells you later that the file has not changed.

Reading does not change anything on the reader.

## 3. Write a backup back (restore)

1. Connect, as above.
2. Choose the backup file. The page accepts only a file of exactly 16,777,216 bytes whose first
   byte is 0xE9 (the start of a bootloader). It shows the file's SHA-256; compare it with the one
   from the day you made the backup.
3. If the MAC address in the file's name is not the connected reader's, the page warns. A backup of
   another reader carries that reader's settings and secret. Writing one to a different reader is
   not tested.
4. Tick **This replaces everything on the reader, bootloader included.**
5. Press **Write the backup to the reader**. The page compresses the file first (the browser can
   stop responding for a while), sends it, and then compares the MD5 of what is in the flash with
   the file's. Do not unplug while the bar moves.
6. The page restarts the reader. If the screen does not change, restart it with Power.

## Safety

- **Do not share a backup.** It holds the reader's secret and its settings. Do not attach it to an
  issue or send it to anyone.
- **A failed restore can be tried again.** Download mode is in the chip's ROM and cannot be
  overwritten, so a reader whose restore stopped halfway should still enter download mode (the
  boot mode above) and take the backup again over USB. We have not tried this.
- **Do not unplug in the middle of a write**, and use a cable you trust and a charged battery.
- The page checks the file's size and first byte, and after writing that the flash holds the same
  bytes. It cannot tell whether the file is a firmware that works.
- A reader with Secure Download Mode on is refused: its flash cannot be read this way.

## Limits

- X4 Pro only (ESP32-S3, 16 MB). Other Xteink readers are refused.
- The whole flash only: the page does not read or write single partitions.
- Desktop Chromium browsers only.
- The page needs the internet once, to load esptool-js. It checks the downloaded file against a
  fixed SHA-384 and does not run a file that differs.

## What is not checked yet

Nothing here has run on a real reader. To check on an X4 Pro:

- Connect from running VaultInk, and from the stock firmware, without the buttons.
- Connect after entering boot mode with the buttons.
- A full read: that it finishes, how long it takes, and that two reads give the same SHA-256
  (where the reader was not started in between).
- A restore of that backup: the write, the MD5 check, how long the compression blocks the browser.
- The restart after a restore and from the **Restart the reader** button: that the reader starts
  the firmware and does not come back in download mode.
- Unplugging in the middle of a restore, then restoring again.
- Windows and macOS (only a Linux desktop browser opened the page, without a reader).

## Credits

- [esptool-js](https://github.com/espressif/esptool-js) 0.7.0 by Espressif (Apache-2.0) does the
  talking to the chip. Its bundle includes [pako](https://github.com/nodeca/pako) 2.1.0
  (MIT AND Zlib) for compression. Both are in [licenses.md](licenses.md).
- The restart after a restore follows the open flasher of [explorink.com](https://explorink.com)
  (MIT): on the ESP32-S3's own USB port, clear the bit that makes the chip start its loader again,
  switch DTR off first, and only then pulse RTS. Their source explains why a plain reset can drop
  the board back into the loader; the code here is our own.

For developers: the logic is in `site/backuplib.js` (no page, no serial port in it) and its tests
are `node test/backup_test.js`.
