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
* ⏳ VaultInk logo for boot, sleep and web UI (Lef picks from drafts).
* ⏳ Public repo Somnius/VaultInk.

## ⏳ Phase 1 - Vault reader

**Goal:** open a real vault from the SD card and read it like in Obsidian.

* Vault picker, folder browser, recent notes, Greek paths.
* `lib/Vault`: Markdown parser (spans, tables, callouts, tasks, code, links, embeds, frontmatter).
* Note reader with links, back stack, image and note embeds, block refs.
* Index on SD, incremental, for vaults of 60 MB and more.
* Search with Greek folding, tags, backlinks.

**Done when:** synthetic test vaults pass the host tests, and Lef's own vaults (tested locally only,
never in the repo) open and render every feature they use (SPECS.md 5.1) in the simulator.

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

## 💤 Phase 6 - Android companion

* Separate repo Somnius/VaultInk-Companion (`com.lefteros.vaultink`).
* Type into the device, quick notes, settings, signed APKs with an in-app update checker.

## 💤 Phase 7 - PDF trial

* Render one PDF page on the ESP32-S3 with PSRAM; go/no-go. Can run early as a spike.

## 💤 Phase 8 - Website

* lefteros.com/apps/vaultink and a VaultInk donation page (separate from CrossInk's).

## 💤 Battery (needs the device)

* Already in (from CrossPoint): CPU drops to 80 MHz after 3 s idle, deep sleep with the panel off
  after the auto-sleep time (Settings, 1-30 min or never).
* Light sleep between page turns and while a page is shown. Measure current on the X4 Pro before
  and after; keep it only if it saves power without slowing page turns.

## 💤 Later

* Audio attachments, Excalidraw drawings.
* Indic and Burmese text shaping and fonts.
* Other devices.

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
