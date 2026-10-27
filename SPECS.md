# VaultInk specification

VaultInk is e-reader firmware for the Xteink X4 Pro that reads books and works with Obsidian vaults.
It is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) (MIT).

- Works with Obsidian vaults.
- Supports Sync with Self-hosted LiveSync plugin.
- VaultInk is not affiliated with Obsidian (Dynalist Inc.) or with the Self-hosted LiveSync project.

Status words used below: **must** (needed for the phase to be done), **should** (wanted, can slip),
**later** (not planned for a phase yet). "Unverified" marks facts that still need a test.

## 1. Goals

1. A portable note device: read, search and edit an Obsidian vault away from the desktop.
2. Keep everything CrossPoint already does for books (EPUB, TXT, XTC).
3. English and Greek, for the UI and for vault content.
4. Fully free. No licences, device lock, accounts or payments. Donations go through a VaultInk page
   on lefteros.com.

Non-goals for now: other devices than the X4 Pro, iOS, Obsidian plugins (Dataview, Excalidraw, Bases),
graph view, audio, video, writing back canvas files.

## 2. Target hardware

| Item | Value | Source |
| --- | --- | --- |
| MCU | ESP32-S3 | `platformio.ini` env `x4pro` |
| Flash / PSRAM | 16 MB / 8 MB octal | board `esp32-s3-devkitc1-n16r8` |
| App partition | 7.94 MiB (0x7F0000) per OTA slot, no SPIFFS (was 6.25 MiB at fork) | `partitions_vaultink.csv` |
| Flash used at fork | 5,411,710 B (82.6%), ~1.1 MB free for VaultInk | `pio run -e x4pro`, 2026-10-26 |
| Flash after Phase 0 | 5,548,982 B (84.7%), ~1.0 MB free | `pio run -e x4pro`, 2026-10-26 |
| Panel | 800x480 e-ink (SSD1677, UC8179 or UC8279) | `freeink-sdk/.../BoardConfig.h` |
| SD | SDMMC 1-bit, SdFat block device | `platformio.ini` |
| Input | buttons + touch | handover, unverified on hardware |
| Radio | Wi-Fi + BLE | ESP32-S3 |

Hardware testing waits for Lef's device. Until then, testing uses
[crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator) with
`-DSIMULATOR_DEVICE_X4_PRO` plus host unit tests.

### 2.1 Languages and fonts

- UI languages: all CrossPoint languages stay; English and Greek are the ones VaultInk maintains.
- Built-in UI fonts (Ubuntu 8/10/12) include Greek and Coptic (U+0370-03FF) and Greek Extended
  (U+1F00-1FFF), so menus, file names and note names show monotonic and polytonic Greek.
- Reading text: the built-in reading font (Noto Sans, 12-18 pt) includes Greek and Greek Extended,
  so Greek books and notes read with no extra font (about 300 KB of flash, affordable since the
  partition change). SD-card fonts with Greek (Literata, Noto Serif, Gentium...) work as well.
- Hyphenation: monotonic Greek patterns (`hyph-el-monoton`) and polytonic patterns
  (`hyph-el-polyton`, built with typst/hypher's generator). A word with any polytonic letter uses
  the polytonic patterns; other Greek words use monotonic; this ignores the book's language tag.
  Text with combining accents (not precomposed) is not hyphenated.
- After a firmware update that changes the page cache format, the first open of each chapter shows
  "Updating for new version..." while it is rebuilt.
- Later, on request: Indic and Burmese text shaping and fonts.

## 3. Architecture

- `lib/Vault/`: all vault logic, no UI and no Arduino includes, so it builds and tests on the host.
  - `Markdown`: line and span parser that produces a block/run stream for the renderer.
  - `Frontmatter`: YAML subset (scalars, lists, `aliases`/`alias`, `tags`/`tag`).
  - `Index`: note table, links, tags, aliases, search terms, stored on SD.
  - `Canvas`: streaming `.canvas` (JSON Canvas 1.0) reader into a compact node/edge model.
  - JSON for Canvas and CouchDB responses. Two parsers are already in the build: ArduinoJson 7.4.2
    (`platformio.ini:171`, DOM with stream filters) and the SDK's SAX `StreamingJsonParser`
    (`freeink-sdk/libs/network/JsonSax`). The SAX parser drops any string longer than its 2 KB token
    buffer, while card text and chunk `data` can be far longer, so it needs a mode that streams long
    string values out in pieces (or a VaultInk wrapper that does). Decided in Phase 2.
  - `LiveSync`: document model, chunk assembly, E2EE, path de-obfuscation. HTTP/TLS is behind a
    small transport interface, implemented in `src/` on the device and faked in host tests.
- `src/activities/vault/`: thin screens (picker, browser, note reader, search, tags, backlinks,
  canvas, editor, sync). Built on `UiListActivity` / `UiTabListActivity` and the shared UI.
- Book code stays as it is. The Library and Home screens gain a "Vaults" entry.
- MicroMarkD's MIT parser (`lib/MicroMarkDMarkdown`, Daniil Pankov) may be reused as a base for
  `Markdown`, with credit in NOTICE. Its `esp32-git` dependency has no licence and is not used.
- Big buffers go in PSRAM. Every allocation has a fixed cap and a failure path (see
  `.agents/rules/hardware-resources.md`). Nothing loads a whole note, vault or canvas into RAM.

### 3.1 Home and menus (Phase 1b)

Approved design: `docs/design/` (home, quick settings, reading menu, settings; drawn by
`docs/design/mockups.py`).

- Home is one vertical list: a Continue card (cover, title, progress, today's reading time and
  streak), then Books and Vaults as the two big rows, then Search, Files, Transfer, Plugins,
  Settings. Each row has an icon, a title and a one-line status. It fits on one screen.
- Continue covers vaults too: the card shows whatever was open last, a book or a vault note.
  Each vault also remembers its last note, shown on its row in the vault picker ("Last: <note>";
  built in 0.9.0, stored in `/.vaultink/<vault-id>/last.txt`). Opening the vault lands on it.
- Buttons: up/down move one focus highlight (thick outline and a bar on the left; only the two
  changed rows are redrawn), OK opens, Back goes back. A hint line shows the keys.
- No large black areas (they ghost on 4-level e-ink).
- Quick settings: opened by tapping the status group (light, Wi-Fi, sync, battery) at the top, by
  swiping down, or by a button hold (which button is decided on the device). Tiles: Light, Wi-Fi,
  Sync now, Rotate, Sleep, Transfer. Brightness and warmth use - / + steps, not sliders.
- In a book, the swipe-down panel puts the book card on top (cover, title, page, progress,
  time left) above the quick-settings tiles, plus an Exit button (idea from KOReader SimpleUI).
- Reading menu: OK or a swipe up opens a bottom sheet with title, progress, Contents, Bookmark,
  Search, Highlights, a text-size stepper, and Font / Spacing / Margins. Buttons step through it.
- Settings are grouped by task: Reading (Reader, Library, Vaults), Device. Each row shows its current
  value. Vaults (0.9.0): Open Vault at Last Note, Rebuild Search Index.
- Page backgrounds (kos25k's idea, mostly for fun): plain, paper, sandpaper, papyrus soft/medium/
  hard, toilet paper, kitchen paper, bath towel. Drawn as dots under the text in the B/W frame,
  computed from the pixel position (no memory, same on every page). Settings → Reader → Page
  Background, and a quick-settings tile that names the current one. EPUB and notes; not XTC.
  Text anti-aliasing is skipped while a background is on (0.8.3): on UC8279 X4 Pro panels the AA
  pass faded dotted pages. The tile steps through the styles checked under Settings → Reader →
  Paper Button Cycle (0.20.0; `paperCycle` bit mask, 0 = all).
- Line spacing (0.20.0): levels 1-10, stored 0-9 under `lineSpacingLevel`, a multiplier on the
  font's line height (SD fonts 0.75-1.20, built-in 0.70-1.05). The old `lineSpacing` 0-3 loads as
  levels 5, 6, 8, 10. Hide Status Bar (`statusBarHidden`) empties the status bar spec, so the text
  gets its height. Hyphenation defaults on.
- Sleep screen battery (0.20.0, `sleepBattery`): the percent at sleep time, black on a white patch
  at the bottom right; in gray picture planes the patch is kept white.
- File browser (0.20.0): Move/Copy through a folder-only picker, New folder, folder rename. A
  book's cache, bookmarks and clippings move with a file; a folder moves as one rename. Copy
  streams through `name.part` and removes it on a short write.
- On-screen keyboard: English and Greek (from the FreeInk SDK, Greek layout added in Lef's fork
  until upstream takes it). Opens in the UI language's layout; the globe key switches. Greek vowels
  hold for tonos, ϊ/ϋ keys hold for ΐ/ΰ.

## 4. Vaults on the SD card

- A vault is any folder that contains `.obsidian/`, or a folder the user picks. Default root: `/Vaults/`.
- Several vaults; the last one opened is remembered.
- Paths are UTF-8 and can be Greek and long (exFAT/FAT long names). Must handle names up to the
  file-system limit (255 UTF-16 units, so up to ~765 UTF-8 bytes) and paths up to 1024 bytes. What the
  SdFat stack in this tree really supports is unverified; test it in Phase 0.
- `.obsidian/`, `.trash/` and dot folders are hidden in the browser and skipped by the index.
- Sizes to support: 300 notes, 60 attachments, 60 MB per vault (largest known test vault: 53 MB,
  241 notes). Design limit: 5,000 notes per vault.

### 4.1 Index

- Stored outside the vault in `/.vaultink/<vault-id>/`, so it is never synced or seen by Obsidian.
  The first line names the format version; a different version means a rebuild.
- Built (0.4.0): `index.tsv`, one text line per note: path, stamp (size and modification time),
  title, aliases, tags, outgoing links (0.7.0: `[[...]]`, `![[...]]` and `[..](x.md)` targets
  without alias or heading, up to 200 per note), folded words (up to 1,500 per note, lines under
  60 KB). Searched by streaming the file, so memory stays bounded.
- Backlinks (0.7.0): one pass over the index; a note links here when a link equals this note's
  name, or is a path (with `./` and `../`) that leads to it from the vault root or from the linking
  note's folder. Case and accents are ignored (search folding, Greek too); `.md` in any case.
  Links take at most half of an index line.
- File list (0.9.1): `files.txt` next to `index.tsv`, every file path in the vault (dot folders
  skipped, up to 20,000), rewritten with each index update. Links, images and note embeds with a
  bare name look it up there first (same order as the scan: fewest folders deep, then shortest
  path); the SD scan runs only when there is no list yet or the listed file is gone (renamed, or
  added since the list was written). Settings → Vaults → Rebuild Search Index deletes both files.
- Planned: heading and block-id anchors.
- Incremental: compare the stamp per file; re-read only changed or new files, copy the rest.
  Runs when Search opens, with a progress popup. Not yet: a background task that can be cancelled.
- Search: case and accent folding for Latin and Greek, monotonic and polytonic (`ά`/`ἄ`/`ᾄ`→`α`,
  final `ς`→`σ`, diaeresis dropped),
  prefix match on titles, aliases and body tokens; every word must match. Ranking: title > alias >
  tag > body, exact word above prefix; all vaults ranked together (top 50). No note bodies are read
  at query time.

## 5. Notes (Phase 1)

### 5.1 Markdown support

| Feature | Phase 1 | Notes |
| --- | --- | --- |
| Headings `#`..`######` | must | anchors for `[[note#heading]]` |
| Bold, italic, bold-italic, strike, inline code | must | per span, not per line |
| Highlight `==text==` | should | done (0.13.0): a light gray band behind the text (`<mark>`), drawn in the black-and-white pass only, so the letters stay black. 0.14.0: Settings → Vaults → Highlight style picks Light gray (default), Inverted (black band, white letters) or Dark gray (black band in the black-and-white pass, lightened to dark gray by the two gray passes, letters cut back out; plain black without text anti-aliasing) |
| Paragraphs, line breaks | must | reflow, hyphenation per language; Greek with 1/1 minima (one-letter syllables may split off, as in babel-greek) |
| Bullet, numbered, nested lists | must | done; nested levels indented, numbers keep their start (0.8.0); inside quotes as lines with the marker |
| Task lists `- [ ]` / `- [x]` | must | read only in Phase 1, toggle in Phase 4 |
| Block quotes | must | done, indented |
| Callouts `> [!type] title` | must | done (0.11.0): a framed box (1 px border, padding) with the title in bold, or the type ("Note") when there is no title; no per-type icon yet |
| Foldable callouts `+`/`-` | should | Phase 1 shows them open; tap-to-fold needs re-pagination |
| Bare URLs and `<https://…>` | must | shown as links |
| Block ids `^id` on paragraphs, list items, tables | must | hidden, used as anchors |
| Escapes `\[[`, `\*` | must | |
| Fenced and indented code | must | monospace, no syntax colours |
| Tables | must | column widths fitted to screen, wide tables scroll sideways |
| Horizontal rule | must | |
| Wikilinks `[[note]]`, `[[note\|alias]]`, `[[path/note]]`, `[[note#heading]]`, `[[note#^block]]` | must | resolve like Obsidian: shortest unique path, then aliases |
| Markdown links `[text](url)` and `[text](note.md)` | must | external URLs show the address, are not opened |
| Embeds `![[image.png]]`, `![[image.png\|300]]` | must | PNG, JPEG, GIF (first frame), scaled and dithered |
| WebP embeds | should | no WebP decoder in the tree yet (`ImageProbe.h:29`); needs a new library |
| Note embeds `![[note]]`, `![[note#heading]]`, `![[note#^block]]` | must | done (0.8.0) for a whole note on its own line: one level (an embedded note's embeds stay links, so no loops), up to 32 KB; (0.10.0) `#heading` embeds that heading up to the next one of the same or a higher level, `#^id` the list item or paragraph ending in ` ^id`; an anchor that isn't found embeds the whole note |
| Frontmatter | must | hidden from the text; `aliases`, `tags` used by the index; a small "Properties" view |
| Tags `#tag`, `#nested/tag` | must | |
| Footnotes | should | done (0.8.0): `[^id]` is a raised id linking to the `[^id]: text` paragraph |
| Comments `%%...%%` | must | hidden |
| HTML | later | tags stripped, text kept |
| Math, Mermaid | later | shown as code |

### 5.2 Note screens

- Vault picker, folder browser (folders first, sorted by name with Greek collation), recent notes.
- Note reader: paged, same fonts and settings as the book reader. Saves position per note.
- Link following: tap a link (touch) or select it with buttons. A back stack of at least 16 notes.
- Unresolved links are drawn differently and say "note not found".
- Search (done), backlinks for the current note (done: More → Backlinks), tag list, outgoing links.
- Attachments other than images (PDF, docx, audio) are listed but not opened until their phase.

## 6. Canvas (Phase 2, read only)

- Format: [JSON Canvas 1.0](https://jsoncanvas.org/). Streamed from SD, never parsed as one DOM.
- Nodes: `text` (Markdown, rendered with the note renderer), `file` (note or image), `link` (URL shown
  as text), `group` (box with label). Colours (preset `"1"`-`"6"` or `#hex`) map to grey levels or
  patterns.
- Edges: lines with arrow heads (`fromEnd`/`toEnd`), side anchors (`fromSide`/`toSide`), one `label`.
- View: fit-to-screen on open, pan, zoom in/out in steps, tap a card to open its note or image.
  Pan and zoom draw a low-detail frame (boxes and lines) with partial refresh, then the full frame.
- Card text stays in the file on SD (offset + length, JSON-unescaped while reading); only geometry
  and ids are kept in memory, in fixed-size records in PSRAM with a budget set in Phase 2.
- Limits: 2,000 nodes and 4,000 edges per canvas. A spatial grid on SD or PSRAM decides which cards
  are on screen.
- Built (0.16.0): `lib/Vault/Canvas` streams the file with its own small tokenizer (the SDK's JSON
  SAX drops strings over 2 KB and gives no file offsets). Per node: id, type, geometry, colour, a
  label (group label, file path, URL or the first 160 bytes of a text card, cut at a UTF-8
  boundary) and the raw text's offset/length; edges keep node indexes, sides, ends, colour and
  label, and edges to unknown nodes are dropped. The viewer (`CanvasActivity`) draws groups,
  then edges (clipped to the screen, arrow heads, label on a white patch), then cards with
  wrapped text clipped to the card. Every node is checked against the screen on each frame; the
  spatial grid isn't needed yet at the sizes seen (106 nodes). A text card is written to
  `/.vaultink/canvas-card.md` (its reader cache dropped first) and opened as a note.
- 0.18.0: a coloured node (any of `"1"`-`"6"` or `#hex`) is filled with the light-gray dither,
  coloured cards also keep the 2 px frame; a file node whose path is a PNG or JPEG is resolved
  once (like a link) and decoded into the card's inner box, fitted, black-and-white with
  dithering, only when the fitted picture lies fully on screen (decoders don't clip), otherwise
  the file name; only the needed decoder is built.
- Zoom controls (0.18.0): 52 px buttons stacked at the bottom right (16 px from the right edge,
  72 px above the bottom), +, the level label, −, Fit; steps of 2 from fit up to 8x, around the
  screen centre; the panel swallows taps; a HALF refresh on Fit and every 5th step, FAST
  otherwise.

## 7. LiveSync pull (Phase 3)

Target: Self-hosted LiveSync over CouchDB 3.x only. Object storage and P2P are not supported.
The format is "not a stable API" (`docs/datastructure.md` in obsidian-livesync); VaultInk pins the
generations it has been tested against and refuses others with a clear message.

- Setup: import a Setup URI (`obsidian://setuplivesync?settings=...`) from a file on SD, the web UI
  or QR code(s), unlock with its passphrase. How the plugin splits settings over QR codes is unverified.
- TLS: public CA certificates by default; the user can add their own CA certificate for a self-signed
  server (should). The user chooses to keep the decrypted settings on the
  device (encrypted with a device key) or to forget them after the sync.
- Wire: HTTPS, `GET _changes?since=…` with a checkpoint saved per vault, `_bulk_get` or
  `_all_docs?keys=…&include_docs=true` for metadata and chunks.
- Documents: metadata docs (`type` `plain` / `newnote`, `children` chunk ids; legacy `notes` with
  inline `data`), chunks `h:`/`h:+`, obfuscated ids `f:`. Skip control docs and the `i:`, `ix:`, `ps:`
  namespaces. Reconstruct a file by streaming its chunks in order to a temp file on SD, then rename;
  binary chunks are Base64.
- Encryption, pinned from octagonal-wheels `hkdf.ts` and livesync-commonlib (2026-10-27) and checked
  against the real library's output (`test/livesync/`, vectors from `.work/livesync-src/vectors/gen.mjs`):
  - master = PBKDF2-SHA256(passphrase, `pbkdf2salt` of `_local/obsidian_livesync_sync_parameters`
    (base64, 32 bytes), 310,000 iterations, 32 bytes); per message HKDF-SHA256(master, salt[32], info "")
    -> AES-256-GCM key, 12-byte IV, 16-byte tag after the ciphertext, no AAD.
  - Chunk `data` and other V2 strings: `%=` + base64(iv | hkdfSalt | ciphertext | tag). Refused:
    `%~` (V3 legacy), other `%` (V1), `[`.
  - Obfuscated metadata (`f:` ids): `path` is `/\:` + a `%=` string holding the JSON
    `{path, mtime, ctime, size, children}`; the doc's own mtime/ctime/size are 0 and `children` is
    empty, so the chunk list is only known after decrypting.
  - Setup URI: `obsidian://setuplivesync?settings=` + URL-encoded `%$` + base64(pbkdf2Salt[32] | iv |
    hkdfSalt | ciphertext | tag) (trailing space trimmed). The passphrase is either as typed or the
    time-bound form hex(HMAC-SHA256(SHA256(pass), `["livesync/setup-uri","tb1","ephemeral",<week>]`)),
    week = floor(unix_ms / 604800000).
  - Version doc `obsydian_livesync_version` (sic): 12 now, 13 with newer features; refuse > 13.
  - Built (`lib/LiveSync/LiveSyncCrypto`, mbedtls HMAC/HKDF/GCM; PBKDF2 and base64 written out so the 310,000 rounds can yield and the simulator matches the device): all of the above for reading. Still unverified:
    PouchDB's exact replication requests, compression order with encryption (VaultInk needs
    compression off), how `couchDB_URI` and `couchDB_DBNAME` join.
- Scope: choose which vault (database) to sync. Text first; attachments on demand or by size limit.
- Deletes and conflicts: a remote delete moves the local file to `/.vaultink/<vault-id>/trash/`.
  Remote conflicts (`_conflicts`) are resolved by taking CouchDB's winning revision, like the plugin
  shows by default. Local edits are kept and shown as "local changes".
- Test setup: local CouchDB in Docker (`--cpus 4`), filled by the real plugin with a throw-away vault.
  Real vaults are never used for automated tests and never sent to external tools.

Phase 3 must at least support: E2EE V2, path obfuscation, compression off, `customChunkSize`
60, `hashAlg` xxhash64, `chunkSplitterVersion` v3-rabin-karp, `useEden` off, dynamic
iterations off.

TLS and triggers (0.21.0):
- Server certificates are verified by default: a 7-root PEM bundle in flash (`src/util/CaBundle.h`,
  made by `scripts/gen_ca_bundle.sh` from the Mozilla store), or `/.vaultink/livesync/ca.pem`
  instead of it (unreadable = the request fails, never a fallback). The SDK's wolfSSL client calls
  `wolfSSL_check_domain_name` before the handshake and fails when the CA buffer doesn't load; SNI
  is left out for IP literals. `checkCert` in the profile (default true, also for older profiles)
  turns it off (`WOLFSSL_VERIFY_NONE`). An untrusted clock gets an SNTP sync first.
- Quick-settings Sync tile (only with a profile): LiveSync screen in auto mode, restart to the
  reader (or home) after a successful sync; a failure stays on screen.
- Sync on wake (`autoHours` 0/6/24, `lastAttempt`): checked in `setup()` before the normal
  routing, needs a trusted clock, records the try first, joins the last saved network headless
  (10 s, battery at least 20%), then the same auto mode, which returns even on failure.

## 8. Editing (Phase 4)

- Editor: line-based, touch keyboard (English + Greek layouts), cursor keys, select line, undo.
- Toggle task checkboxes from the reader.
- New note, rename, delete (to `/.vaultink/<vault-id>/trash/`). Links are not rewritten on rename in Phase 4.
- Save is atomic: write `name.md.tmp`, sync, rename; recover an orphaned `.tmp` on boot.
- `update-time-on-edit` style: if frontmatter has an `updated`/`modified` key, set it on save (should).
- LiveSync push: split with the same chunker as the plugin (v3 rabin-karp, xxhash64), encrypt,
  `_bulk_docs`, with `_rev` handling. If a pull finds that a note with local changes also changed
  remotely, keep both: the remote version is saved as `note (conflict YYYY-MM-DD).md` and the user is
  told. Conflicts only between remote revisions still follow §7 (winning revision).

## 9. Bluetooth keyboard (Phase 5)

- BLE HID host (NimBLE, Apache-2.0). Pair, reconnect, US and Greek layouts, toggle with a key combo.
- Works in the editor, search and any text field. `lib/BlePageTurner` is checked for reuse first.
- Wi-Fi and BLE share one radio. Whether a keyboard stays usable during a sync is tested in Phase 5;
  if not, sync pauses keyboard input and says so.

## 10. Android companion (Phase 6)

- Separate repo Somnius/VaultInk-Companion, Kotlin, app ID `com.lefteros.vaultink`, Android only.
- BLE link to the device: type into the device, send a quick note, change settings, show sync status.
- Signed APKs on GitHub releases. The app checks the latest release, downloads it and starts the
  install. Whether Android allows a silent install for this case is unverified; the fallback is the
  normal install prompt.
- The signing keystore is kept offline by the maintainer, never in the repo.

## 11. PDF trial (Phase 7)

- Spike: render one page of a small PDF on the ESP32-S3 with PSRAM, measure time and memory.
- Go/no-go decision written to the roadmap. If go: page images, fit width, pan, page cache on SD.

## 11a. Watch-only wallet (Phase 9)

Watch-only means the device holds **no secrets**: no private keys, seeds or recovery phrases, ever.
It can show receive addresses and balances, but it can't spend. Storing keys was ruled out: the SD
card is unencrypted, flash encryption and secure boot are off (turning them on is a one-time eFuse
change), the ESP32-S3 has no secure element, and Espressif advisory AR2026-005 (2026-06-10)
reports AES key recovery on the S3 with voltage fault injection.

- Bitcoin first. Input: an extended public key or output descriptor (BIP84 `zpub`/`wpkh(...)`
  first), or a plain list of addresses. Entered through the web UI or a file on the SD card.
  The firmware refuses anything that looks like a private key or seed (`xprv`, WIF, 12/24 words).
- Receive addresses are derived on the device with public BIP32 derivation only. mbedTLS in the
  pinned Arduino core has secp256k1 enabled (`CONFIG_MBEDTLS_ECP_DP_SECP256K1_ENABLED=y`); bech32
  encoding is small. Tested against the BIP84 test vectors on the host.
- Show the next unused address as a QR code (the `ricmoo/QRCode` library is already a dependency)
  and as text. Mark addresses as used after a balance check.
- Balances are opt-in, off by default, and only on request. They use the mempool.space REST API
  format (`GET /api/address/:address`, confirmed balance = `chain_stats.funded_txo_sum -
  chain_stats.spent_txo_sum`), against a server the user picks. A self-hosted mempool or another
  Esplora-compatible server is recommended. A public server learns which addresses belong together,
  so the screen says so. Public servers rate-limit (HTTP 429): query a small gap of addresses
  (default 20) slowly, and cache results on the SD card.
- Wallet data (xpub, descriptor, labels, cached balances) is stored on the SD card **encrypted**
  as one file, `wallet.vik`: AES-256-GCM with a random 96-bit nonce per save (mbedTLS GCM, in the
  pinned core). It is decrypted into RAM only while the wallet screen is open, re-encrypted on
  save, and the RAM copy is wiped on close, sleep and timeout. No plaintext is ever written to SD.
- The file key comes from two parts, so a copied SD card alone is useless:
  - **PIN or passphrase** typed on the device, stretched with PBKDF2-HMAC-SHA256 (mbedTLS
    `PKCS5_C`, in the core) using a per-file random salt; iterations set from a device timing
    (about 1 s).
  - **Phone share**: a random 256-bit key held by the paired companion app (Android keystore,
    unlocked with the phone's fingerprint or screen lock), sent over the paired BLE link when the
    phone is near. File key = HKDF(PIN key || phone share).
  - Modes the user picks: PIN + phone (default, strongest), passphrase only (long passphrase
    required), phone only (convenient; anyone with the unlocked phone and device can open it).
  - A button combo is not offered as the only secret: it has too few possibilities, and the file
    can be copied and guessed offline.
  - Recovery: the xpub can always be re-imported (it isn't a secret that controls funds), so a lost
    phone or forgotten PIN means re-adding the wallet, never losing money.
- Companion app: scans xpub/descriptor/address QR codes with the phone camera and sends them to
  the paired device; the device has no camera.
- Later, if asked: other chains with plain addresses (e.g. Ethereum, balance from a user RPC).
- Never: signing, storing seeds, or sending.

## 12. Updates, settings and privacy

- OTA update check points at Somnius/VaultInk GitHub releases. No other network calls unless the
  user starts a sync.
- No telemetry, no analytics, no accounts.
- Secrets (CouchDB credentials, passphrases, Setup URIs, APK keystore) are never in the repo, logs
  or crash dumps.

## 13. Testing

- Host unit tests for every `lib/Vault` module (GTest, existing `test/` setup), with small synthetic
  vaults in `test/` written for VaultInk. No real vault content in the repo.
- Simulator runs for every screen in X4 Pro mode, English and Greek UI.
- LiveSync tests against the local Docker CouchDB, plus recorded test vectors for decryption.
- Builds: shipped firmware is built on Lef's machine (CPU limits: nice, capped jobs) and uploaded by
  `scripts/deploy_web_installer.sh` (web installer plus GitHub release). GitHub CI only runs checks,
  on release tags and manual runs.
- Hardware tests are done by Lef on the device before a release.

## 14. Licences and credits

- VaultInk: MIT, keeping CrossPoint's copyright notice.
- Third-party: CrossPoint Reader (MIT), FreeInk SDK, MicroMarkD parser (MIT, Daniil Pankov) if used,
  fonts (OFL), NimBLE (Apache-2.0). All listed in NOTICE.
- The LiveSync wire format is implemented from the public source; no LiveSync code is copied unless
  its licence allows it and it is credited.
