# VaultInk roadmap

VaultInk is built in phases. Details for each feature are in [SPECS.md](SPECS.md); what is in and out of
scope is in [SCOPE.md](SCOPE.md). VaultInk is a fork of CrossPoint Reader; its upstream roadmap is at
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/ROADMAP.md).

Status: ✅ done · 🚧 in progress · ⏳ next · 💤 later · ❌ dropped

Target device: Xteink X4 Pro only. Until the device arrives, testing is in the simulator (X4 Pro mode)
and host tests.

---

## 🚧 Phase 0 - Setup

**Goal:** a VaultInk build that runs, with the project name, docs and update channel in place.

* ✅ Fork from CrossPoint `develop` (116195b4).
* ✅ Specs, roadmap and scope written.
* ✅ NOTICE (CrossPoint copyright kept, third-party licences).
* ⏳ README rebuilt as VaultInk: real simulator screenshots, honest feature list. Waits until the simulator
  can take screenshots.
* ✅ Rename to VaultInk (USB names, version 0.1.0, boot and sleep screens, web UI).
* ✅ OTA update check pointed at Somnius/VaultInk releases (`vaultink-<version>-x4pro.bin`, tags
  without "v").
* ✅ Simulator build with `-DSIMULATOR_DEVICE_X4_PRO` (touch).
* ✅ CI and release workflows for `x4pro` firmware and simulator only; issue templates for VaultInk.
* ✅ Greek UI strings (new strings need Lef's review).
* ✅ Greek monotonic and polytonic in the UI fonts and hyphenation.
* ✅ "Updating for new version..." message when a book's cache is rebuilt after an update.
* ✅ VaultInk logo (open book with a V) on boot, sleep and the web UI.
* ⏳ Public repo Somnius/VaultInk.

## ⏳ Phase 1 - Vault reader

**Goal:** open a real vault from the SD card and read it like in Obsidian.

* Bigger app slots: drop the `spiffs` partition if nothing uses it, for about 1.7 MiB more
  firmware space. Needs one USB flash.
* Home: **Vaults** right after **Books**. Vault picker, folder browser, recent notes, Greek paths.
* `lib/Vault`: Markdown parser (spans, tables, callouts, tasks, code, links, embeds, frontmatter).
* Note reader with links, back stack, image and note embeds, block refs.
* Index on SD, incremental, for vaults of 60 MB and more.
* Search with Greek folding, tags, backlinks.

**Done when:** synthetic test vaults pass the host tests, and Lef's own vaults (tested locally only,
never in the repo) open and render every feature they use (SPECS.md 5.1) in the simulator.

## ⏳ Phase 1b - New home, menus and reading stats

**Order:** the home and menu design comes first, before Phase 1's screens, so vault screens are
built once in the new style.

**Goal:** a home screen and menus that are clear to use on touch and buttons, not CrossPoint's
menus with a new name. Designed as screens first (simulator mock-ups), then built.

* Home with Books and Vaults first, then the rest; settings grouped by task.
* Built as a new home style and made the default; CrossPoint's styles stay selectable for now.
* Swipe up from the bottom for reading settings, swipe down from the top for quick settings.
* Reading statistics on the device: time, sessions, streaks, calendar, finished books; notes count too.
* Library speed: build covers when idle so key presses stay fast.
* Ideas taken from CrossNext (MIT) are credited in NOTICE.

## 💤 Phase 2 - Canvas, read only

* Streaming `.canvas` reader, cards, groups, arrows with labels.
* Fit, pan, zoom, low-detail drag frame, tap a card to open it.

## 💤 Phase 3 - LiveSync pull

* Setup URI import, E2EE V2, path obfuscation.
* CouchDB `_changes` with checkpoints, chunk assembly, attachments on demand.
* Local Docker CouchDB test setup and decryption test vectors.
* Rough effort from research: 1-2 weeks, plus about 1 week for encryption. Not reliable until the
  encryption details are pinned with test vectors.

## 💤 Phase 4 - Editing and LiveSync push

* Touch keyboard editor (English + Greek), atomic save, task toggles, new/rename/delete.
* Push with the plugin's chunker, conflict copies.
* Rough effort from research: 2-4 weeks for push.

## 💤 Phase 5 - Bluetooth keyboard

* BLE HID host (NimBLE), US and Greek layouts.
* Remote buttons on top of CrossPoint's BLE page turner (`lib/BlePageTurner`: up to 4 remotes,
  learned buttons, tap and hold): camera shutter remotes and page turners already work as BLE
  HID (volume / arrow keys). Add actions beyond page turns (open quick note, sync, home).
* Flic 2: pair directly in Flic's "Universal" (HID keyboard) mode, which needs no Flic app at
  run time; hold-to-repeat isn't supported by Flic in that mode. Or through a Flic Hub: a hub SDK
  package (JavaScript on the hub) sends clicks to VaultInk's web server on the LAN.
* Research and test list of other BLE buttons and remotes before claiming support.

## 💤 Phase 6 - Android companion

* Separate repo Somnius/VaultInk-Companion (`com.lefteros.vaultink`).
* Type into the device, quick notes, settings, signed APKs with an in-app update checker.
* Bring notes in: convert to Markdown into a vault folder (e.g. `Imported/Keep/`). Order:
  Android share target (any app) -> Google Keep (Takeout) -> Samsung Notes (`.sdocx` export) ->
  contacts (one note per contact plus a `.vcf` backup) -> formats Obsidian's Importer handles.
  Other apps' private databases can't be read without root, so only exports and sharing.

## 💤 Phase 7 - PDF trial

* Render one PDF page on the ESP32-S3 with PSRAM; go/no-go. Can run early as a spike.

## 💤 Phase 8 - Website

* lefteros.com/apps/vaultink and a VaultInk donation page (separate from CrossInk's).

## 💤 Phase 9 - Watch-only wallet

* Bitcoin xpub/descriptor or address list; receive address QR; opt-in balances from a server the
  user picks (self-hosted recommended). No keys or seeds on the device, ever (SPECS.md 11a).
* Wallet file encrypted on the SD card; unlock with PIN plus the paired phone (companion app over
  BLE), or a passphrase. Companion scans wallet QR codes. Needs Phase 5 (BLE) and Phase 6.
* Done when: BIP84 test vectors pass on the host, and the simulator shows addresses and a QR.

## 💤 SD-card plugins

* Extend VaultInk with SD-card plugins instead of adding to the firmware.
* Plugins are web pages and on-device catalogs (download, sign in, reading events); they can't run
  code on the device, so games and timers can't be plugins.
* Bundled from CrossPoint's Plugin Hub releases (not copied into this repo; licences checked
  first): Dictionaries, WebDAV, Send2Ereader. Send2Ereader uses a third-party server, so it is
  off until the user turns it on. Later: a wallpaper plugin. Check for a Greek dictionary.
* Reading Statistics: built into the firmware (Phase 1b) instead of the plugin; read the plugin's
  history format so existing stats can be imported.
* VaultInk plugins to write:
  * Vault upload from the browser, then hand the vault to LiveSync so only changes travel later
    (saves battery and data on big vaults).
  * Quick note from the phone into a vault inbox.
  * Highlights from books exported to a vault note.
  * Vault from a Git repo, public or private, with a fine-grained GitHub token (read-only,
    one repo), stored only on the SD card.
  * Anki deck download.

## 💤 Later from CrossNext

* Flashcards built from vault notes (spaced repetition), Habits, Pomodoro.

## 💤 Battery (needs the device)

* Already in (from CrossPoint): CPU drops to 80 MHz after 3 s idle, deep sleep with the panel off
  after the auto-sleep time (Settings, 1-30 min or never).
* Light sleep between page turns and while a page is shown. Measure current on the X4 Pro before
  and after; keep it only if it saves power without slowing page turns.

## 💤 Later

* Audio attachments, Excalidraw drawings.
* Indic and Burmese text shaping and fonts.
* More devices: M5Stack PaperMono / PaperMono Lite first, then Seeed reTerminal Sticky, then other
  Xteink models. Same 800x480 screen and 8 MB PSRAM; each needs UI checks and hardware tests.
  No test devices yet, so these stay simulator-only and unreleased until someone can test them.

---

## Risks

* The LiveSync database format is not a stable API (generation 12 to 13 this autumn). We pin tested
  versions and refuse unknown ones.
* Flash: the x4pro build uses 84.7% of the app slot after Phase 0 (~1.0 MB free). Vault, canvas, sync and
  editor code must fit in that, or flash is freed (e.g. built-in fonts or UI languages to SD).
* Big vaults and canvases must be streamed; nothing loads whole into RAM.
* E-ink canvas pan/zoom needs partial refresh to feel usable.
* Android silent self-update and PDF speed are unverified until tried.

## How this roadmap changes

* A phase starts when the previous one is done or an item is carried over with a note here.
* Changes are made by Lef.
