# VaultInk companion (Android)

The companion is an Android app that talks to the device over Wi-Fi, through the web server the
device runs while **File Transfer** is open. Its code is in [`companion/`](../companion/) in this
repo, and it has its own releases, tagged `companion-x.y.z`.

Status: `companion-0.1.0` is out (needs VaultInk 0.29.0 or newer on the device). Download the APK
from the [companion-0.1.0 release](https://github.com/Somnius/VaultInk/releases/tag/companion-0.1.0)
on the phone and open it. It's tested on one phone (Android 16) against the simulator, not yet
against a real X4 Pro. See "Tested so far" below.

## First start

Three short pages: what the app is (the reader's companion), what the reader needs (File Transfer
open, with **Join a Network** or **Create Hotspot**), and what each tab does. **Skip** or **Start**
goes to the connect step, Back goes to the previous page, and **Show the intro again** on the
Device tab brings it back. Each tab then has a one-line hint at the top.

## Connecting

1. On the device, open **File Transfer** (it announces itself on the network as `vaultink.local`).
2. Join the phone to the same Wi-Fi, or to the device's hotspot.
3. Open the app. It tries the last address that worked, then `vaultink.local`, then looks for the
   device on the network (mDNS, `_http._tcp`, named "vaultink"). If none answers, type the address
   the device shows (for example `192.168.1.23`). While it searches, the app shows what the reader
   needs; if nothing answers, it lists the two steps again with **Search again** and the address
   field.

The connection is plain HTTP on your local network, like the device's web page. In hotspot mode
anyone who joins the device's Wi-Fi can read and change the card; the app warns about it.

## Tabs

### Note

Pick a vault (the folders in `/Vaults`) and a folder (default `Inbox`), write a title and text,
and **Send to VaultInk**. The note is saved as `<title>.md` with the title as its heading. A name
that's already there gets `-2`, `-3`, ... added. The draft stays until it's saved.

Sharing from another app works too: text and pictures shared to VaultInk open in this tab. The
first line of shared text becomes the title. Pictures go to the vault's `attachments` folder and
the note links them with `![[...]]`.

Notes saved this way, or uploaded through the web page into `/Vaults`, are marked as changed, so
the device's next LiveSync sync sends them to the server (from 0.29.0).

### Files

The whole SD card:

- Tap a folder to open it; the path at the top ("SD card › books") takes you back up.
- **Upload** picks one or more files on the phone and sends them to the open folder, with a
  progress bar.
- **New folder** makes a folder here.
- The **⋮** menu of a file: **Download to phone** (you pick where it's saved, with a progress
  bar), **Rename**, **Move** (browse to the folder and tap **Move here**), **Delete** (asks first).
- The ⋮ menu of a folder: **Delete**, for an empty folder only.

What the device's web server allows, the app allows: folders can't be renamed or moved, a
non-empty folder can't be deleted, an upload never overwrites a file with the same name, and the
saved Wi-Fi/OPDS/KOReader credential files and system folders can't be touched. A book you rename or
move keeps its reading place on the reader (from VaultInk 0.35.1).

### Comics: Files → Convert a comic (0.2.0)

Opens Banana Comic Converter (by u/curseofthebanana, an independent tool) inside the app, live from
its site. Its downloads are caught and offered as **Send to VaultInk** (to `/Comics` on the card,
with a progress bar) or **Save to phone**. The page gets a message channel to the app only from its
own site; other links open in the browser; an unreachable site shows a retry screen. Details and
screenshots: [comics.md](comics.md).

### Settings

The device's settings, one card per category (Display, Reader, ...). Tap a card's title to open or
close it. Switches, choices and numbers change on the device straight away.

### Device

The address and firmware version, **Search again**, and **Check for app update**: the app looks
for a newer `companion-*` release on GitHub, downloads the APK and opens Android's install prompt.

## Look

The app follows the phone's light or dark mode, with an ink-blue header. The status bar (clock,
notifications, battery) stays visible in both.

## Languages

English and Greek (the phone's language picks one). "Vault" stays in English in Greek, as on the
device.

## Building

```bash
cd companion
taskset -c 0-23 nice -n 10 ./gradlew assembleRelease
```

The release build is signed from `companion/signing.properties`, which is not in the repo (it
points to the maintainer's keystore). Without it, `assembleDebug` still builds a debug APK.

Kotlin + Jetpack Compose, OkHttp; minSdk 26, targetSdk 36. Package `com.lefteros.vaultink`.

## Releases

Tag `companion-x.y.z`, asset `vaultink-companion-x.y.z.apk`. A companion release is never marked
**Latest**: the device's update check reads the latest release, and from 0.29.0 it also ignores
tags that aren't a plain `x.y.z` version.

## Tested so far

On a OnePlus phone (Android 16) against the simulator's web server, over USB debugging:

- Note: typed note saved with the right content; shared text; Greek file and folder names.
- Files: list, a 3 MB upload (byte for byte the same), a download (the same), rename, move into
  another folder, the delete prompt.
- Settings: a switch turned on and off on the device.
- Look: dark mode, status bar visible.
- Intro (English and Greek): the three pages, Next, Back to the previous page, Start, Show the
  intro again; the searching and not-found screens; the tab hints.
- Delete from the phone after the simulator fix.
- Sharing a picture with text: the picture saved in `attachments/` byte for byte, the note links
  it with `![[...]]`.
- Update check: "newest version" while no `companion-*` release exists. Downloading and installing
  an update can only be tried once a newer release is out.
- Greek tab names fit on one line (they broke mid-word at a large phone font before).

Not tested yet:

- Anything against a real X4 Pro over Wi-Fi, including finding it with mDNS.
- The progress bar at Wi-Fi speed (over USB the transfers finished in about a second).
- Delete in the simulator first came back with a garbled path. Found with ThreadSanitizer: the
  firmware frees the request's arguments after `handleClient()` to save memory, and in the
  simulator the handler runs on another thread, so they were freed under it. On the device the
  handler finishes inside `handleClient()`. Fixed for the simulator (0.29.0); 30 of 30 deletes pass.
- Installing an app update: tested 2026-10-29, 0.1.0 found companion-0.2.0, downloaded it and
  Android installed it (version 0.2.0 afterwards).
