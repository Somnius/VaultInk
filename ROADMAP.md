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
* ✅ README rebuilt as VaultInk, with simulator screenshots and an honest feature list.
* ✅ Rename to VaultInk (USB names, version 0.1.0, boot and sleep screens, web UI).
* ✅ OTA update check pointed at Somnius/VaultInk releases (`vaultink-<version>-x4pro.bin`, tags
  without "v").
* ✅ Simulator build with `-DSIMULATOR_DEVICE_X4_PRO` (touch).
* ✅ CI and release workflows for `x4pro` firmware and simulator only; issue templates for VaultInk.
* ✅ Language list puts English and Ελληνικά first (0.8.1; Greek was 12th, below the first screen,
  and a tester missed it).
* ✅ Greek UI strings. ⏳ Lef reviews the strings written for VaultInk.
* ✅ Greek monotonic and polytonic in the UI fonts and hyphenation.
* ✅ "Updating for new version..." message when a book's cache is rebuilt after an update.
* ✅ VaultInk logo (open book with a V) on boot, sleep and the web UI.
* ✅ Public repo Somnius/VaultInk, history starting from an import of CrossPoint (credited in the
  first commit, LICENSE and NOTICE); CrossPoint's agent, governance and contributing files left out.
* ✅ About screen: logo, what VaultInk is based on (CrossPoint Reader, FreeInk SDK, ideas from
  CrossNext and KOReader SimpleUI), thanks to kos25k for testing.
* ✅ Welcome guide on the first start (five pages with pictures), again once after an update that
  changes it (switch in Settings → System), and any time from Settings → System → Welcome Guide.
* ✅ README: the welcome guide's pages as illustrated instructions.
* ✅ Translation guide (`docs/translating.md`), a GitHub issue form to request a language, and
  `scripts/i18n_status.py` to see what each language is missing.
* ✅ Web installer on GitHub Pages (somnius.github.io/VaultInk): ESP Web Tools 10.4.0; the page and
  the locally built full image are pushed to the `gh-pages` branch (nothing is built on GitHub) (release
  assets have no CORS headers). Marked as a test build; the button unlocks after ticking "I
  understand this is untested". ✅ Device picker (0.8.0); X4 Pro steps (0.8.1): back up the
  SD card, put the app bin (not factory) on it, boot mode (Power + both page buttons, from Lef),
  what erasing wipes; X4/X3 point to CrossPoint, since VaultInk
  is X4 Pro only.
* ✅ Fix: CrossPoint's `release.yml` was still building firmware on GitHub for every published
  release and replacing the locally built OTA file (`--clobber`). Found on 0.8.5; that run was
  cancelled before it uploaded. Removed `release.yml` and `release_candidate.yml`; local builds put
  back on 0.5.0 and 0.8.2–0.8.5. The GitHub-built OTA files on 0.6.0–0.8.1 were deleted (no local
  copy of those; OTA only ever takes the latest release).
* ✅ Test builds 0.2.0 to 0.8.9 published (web installer and GitHub releases, marked untested); the
  deploy script makes the release too, so the device's update check sees each one.
* 🚧 Device testing, by kos25k (X4 Pro, UC8279 display controller, GT911 touch, now on 0.8.2): 0.7.0 runs on his reader (boots, menus, quick settings, System
  settings, language list). Found: Greek hard to find in the language list (fixed 0.8.1), paper tile
  could rotate the book (fixed 0.8.2, rotation confirmed fixed on 0.8.2); backgrounds faded the page with text
  anti-aliasing on (fixed 0.8.3, not yet re-tested). Not tested on the
  device yet: vaults, notes, search, backlinks, keyboard, page backgrounds, Wi-Fi update, timing.
* ⏳ First hardware-tested release, once the X4 Pro arrives and passes the hardware checks below.

## 🚧 Phase 1 - Vault reader

**Goal:** open a real vault from the SD card and read it like in Obsidian.

* ✅ Bigger app slots: `spiffs` partition dropped (nothing used it), 7.94 MiB per slot
  (`partitions_vaultink.csv`). Firmware now uses 71% of a slot (0.8.0). Done before the first test build,
  so nobody needs an extra USB flash.
* ✅ Home: **Vaults** right after **Books**; vault picker (`/Vaults/*` and top-level folders with
  `.obsidian`), each vault opens in the folder browser. ⏳ Recent notes, Greek path checks on device.
* Continue card covers vault notes too; each vault remembers its last note.
* Settings: a Vaults group.
* 🚧 `lib/Vault`: Markdown parser. Done: `MarkdownToHtml` (headings, emphasis, highlight, strike,
  code, lists, tasks, quotes, callouts, tables, rules, frontmatter and comments hidden, block ids,
  wikilinks, Markdown and bare links, image embeds), 17 host tests, wired into the reader's `.md`
  path so notes render with the book reader. ✅ Links between notes (Obsidian resolution,
  headings, same-note jumps, Back through 16 notes, "Note not found") and embedded images found
  anywhere in the vault. ✅ Greek and polytonic in the built-in reading font, so notes and books
  read Greek without an SD font. ✅ (0.8.0) Nested lists indented, numbered lists keep their first
  number (`<ol start>`, EPUBs too), lists inside quotes, quotes indented, footnotes (raised number
  that links to the note's footnote), note embeds `![[Note]]` on their own line (the note's text
  under its title, one level, up to 32 KB). To do: embeds of a heading or block
  (`![[Note#Heading]]` shows the whole note for now); a note's cache isn't rebuilt when only an
  embedded note changes; callouts show only a bold title (no frame); `==highlight==` shows as an
  underline (same as links).
* Note reader with links, back stack, image and note embeds, block refs.
* 🚧 Index on SD, incremental, for vaults of 60 MB and more. ✅ Search index per vault (title,
  aliases, tags, outgoing links, words), updated by size and modification time. To do: use it to
  resolve links and images too, instead of the SD scan they use today (fine for small vaults, slow
  for thousands of notes), and store heading anchors.
* 🚧 Search: ✅ Search row on home, all vaults at once, Greek and accent folding, tags and aliases,
  exact words ranked first. ✅ Greek on-screen keyboard (with English, switched by the globe
  key; offered to the FreeInk SDK upstream). ✅ Backlinks: More → Backlinks in a note lists the
  notes that link to it; Back from one returns to the note (0.7.0).

**Done when:** synthetic test vaults pass the host tests, and Lef's own vaults (tested locally only,
never in the repo) open and render every feature they use (SPECS.md 5.1) in the simulator.

## 🚧 Phase 1b - New home, menus and reading stats

**Order:** the home and menu design comes first, before Phase 1's screens, so vault screens are
built once in the new style.

**Goal:** a home screen and menus that are clear to use on touch and buttons, not CrossPoint's
menus with a new name. Designed as screens first (simulator mock-ups), then built.

* ✅ Home with Books and Vaults first, then the rest, as the new default style.
* ✅ Settings grouped by task (Reading, Device).
* ✅ Search row on home.
* ⏳ Counts on the home rows (e.g. "128 books"), current values in the settings rows.
* Built as a new home style and made the default; CrossPoint's styles stay selectable for now.
* 🚧 Quick settings on CrossPoint's control centre (status-bar tap, top swipe or a bound button):
  done: Sleep / Transfer / Home tiles, book title and live progress over an EPUB. To do:
  progress from the XTC/TXT readers, Sync tile (Phase 3).
* ✅ Fix (0.8.9, kos25k's video, UC8279): closing the reading toolbar left a faint copy of it on the
  page (0.8.7 made the quick-settings case clear with a swipe, not this one). On UC8279 the page
  redraw after the toolbar closes now uses a Half cleanup refresh. Not yet re-tested on the device.
* ✅ Fix (0.8.7, kos25k on UC8279, photos): after quick settings closed, a faint image of the panel
  stayed on the glass and showed under the reading menu until a sleep/wake. Closing quick settings
  now hands a Half cleanup refresh to the screen underneath, and anti-aliased pages now honour such
  a promoted refresh too (they ignored it, so the refresh tile didn't clean them either). Not yet
  re-tested on the device.
* ✅ Hyphenation switch in the reading toolbar's Text panel (0.8.6, kos25k), so it can change
  while reading; before it was only in Settings.
* ✅ Fix (0.8.5, kos25k, from his video): option lists taller than the screen ran off the top
  (margins 0/1/2, and the in-book font sizes 8/9 with a .ttf were out of reach; it looked like a
  freeze). The popup now shows what fits around the current choice and pages through the rest with
  a "· · ·" row (tap) or the buttons.
* ✅ Fix (0.8.4, kos25k): the Wi-Fi file manager refused to move fonts into `/.fonts` ("Cannot move
  into protected folder": every dot folder counted as protected). `/.fonts` is allowed now.
* ✅ Screen margins down to 0 (kos25k): 0..40 px; the reader's margin list offers 0, 1, 2, 3, 4,
  5, 6, 8, 10, 12, 15, 20..40 (the list holds 16). Default stays 5.
* ✅ Fuller lines with Greek hyphenation (0.8.8, kos25k): Greek used the TeX default minima 2/2,
  which drop every break next to a one-letter syllable (ά-νε-μος, ι-δέ-α, ω-ραί-α); on 19 sample
  words that was 17 lost breaks. Greek and polytonic now use babel-greek's 1/1. Trade-off: a single
  letter can end a line (ε-νώ); can be limited to 2/1 if that reads badly. ✅ Font sizes
  in 1 pt steps work with a .ttf on the SD card (8..22 pt, confirmed by kos25k); the built-in font
  has 12/14/16/18.
* ✅ Fix (0.8.3, found by kos25k on a UC8279 X4 Pro): with a page background on, the text
  anti-aliasing pass faded the whole page, text included. AA is now skipped while a background is
  on (with AA off he saw the page stay sharp).
* ✅ Fix (0.8.2, reported by kos25k on the device): taps on the paper tile could land on the rotate
  tile above it and turn the book upside down. Unlabelled tiles now answer only on their circle,
  and the rows are further apart.
* ✅ Page backgrounds (kos25k's idea, mostly for fun): plain, paper, sandpaper, papyrus soft/medium/
  hard, toilet paper, kitchen paper, bath towel. In Settings → Reader and as a quick-settings tile.
  EPUB and notes only for now; XTC pages are pre-rendered. Ideas for more: parchment, linen,
  graph paper, kraft, newsprint, rice paper, cardboard, denim.
* ✅ Reader menu: CrossPoint's toolbar sheet (default on touch) kept; tool names added under the
  icons.
* ✅ Swipe down from the top for quick settings. ✅ Swipe up for the reading menu: CrossPoint's
  Settings → Controls → Show Reader Menu → Swipe Up (boards with a Home key, like the X4 Pro).
* Reading statistics on the device: time, sessions, streaks, calendar, finished books; notes count too.
* Library speed: build covers when idle so key presses stay fast.
* Greek name for the cover-grid style: Εξώφυλλα (done).
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

* Touch keyboard editor (English + Greek; the Greek layout is done, 0.5.0), atomic save, task toggles, new/rename/delete.
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

**When:** starts right after Phase 1 (it needs the vault picker, index and search on the device).
The first versions talk to the device over Wi-Fi through its existing web server; Bluetooth comes
with Phase 5. Kotlin, Android only.

* Separate repo Somnius/VaultInk-Companion (`com.lefteros.vaultink`).
* Type into the device, quick notes, settings, signed APKs with an in-app update checker.
* Bring notes in: convert to Markdown into a vault folder (e.g. `Imported/Keep/`). Order:
  Android share target (any app) -> Google Keep (Takeout) -> Samsung Notes (`.sdocx` export) ->
  contacts (one note per contact plus a `.vcf` backup) -> formats Obsidian's Importer handles.
  Other apps' private databases can't be read without root, so only exports and sharing.

## 💤 Phase 7 - PDF trial

* Render one PDF page on the ESP32-S3 with PSRAM; go/no-go. Can run early as a spike.

## 💤 Phase 8 - Website

* 🚧 App page lefteros.com/en/apps/vaultink with donations (being written in the lefteros.com project). ✅ Credited in About, README, the web installer and release notes (0.8.1).

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

## 💤 Fonts

* A "Suggested fonts" list in Settings → Reader → Manage Fonts, with English and Greek reading
  fonts the device downloads into `/.fonts/` on the SD card. Only fonts whose licence allows
  redistribution (e.g. OFL); many Windows fonts that Wine setups install (Microsoft core fonts)
  don't, so each font is checked first.
* Mark fonts that cover polytonic Greek.
* How: VaultInk publishes its own fonts catalog (the `.cpfont` format and manifest that Manage
  Fonts already reads, built by a fonts workflow into a separate VaultInk fonts repo), and the
  device's manifest URL points there instead of CrossPoint's.
* Checked 2026-10-26 by opening each font file (Google Fonts copies): full Greek **and polytonic**
  in Literata, Noto Serif, Noto Sans, Gentium Book Plus, Gentium Plus, Cardo, EB Garamond,
  Alegreya, GFS Didot, GFS Neohellenic, Fira Sans, Ubuntu. Monotonic Greek only: Source Serif 4,
  Vollkorn, Open Sans, Roboto, Commissioner. Little or no Greek in the Google Fonts copies:
  Charis SIL, Andika, PT Serif, Cormorant Garamond, Old Standard TT, Roboto Serif.
* Still to check: the other GFS families from the Greek Font Society's own site (OFL), and
  Liberation and Droid fonts (from the Winetricks list; redistributable, unlike the Microsoft
  fonts there). Variable fonts need static instances first (the converter can't set axes).

## 💤 Languages on the SD card

* Indic languages and Burmese as UI languages loaded from the SD card, not built into the
  firmware (they need their own fonts and text shaping). Check first whether the firmware can load
  a UI language from the SD card at all; if not, design that.

## 💤 Hardware checks (when the X4 Pro arrives)

* Boot screen, home rows, focus outline and ghosting, cover on the Continue card.
* A Greek folder name, Greek and polytonic books with an SD font, the cache "Updating" message.
* Quick settings tiles, welcome guide, About.
* Update check once a release exists.
* Scan the I2C bus for a motion sensor the stock firmware doesn't use (none expected).

## 💤 Battery (needs the device)

* Already in (from CrossPoint): CPU drops to 80 MHz after 3 s idle, deep sleep with the panel off
  after the auto-sleep time (Settings, 1-30 min or never).
* Light sleep between page turns and while a page is shown. Measure current on the X4 Pro before
  and after; keep it only if it saves power without slowing page turns.

## 💤 Later

* Audio attachments, Excalidraw drawings.
* Indic and Burmese text shaping and fonts.
* XTC and TXT readers share their live progress like the EPUB reader.
* More devices: M5Stack PaperMono / PaperMono Lite first, then Seeed reTerminal Sticky, then other
  Xteink models (the X3 and X4 Classic have a motion sensor: face-down sleep, lift to wake, tilt
  page turns). Same 800x480 screen and 8 MB PSRAM; each needs UI checks and hardware tests.
  No test devices yet, so these stay simulator-only and unreleased until someone can test them.

---

## Risks

* The LiveSync database format is not a stable API (generation 12 to 13 this autumn). We pin tested
  versions and refuse unknown ones.
* Flash: the x4pro build uses 67% of the (bigger) app slot (~2.7 MB free). Vault, canvas, sync and
  editor code must fit in that, or flash is freed (e.g. built-in fonts or UI languages to SD).
* Big vaults and canvases must be streamed; nothing loads whole into RAM.
* E-ink canvas pan/zoom needs partial refresh to feel usable.
* Android silent self-update and PDF speed are unverified until tried.

## How this roadmap changes

* A phase starts when the previous one is done or an item is carried over with a note here.
* Changes are made by Lef.
