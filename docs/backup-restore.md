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

After connecting, the page switches off the chip's two watchdogs (see [Watchdogs](#watchdogs)).

While connected the reader stays in download mode and its screen does not change. **Restart the
reader** leaves that mode and disconnects. If the reader does not start, restart it with Power.
If the page says it could not clear the download-mode flag, the reader may re-enter download mode:
unplug it and power it on with the power button.

## 2. Read the flash (backup)

Press **Read the flash (backup)**. The page reads all 16,777,216 bytes, in 64 blocks of 256 kB,
and shows the bytes read and the rate. It takes several minutes; how many is not measured yet. Do
not unplug while the bar moves.

**Every block is checked against the device.** After each block the reader computes the MD5 of the
same 256 kB of its flash, and the page compares it with the MD5 of what arrived. If they differ, or
a block comes back short, the page stops, says at which offset, and saves nothing.

**Abort** stops the read after the block that is on its way and throws away what was read. Reading
does not change anything on the reader, so stopping is safe.

Then the page shows the file's SHA-256 and a download link. The name is
`vaultink-x4pro-backup-<mac>-<YYYY-MM-DD>.bin`, with the reader's MAC address and the day. The file
is only in the browser tab until you download it, so download it before you close the tab. Write
the SHA-256 down, or keep it next to the file: it tells you later that the file has not changed.

The page also runs the checks of step 3 on the new backup. If the backup would be refused there,
it says so next to the download link. Keep the file anyway and tell us: it would mean a firmware
laid out in a way the page does not know.

## 3. Write a backup back (restore)

1. Connect, as above.
2. Choose the backup file. The page reads it and accepts it only when all of this is true:
   - it is exactly 16,777,216 bytes;
   - it starts with a bootloader for the ESP32-S3: the byte 0xE9, 1 to 16 segments, and the chip id
     0x0009 in its header;
   - there is a partition table at 0x8000 (entries that start with the bytes AA 50), with at least
     one app partition, and no partition that ends after 16 MB;
   - at least one app partition starts with a firmware image for the ESP32-S3 (the same three
     header checks). The page looks where the table says the app partitions are, not at a fixed
     address.

   A file that is erased (all 0xFF) or empty fails the first of these. The page shows the file's
   SHA-256; compare it with the one from the day you made the backup.
3. If the file's name has no MAC address, the page says it cannot tell which reader the file is
   from. If the name has a MAC address that is not the connected reader's, Write stays locked until
   you also tick **The file is from a different reader (MAC differs): write anyway**. A backup of
   another reader carries that reader's settings and secret. Writing one to a different reader is
   not tested.
4. Tick **This replaces everything on the reader, bootloader included.** The ticks are cleared when
   you choose another file and whenever a reader connects or disconnects.
5. Press **Write the backup to the reader**. The page compresses the file first (the browser can
   stop responding for a while), sends it, and then compares the MD5 of what is in the flash with
   the file's. Do not unplug while the bar moves. **A write cannot be stopped safely**, so there is
   no Abort for it.
6. The page restarts the reader. If the screen does not change, restart it with Power.

## Watchdogs

The ESP32-S3 has two watchdogs (the RTC watchdog and the "super watchdog") that are not fed while
the chip sits in download mode on its own USB-Serial/JTAG port, and they can reset it in the middle
of a long read or write. Espressif's esptool switches them off for this port, and the page does the
same right after connecting, with the same register writes (`disable_watchdogs()` in esptool 5.1.2,
`esptool/targets/esp32s3.py`): unlock the RTC watchdog with the key 0x50D83AA1 at 0x600080B0, write
0 to its configuration at 0x60008098, lock it again; unlock the super watchdog with the key
0x8F1D312A at 0x600080B8, set its auto-feed bit (bit 31 of 0x600080B4), lock it again. esptool-js
0.7.0 does not do this by itself.

If one of these writes fails, the page shows a warning and goes on. The risk then: the reader
restarts by itself. During a read the backup just stops. During a write the flash is left half
written; enter download mode again (it is in the chip's ROM) and write the backup again.

Whether the sequence has the intended effect on the X4 Pro is not tested.

## Safety

- **Do not share a backup.** It holds the reader's secret and its settings. Do not attach it to an
  issue or send it to anyone.
- **A failed restore can be tried again.** Download mode is in the chip's ROM and cannot be
  overwritten, so a reader whose restore stopped halfway should still enter download mode (the
  boot mode above) and take the backup again over USB. We have not tried this.
- **Do not unplug in the middle of a write**, and use a cable you trust and a charged battery.
- The page cannot tell whether the file is a firmware that works. Its checks are about the shape
  of the file and about the bytes arriving unchanged.
- A reader with Secure Download Mode on is refused: its flash cannot be read this way.

## Limits

- X4 Pro only (ESP32-S3, 16 MB). Other Xteink readers are refused.
- The whole flash only: the page does not read or write single partitions.
- The partition table must be at 0x8000, ESP-IDF's default place and where VaultInk's is. Where
  the stock firmware keeps its table is not verified; if it is somewhere else, the page reads the
  backup but refuses to write it back (step 2 says so right after the read).
- Desktop Chromium browsers only.
- Memory: a backup is in the tab twice for a moment (the blocks, and once more in one piece for
  the SHA-256), about 32 MB; a restore holds the file and its compressed copy.
- The page needs the internet once, to load esptool-js. It checks the downloaded file against a
  fixed SHA-384 and does not run a file that differs.

## What is not checked yet

Nothing here has run on a real reader. To check on an X4 Pro:

- Connect from running VaultInk, and from the stock firmware, without the buttons. Also whether the
  port reports a disconnect while the chip restarts into download mode (the page writes that into
  its message box and goes on).
- Connect after entering boot mode with the buttons.
- The watchdog step: that it reports "done", and that a long read is not cut by a restart.
- A full read: that it finishes with every block checked, how long it takes, and that two reads
  give the same SHA-256 (where the reader was not started in between).
- A backup of the **stock** firmware: that step 2 does not say the restore would refuse it.
- A restore of that backup: the write, the MD5 check, how long the compression blocks the browser.
- The restart after a restore and from the **Restart the reader** button: that the reader starts
  the firmware and does not come back in download mode.
- Unplugging in the middle of a restore, then restoring again.
- Abort during a read, then a new read.
- Windows and macOS (only a Linux desktop browser opened the page, without a reader).

## Credits

- [esptool-js](https://github.com/espressif/esptool-js) 0.7.0 by Espressif (Apache-2.0) does the
  talking to the chip. Its bundle includes [pako](https://github.com/nodeca/pako) 2.1.0
  (MIT AND Zlib) for compression. Both are in [licenses.md](licenses.md).
- The restart after a restore follows the open flasher of [explorink.com](https://explorink.com)
  (MIT): on the ESP32-S3's own USB port, clear the bit that makes the chip start its loader again,
  switch DTR off first, and only then pulse RTS. Their source explains why a plain reset can drop
  the board back into the loader; the code here is our own.
- The watchdog registers and values are those of Espressif's
  [esptool](https://github.com/espressif/esptool) (GPL-2.0-or-later); the page has no esptool code
  in it, only the same register writes.
- The image and partition table layout is from ESP-IDF: `esp_app_format.h` (`esp_image_header_t`,
  `ESP_CHIP_ID_ESP32S3`, `ESP_IMAGE_MAX_SEGMENTS`) and `esp_flash_partitions.h`
  (`esp_partition_info_t`, `ESP_PARTITION_MAGIC`).

For developers: the logic is in `site/backuplib.js` (no page, no serial port in it) and its tests
are `node test/backup_test.js`. The test's mocked loader has only the methods esptool-js 0.7.0
really has and fails on any other. Two optional checks use real files:
`BACKUP_TEST_ESPTOOL_BUNDLE=<bundle.js of esptool-js 0.7.0>` and
`BACKUP_TEST_FACTORY_IMAGE=<a VaultInk x4pro factory image>`.
