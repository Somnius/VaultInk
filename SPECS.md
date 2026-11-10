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
- XTC chapters (0.33.2): the format description makes start/end 0-based; CrossPoint assumed
  1-based. `XtcParser` reads all records first: if any chapter starts at 0 the table is 0-based and
  kept, else every page is shifted down by one. An all-zero record ends the table except the first
  (an unnamed one-page first chapter in a 0-based table).
- Comics (Phase 1c, planned; guide docs/comics.md): conversion off the device with Banana Comic
  Converter (XTC/XTCH/EPUB/CBZ). 1c.1 companion "Convert a comic" link + share target by display
  name to /Comics; 1c.2 XTCH row/byte blit (transposing, host-tested) and grayscale refresh choice;
  1c.3 lib/Cbz + CbzReaderActivity streaming pages from the zip into the existing decoder, .pxc
  pool of 2-3, fit-to-screen, natural sort, sanitised names, no ZIP64/CBR/WebP; 1c.4 per-book RTL
  flag swapping next/previous, ComicInfo.xml, covers.
- Settings search (0.33.0): `SettingsSearchActivity` builds every section's rows with
  `SettingsActivity::buildList` (one shared `allSettings()` per search) plus the Text Settings rows,
  matches `textfold::fold(name)` against the folded query, opens `SettingsActivity(section, nameId)`.
  The notes row starts `VaultSearchActivity` with `initialQuery`.
- Home row order (0.32.0): `homeRowOrder`, a uint32 of 4-bit slots holding row id + 1 (0 ends;
  0 overall = default). Row ids are `VaultInkHomeUi::Row`, append only. Decode drops unknown ids
  and repeats and appends missing rows in default order.
- I18n blobs (0.32.0): every string starts on an even byte and offsets are stored halved, so the
  15 bits under the English-fallback flag reach 64 KB per language.
- Favourites (0.31.0): `/.crosspoint/favourites.json`, up to 50 `{path, title, author}` newest
  first, held in RAM; written through `.tmp`, an unreadable file kept as `.bad`. The Library's
  fourth tab lists it directly (not the library index). Toggles: reader menu (books only) and the
  Recent long-press popup. Rename/move (file browser, reader, web server) update the path; a
  delete removes the entry.
- Books only (0.35.0): `libraryBooksOnly` in settings.json. Drops `.md`/`.canvas` rows from the
  Library index lists and from the Recent pins (`pinnedStore` maps shown pins to store rows).
  Book info (`util/BookInfo`): title/author from the index, else recents, favourites, file name.
- Authors as folders (0.36.0): `libraryAuthorFolders`. The Author tab's group view (groupStarts by
  author) is re-applied after every `applyFilter()`; a one-book group is drawn and opened as the book;
  `folderGroup` limits `rowFor()` to that group's run. Spellings are merged at index build (author key).
- Reading log (0.51.0): lib/ReadingLog (host-tested) + src/util/ReadingStats. ReaderActivity::flushReaderSession (on exit and before sleep, 0.52.0)
  appends `start\tseconds\tprogressBp\tpath` to /.vaultink/reading-log.tsv for books (not .md/.txt) when the
  session is dated (trustedtime) and >= 10 s. Sleep mode READING_STATS = 9 streams it twice (totals and
  recent books; then the calendar book's last progress per local day, carried forward).
- Lock screen (0.49.0): `lockMode` 0 off / 1 PIN / 2 corner; PIN as an FNV-1a digest in
  /.vaultink/lock-pin.txt (no file = PIN lock off). enterDeepSleep stores esp_rtc_get_time_us() in
  `lockSleptAtUs`; setup() shows LockActivity first when asleep >= 30 min, the clock went back (cold
  start) or `lockPending` (the lock was up at sleep); unlocking runs the normal wake routing
  (routeWake). Not on silent reboots, recovery or the crash report. (0.52.0) `lockAfter` indexes
  LOCK_AFTER_MINUTES {0,5,10,15,30,60,120,180}; `lockCorner` 0 any/1 TL/2 TR/3 BL/4 BR; `lockHoldSeconds`
  0-10 timed with isScreenTouchHeld. LockSettingsActivity is the Lock Screen page (type
  popup, the three above cycled on tap, `ownerName[32]` / `ownerPhone[24]` by keyboard); LockActivity
  draws the owner at the top in Unlock mode; `sleepOwner` stamps "name  phone" bottom left (stampSleepBattery,
  the stats screen, the default screen). Those settings keys are category-less, so Settings → System no
  longer lists them. Every built-in font carries U+2190-2193 (Noto Sans Symbols) and
  U+2610-2612 (Noto Sans Symbols 2).
- Library format filter, 0.58.0 (kos25k #18): lib/LibraryIndex/LibraryFormats.h (bits FMT_EPUB..FMT_CBZ, 63 = all; formatBitOf/formatAllowed, platform-free); settings libraryFormatMask (saved by hand, 0 reads as all) and libraryFormatButton (toggle in Settings > Library); src/activities/settings/LibraryFormatsActivity (a UiListActivity of toggles, the last switch stays on); LibraryListActivity draws the gear as a third header button by hand (the FreeInkUI header has two slots), touch boards only; filtering() is true when the mask is not all, applyFilter and the pinned rows skip other formats. .pdf is in isLibraryBookFile and CLIX_FORMAT_VERSION is 3 so old indexes rebuild once; a PDF's converted text lives under /.vaultink (hidden, not listed).
- Search in a book, 0.58.0 (on top of 0.57.0 below): EpubSearchActivity hands the hit list to the reader through a SearchSession (src/activities/reader/SearchSession.h: query, hits, current, origin spine/page/offset). EpubReaderActivity search mode: setSearchActive freezes ReaderActivity::sessionFrozen (no reading-log time or turns), the render's progress save and launchKOReaderSync are skipped; drawSearchHighlights groups touching words by source range (so an <i> inside a word still matches) and shades them like a clipping; drawSearchBar replaces the status bar (stay-here counter, previous, next, white-on-black return arrow); Back or the arrow = searchReturn (applyProgressChange to the origin), the counter = searchStay. Settings searchInMoreMenu and searchScope (whole book / start to the reading point: limitSpine and limitOffset in EpubSearchActivity).
- Search word, 0.59.0 (kos25k #24): SearchWordSettingsActivity (typeOnly = the gear's short version) holds searchInMoreMenu, searchScope and the new searchType (0 partial, 1 exact; these three are hidden from the Reader section list and reached through the Search word action). ChapterTextSearcher takes exactWords: a candidate match waits for the next character (awaiting_, resolveAwaiting) and is dropped when a letter or digit touches it or when the character before it is a word character; wordsearch::find(…, exactWords) does the same on the page for the highlights, with wordsearch::isWordChar. Snippets are 22 bytes before and 110 after the match so the row fills; the search bar's chevrons are drawn at SearchBar::prevCx/nextCx, 1.6 buttons apart.
- Library percent sort, 0.60.0 (kos25k #15): LibraryListActivity::percentSort (0 arrival, 1 most read first, 2 least read first; Recent tab only) is a third narrowing of applyFilter: filtering() is true while it is active, the matched rows are stably sorted by percentForRow (loadBookProgressQuick: one file check for a never-opened reflowable book, else loadBookProgress), cached per ordinal in percentCache (dropped when a book is opened). The percent is appended to the author line of flat rows. The pinned recents are off while it is active. UiAppHost's FreeInkApp is <32, 10>.
- Random page, 0.60.0: Row::RandomPage (id 9, appended before Settings in DEFAULT_ORDER; homerows::hidden treats its bit in homeRowsHidden as "shown", so it is off by default). HomeActivity::openRandomPage picks an EPUB from the library index, sets randompage::request() and opens the reader; EpubReaderActivity::beginRandomPeek (in loadBook) records the saved place as the search session origin, sets search.active and jumps to a random byte of the book through locateBookSize. With 10 rows VaultInkHomeUi::drawRows is compact (gap 3, first two rows as tall as the rest).
- Paper contrast, 0.60.0 (kos25k #1): SETTINGS.paperContrast (0..100, default 100, saved by hand like paperCycle); paper_texture::draw thins the dots with a per-pixel hash. PaperContrastActivity is the percent-dialog layout with tiles from paper_texture::drawIn; PaperCycleActivity row 0 opens it.
- Search speed, 0.60.0 (kos25k #24): EpubSearchActivity::spineAt starts at the reader's chapter and wraps (book order when the range is "to here"); SearchSession::lastKey/lastFound hold the last finished search (key: prepared query, type, range) and startSearch reuses it. Opt-in index (SETTINGS.searchIndex): <book cache>/search.idx (SearchIndex: header + one 8 KB Bloom filter of folded trigrams per chapter, stamp = chapter size + 1), filled by ChapterTextSearcher::collectTrigrams while a chapter is read to its end (never from a chapter cut short by the hit cap) and consulted by skippedByIndex (words under three letters cannot be judged, so nothing is skipped). lib/Vault/TrigramFilter has host tests.
- Tools, 0.63.0: lib/Focus (Pomodoro state machine on epoch seconds, serialize for sleep/reboot, TSV log line), lib/Habits (days since 2020-01-01, up to 16 habits x 400 days, streaks, text file `name|d,d,..`), lib/TextInput (a 64-event ring shared by the Bluetooth keyboard source, USB HID to UTF-8 translation for US and Greek with a tonos dead key, apply() on a UTF-8 line; KeyboardEntryActivity::drainTypedInput is the only consumer and clears the ring on Enter/Escape), lib/Vault/Excalidraw (streaming parser into shape/point/text pools, caps 1500/12000/24 KB, deleted/image/frame skipped, `files` skipped, `.excalidraw.md` json fence read, compressed-json refused), lib/Dice/DiceEntropy.h and src/util/Entropy.cpp (MurmurHash3 mix of the hardware RNG, yyyymmddhhmmss, epoch, micros, millis, battery percent, battery millivolts, a counter). src/activities/games/FocusActivity and HabitsActivity are hand-drawn like Dice; src/activities/vault/ExcalidrawActivity reuses the canvas viewer's controls; PluginCatalogActivity BUILTIN_ROWS = 5. Bluetooth keyboard: bleturner::Where::TextEntry, Config::keyboardTyping ("bleKeyboardTyping"), a RadioPolicy arm that allows the radio on a text screen only with the option on, a bond and the S3 heap, and Runtime::serveTextEntry routing presses to textinput::global(); KeyboardEntryActivity calls setTextEntry/setKeyboardLayout. Companion 0.5.0: GET /api/livesync/status, KeepImport.kt (Keep JSON to Markdown into Imported/Keep/).
- VaultEUC, 0.62.0: lib/Euc (platform-free): Euc.h contracts (Brand, GattProfile, Telemetry with a valid-field mask, the safe Command set, Protocol, WheelSimulator, Link); one decoder + command encoder + wheel simulator per brand (Begode, Veteran, Kingsong, InmotionV1/V2, NinebotS/E/Z via NinebotCommon, Vesc, CrcUtil, BatteryCurve); Alarms (watch-side thresholds, PWM first, hysteresis, repeats by time), RideStats (with a Tritbool-style battery SoH estimate), RideLog (CSV columns of the DarknessBot/WheelLog family and a streaming parser), Catalog (maker/model table, detectBrand by name and service), Presets (up to 16 wheels, key=value file), Session (command gate: allowedWhileMoving or speed <= 2 km/h, unknown speed = moving; refresh clamp 5..30 s). src/euc: Transport (Link plus scan/connect/read/tick), NimBleTransport (NimBLE client on its own connect task, length-prefixed notification ring so each notification reaches Protocol::feed whole; MTU 23; the stack is brought up and fully down by the activity) and FakeTransport (a looping ride script through the brand's WheelSimulator, always built). src/activities/euc/VaultEucActivity: hand-drawn list screens (Warning, Wheels, Scan, Maker, Model, Dash, More, Rides, Settings, Alarms), files in /VaultEUC (wheels.txt, settings.txt, rides.idx and the ride CSVs), Wi-Fi off while open, auto-reconnect with backoff and a fresh Protocol per attempt, redraw every 5/10/15/30 s (half refresh every 8th draw), an alarm redraws at once at most once a second. PluginCatalogActivity BUILTIN_ROWS = 3.
- Search screen, 0.62.0 (kos25k #24): KeyboardEntryActivity::setGearAction draws a gear in the header and runs an action on a tap; EpubSearchActivity passes the reading point always and reads the range from SETTINGS.searchScope at each search (limit(), limitOff()); the gear screen (SearchWordSettingsActivity typeOnly) has the two types and the range; the result list has sidePadding 6.
- Dice, 0.61.0: lib/Dice (platform-free: Rng/Xorshift32, one roll function per system returning a Result of up to 64 dice with flags and an Outcome; oWoD and V5 follow ShadowRealms wod_dice.py/v5_dice.py) and src/activities/games/DiceActivity (hand-drawn rows from a table of systems and parameters, touch -/+ and buttons; settings kept in /.vaultink/dice.sav; the V5 Rouse check writes the new Hunger back). It is PluginCatalogActivity row 1 (BUILTIN_ROWS = 2). SD plugins cannot run code, so it is firmware like Sudoku.
- Library tab order, 0.61.0 (kos25k #18): SETTINGS.libraryTabOrder (util/LibraryTabOrder.h, tab ids by position, 0 = default); LibraryListActivity keeps tab ids everywhere and translates to positions only for the strip (tabOrder, activeTab(), tabLabel, tabIndicator, onTabAction, stepTab).
- Home set-up, 0.60.0: HomeActivity::onEnter only shows the once-per-update companion QR; the rest (VaultInkHomeUi, recents, library counts) is HomeActivity::setupHome, run at once or from the QR screen's result. Before, onEnter returned after starting the QR and the home came back with vaultInkUi null (the standard home).
- Library start tab, 0.60.0 (kos25k #18): SETTINGS.libraryStartTab (0 Recent … 4 Folders, Settings > Library) is used by LibraryListActivity::onEnter when no tab was asked for. Search screen: the gear is drawn at the right edge; each result label is cut to the row width in buildScreen (EpubSearchActivity::fitted) and makeSnippet starts with ".. " and has no trailing dots.
- Info, 0.59.0 (kos25k #6): FileBrowserActivity shows Info as the last option for every entry (7th on a book or plain file, 8th on a folder or .zip); bookinfo::fileText and modifiedText (HalFile::modificationTime, packed FAT date and time).
- Sudoku, 0.59.0: lib/Sudoku (platform-free engine: bitmask solver with solution counting, seeded generator that keeps the puzzle unique, conflicts, hint, notes, 64-step undo ring, 174-byte save with CRC) and src/activities/games/SudokuActivity (drawn by hand, touch and buttons, saved to /.vaultink/sudoku.sav). It is the first row of the Plugins picker (PluginCatalogActivity::BUILTIN_ROWS).
- Battery history, 0.58.0 (kos25k #20): lib/BatteryLog (Record, toLine/fromLine, append capped at 128, compute: sleep-to-wake and wake-to-sleep pairs summed, USB at either end, a rising percent, a gap over 14 days or an undated end skipped, shown after a 1 % drop or 6 h); src/util/BatteryHistory writes /.vaultink/battery-log.tsv from main.cpp (noteSleep before the display sleeps, noteWake after the clock is read, only when the last line was a sleep); BatteryActivity rows asleep, awake, and "Drain by app: Not measured".
- Battery log, 0.73.1 (kos25k, follows #31): each line can end with millivolts and a wake cause (`<epoch> S|W <percent> <usb> <light> <mV> <cause>`; cause 0 power key held, 1 after flash, 2 USB power, 3 other, 4 power key not held, `-` on S lines); the parser takes 4, 5 or 7 fields, and older lines still read. A wake that is not held through the check goes back to sleep after writing an S line, so a ghost wake shows as W (4) then S. The frontlight is switched off before every deep sleep on boards that have one (was Picco only), also on that ghost-wake path. Whether the light leaked in sleep is not measured.
- Search in a book, 0.57.0 (replaces the layout-based search below): lib/Vault/BookTextSearch ChapterTextSearcher streams a chapter's XHTML (cached html/N.html or the zip) through expat, counts visible text offsets like ChapterHtmlSlimParser, folds with textfold, and returns (spine, visibleTextOffset) hits with a snippet; EpubSearchActivity runs it in ~50 ms steps per loop(), checks Back every step, redraws at most every 1.5 s; a pick jumps by offset through ProgressChangeResult. Old description (0.55.0): lib/Vault/WordSearch (prepareQuery folds with textfold and collapses spaces; find = word indexes of a query in a page's folded, space-joined words; snippet) and src/activities/reader/EpubSearchActivity (UiListActivity: keyboard, then one chapter per loop pass: Section::loadSectionFile, else createSectionFile under a FrameBufferLoan, then every page's PageLine words; at most 100 hits, 3 per page; Back stops; a hit finishes with a ProgressChangeResult carrying spine, page and the page's visible text offset). Menu action SEARCH_BOOK, books only; the reader frees its section and the radio's heap (makeRoomForChapterBuild) first. The top-edge light panel is not opened while the activity named Lock is up (ActivityManager).
- Highlights export: lib/Vault/HighlightsNote (build/isExport/fileNameFor; marker `%% VaultInk highlights %%` on the line after the title block) and src/util/ClippingExport (clippings sorted by chapter then offset, vault = newest `last.txt`, `<vault>/Highlights/<name>.md` through a .tmp, vaultedit::markChanged). Menu action EXPORT_CLIPPINGS for books with clippings.
- PDF as text (Phase 7): lib/Pdf/PdfText (random-access source callback, inflate callback; xref tables
  and streams with /Prev and hybrid /XRefStm, object streams, Flate with PNG predictors, ASCII85/Hex; page tree with
  inherited resources; content interpreter for Tf Td TD Tm T* TL Tc Tw Tz ' " Tj TJ q Q cm Do (forms); fonts: ToUnicode
  CMaps (bfchar/bfrange), simple fonts by WinAnsi + Differences, widths from /Widths and /W for word spaces; widget /V
  values appended; a page with a clear gutter is read left column then right, `twoColumnOrder`). src/util/PdfConvert writes `/.vaultink/pdf/<hash(path)>/<name>.txt` plus `stamp.txt` (the PDF's
  size and date); ActivityManager::goToReader converts then opens the .txt. Standard security handler V1-V4 / R2-R4 with the empty user password (lib/Pdf/PdfCrypt: MD5, RC4, AES-128-CBC; strings and streams decrypted per object, not xref streams or object-stream members). Not read: scans, password-locked and AES-256 files, LZW/DCT
  as content, Type3 fonts and CID fonts without a ToUnicode map.
- Flashcards (Phase 10): lib/Vault/Flashcards (parser: `::`, `:::`, `?`/`??` blocks, `{{}}` and
  `sr-cloze: bold` clozes; ids = FNV-1a 64 of the note path and the folded text), CardScheduler (SM-2,
  integer days, fuzz from the id), CardStore (`cards.tsv`: `vaultink-cards 1`, then `id due interval ease
  reps lapses last` per answer, newest wins, compacted past 1 MB), CardIndex (`cards.idx`: `N path stamp decks`
  then `C id line front back`, tab/newline/backslash escaped; a note keeps its block while its index stamp is
  unchanged). Entry: first row of the vault picker; FlashcardsActivity (decks), CardReviewActivity (review).
  Settings cardsNewPerSession / cardsMaxReviews / cardsMaxIntervalMonths (Vaults group).
- Gallery filter (0.45.0): `galleryFilter` 0 all / 1 .png / 2 under a vault root (vaultfiles::findVaults);
  gear (settings2 icon) at the header's right opens it. (0.48.0) 3 = chosen folders, listed in
  /.vaultink/gallery-folders.txt (only when the gallery root is "/"); Add a folder (PickFolder) and
  Clear in the same menu; icon_refresh_cw_32 left of the gear rescans.
- Home rows (0.44.0): Row::Favourites = id 8, homeRowOrder is uint64 (9 x 4 bits), `homeRowsHidden` bitmask
  (Books 0 and Settings 7 never hidden). Battery page: HAL percent + USB; on device BatteryMonitor
  millivolts/charging and CW2017 reg 0x06 (-40 + v/2 °C), shown only within -20..70 °C.
- CBZ right to left (0.43.0): `cbz::mangaRightToLeft(ComicInfo.xml)`; per book in <cache>/rtl.bin (wins);
  `pageTurn`/`skipPages` invert direction; CbzChapterActivity row 0 toggles (TOGGLE_RIGHT_TO_LEFT).
- Companion QR (0.42.0): `APP_STATE.companionQrPending` set with What's New; HomeActivity::onEnter shows
  QrDisplayActivity with docs/companion.md once. Home tap (0.42.0): `wasHeaderHomeTap` = tap in the rect
  (y 0 to under the strip, 48 px each side) or a swipe starting there shorter than 100 px.
- Header home button (0.41.0): `BaseTheme::headerHomeRect` draws icon_house_24 at the strip's
  centre and records HeaderHomeTapTarget (like HeaderBackTapTarget); ActivityManager::loop goes home
  on a tap there (before the status-band quick-settings tap); hidden on the home screen; setting
  `headerHomeButton` (default on, touch only). PNG sleep cover: `saveDecodedSleepCover` for JPEG/PNG.
- CBZ (0.39.0): `cbz::pages` from `ZipFile::enumerateFilePaths`; CbzReaderActivity copies the page
  entry to /.crosspoint/cbz_<hash>/page.jpg|png and draws it BW + LSB + MSB + BW (as BmpViewer);
  progress.bin holds the page. (0.53.0) Page cache: the first decode writes p<N>.pxc (2-bit, the
  fitted size, RenderConfig::cachePath) and every pass draws from it (renderImagePixelCache, RAM
  slot released after the page); after a page is shown the next one in reading order (RTL aware)
  is decoded into its .pxc between storeBwBuffer/restoreBwBuffer; caches 3 pages away are
  removed; size.bin (the CBZ's byte size) drops all page caches when the file changes.
  Spreads (after 0.53.0): width > height = two halves; RenderConfig::sourceHalf (1 left, 2 right)
  crops the source in both decoders; caches p<N>a.pxc / p<N>b.pxc by image side; currentHalf in
  reading order (RTL flips the side) in memory only, progress stays the page; back from a page
  lands on its last half, jumps and resume on the first; prefetch takes the second half first. Decoder
  cap: PNG 24 MP (MAX_SOURCE_PIXELS, was 8 MP), PNG_MAX_BUFFERED_PIXELS 24640 (two rows: RGBA
  3072 px, RGB 4096 px wide; was 16416), PNG heap guard 52 KB + 16 KB; JPEG 50 MP since 0.48.0
  (MAX_JPEG_SOURCE_PIXELS, decoded at up to 1/8 scale).
- Link rewrite (0.38.0): `linkupdate::plan` before `renameNote` (backlinks from the index, each
  target checked with `vaultlink::resolve` == old path, targets kept per note), then a confirm, then
  `apply` via `saveNote` (markChanged + writeAtomic). `linkrewrite::rewrite` skips fences and inline
  code, keeps prefix, `.md`, `#anchor`, `|alias`. `backlinks` also matches a path's end (resolve order).
- Recent notes (0.37.0): VaultPicker lists up to 5 RECENT_BOOKS entries ending in .md that exist and
  sit inside a found vault, under a "Recent notes" heading; a tap goes to the reader.
- Embeds (0.36.2): after a full .md conversion, `<cache>/embeds.txt` lists each embedded note as
  "size\tpath"; `Txt::validateCache` rebuilds when one is missing or its size changed.
- XTCH drawing (0.36.1): `GfxRenderer::blitXtchPlanes` in Portrait at 480x800, no clip or strip;
  panel row r = XTCH column r, so each pass is `xtchplanes::combineRow` (Black ~(p1|p2), LSB ~p1&p2,
  MSB p1^p2). Host test test/xtch_planes compares it with the per-pixel loops.
- Moves (0.35.1): `moveBookWithState` (util/BookCacheUtils) for Browse Files, /rename, /move and WebDAV
  MOVE. Files: cache, bookmarks, clippings with rollback. Folders: one rename with clippings, then
  each book inside (up to 400, 16 levels) gets cache/bookmarks/recents/favourites carried, best effort.
- Page backgrounds (kos25k's idea, mostly for fun): plain, paper, sandpaper, papyrus soft/medium/
  hard/cloudy (cloudy: value noise, no lines), toilet paper, kitchen paper, bath towel. Drawn as dots under the text in the B/W frame,
  computed from the pixel position (no memory, same on every page). Settings → Reader → Page
  Background, and a quick-settings tile that names the current one. EPUB and notes; not XTC.
  Text anti-aliasing is skipped while a background is on (0.8.3): on UC8279 X4 Pro panels the AA
  pass faded dotted pages. The tile steps through the styles checked under Settings → Reader →
  Paper Button Cycle (0.20.0; `paperCycle` bit mask, 0 = all).
- Line spacing (0.20.0, level 0 from 0.30.0): levels 0-10, stored as the level under
  `lineSpacingStep`, a multiplier on the font's line height (SD fonts 0.70-1.20, built-in 0.65-1.05).
  `lineSpacingLevel` (0.20.0-0.29.0, 0-9 for levels 1-10) loads one up; the old `lineSpacing` 0-3
  loads as levels 5, 6, 8, 10. Hide Status Bar (`statusBarHidden`) empties the status bar spec, so the text
  gets its height. Hyphenation defaults on.
- Sleep screen battery (0.20.0, `sleepBattery`): the percent at sleep time, black on a white patch
  at the bottom right; in gray picture planes the patch is kept white.
- 0.24.0: `SECTION_FILE_VERSION` 75 (the header carries `evenWordGaps`; `ParsedText::evenWordGaps`
  caps the justify extra at half a space, else the line stays ragged). `src/util/ZipExtract`
  (`safeEntryPath`), `src/util/WhatsNew` (`APP_STATE.lastVersionRun`; CHANGELOG.md embedded as
  `src/util/ChangelogData.generated.h`), `src/activities/home/GalleryActivity` (thumbs as pixel
  caches `/.crosspoint/thumbs/<hash>_<size>_<w>x<h>.pxc`), `src/util/FileOps` (copy, shared).
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
- Headings (0.47.0, index version 4): up to 64 heading texts per note (`#` to `######` followed by a
  space, outside code fences, trailing `#`s dropped), a field before the words, within 3/4 of the
  line. A search hit gets the first heading holding every query word (prefix match), unless the
  title already does; it scores +3, reads `Note > Heading`, and opens at MarkdownToHtml's heading
  id (vaultnav::openAtHeading). An older index is rebuilt once. (0.53.0, version 5) Block ids too,
  in the same field as `^id`: a line ending in ` ^id` (or `^id` alone) of letters, digits and
  dashes, outside code, up to 64 per note; their parts go into the words at any length, so
  `table-2` is found. A hit on one reads `Note > ^id` and opens at that block (the reader's own
  anchor, as `[[Note#^id]]` links do).
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
| Task lists `- [ ]` / `- [x]` | must | read only in Phase 1, toggle in Phase 4; ☐ / ☑ (U+2610/2611 from Noto Sans Symbols 2 in the built-in fonts, 0.50.0; SD fonts keep `[ ]`, cache tag `MD_CACHE_VERSION: 8 boxes`) |
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

Done in 4c (0.28.0): `lib/LiveSync/LiveSyncIds` (hashedPassphrase, chunkId, legacy noteId) and
`LiveSyncPush` (pieces <= 1000 bytes on UTF-8 boundaries, E2EE V2 `encrypt`, `_bulk_docs` for
chunks with "conflict" = already there, GET then PUT the note with the meta JSON in the plugin's
key order inside the `/\:` path field; refuses when the server `_rev` isn't the last pulled one).
The engine stores `revs.tsv` and reports conflict copies (`SyncOptions::onConflictCopy`), which
the device marks changed. Push needs `usePathObfuscation`, `idDerivationVersion` 0, no
`idDerivationKey`, `pathNormalizationVersion` 0, case-insensitive names and xxhash64; the profile
keeps `hashedPassphrase` and `sha256(passphrase)` (obfuscated), made at setup.

Done in 4a (0.22.0):
- Task boxes in a note's own lines become `<a href="task:<line>:<fnv1a32 hex>">` (lib/Vault/TaskToggle);
  the reader re-reads the file, checks the line's hash and list-task shape (BOM and CRLF handled),
  flips ' ' <-> 'x' (any other mark -> ' '), saves through `.tmp`, clears the note's caches and
  reopens it at the same page. `MD_CACHE_VERSION` 7.
- New / rename / delete in the note menu (`src/util/VaultEdit`). Delete moves to `<vault>/.trash/<rel>`
  (a time suffix on a clash). Rename moves the book cache and the recent entry; a case-only rename
  goes through a temporary name (FAT). Names: 1-100 bytes, none of `\ / : * ? " < > |` or control
  characters, no leading dot/space, no trailing dot/space.
- `/.vaultink/<id>/changed.txt`: paths changed on the device. The LiveSync pull
  (`SyncOptions::locallyChanged`) writes a remote change to such a path as
  `<name> (conflict YYYY-MM-DD).<ext>` and never trashes it; `SyncStats::conflicts` counts both.
- A note's `.tmp` is recovered when the note is opened (`ActivityManager::goToReader`).

Done in 4b (0.23.0):
- Line editor (`src/activities/vault/NoteEditorActivity`, `lib/Vault/LineBuffer`): lines with their
  own endings (LF/CRLF/none), BOM kept, new lines take the majority ending; undo records (20).
  Up to 256 KB; a line over 2000 bytes makes the note read-only. Save = `vaultedit::saveNote`:
  re-reads the file and refuses when size or FNV-1a hash differ from the load, then marks the note
  changed and writes through `.tmp`.

Planned:

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

- In this repo, `companion/`, Kotlin + Jetpack Compose, app ID `com.lefteros.vaultink`, Android
  only (minSdk 26). Guide: [docs/companion.md](docs/companion.md).
- Talks to the device over Wi-Fi through the device's web server while File Transfer is open
  (`/api/status`, `/api/files`, `/upload`, `/download`, `/mkdir`, `/rename`, `/move`, `/delete`,
  `/api/settings`). Finds it by the last address, `vaultink.local`, then mDNS `_http._tcp` named
  `vaultink`. BLE comes with Phase 5.
- Tabs: Note (quick note and share target), Files (whole card, transfers with progress), Settings
  (one card per device category), Device (status and app update).
- Signed APKs on GitHub releases tagged `companion-x.y.z`, never marked Latest. The app checks those
  releases, downloads the APK and opens Android's install prompt (no silent install).
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
- Update notes (0.46.0): ReleaseJsonParser keeps the release `body` (top level, up to the 2 KB
  JSON token buffer; longer bodies are dropped and the screen says there are no notes).
  OtaUpdateActivity wraps it, scrolls with Up/Down or a swipe, and keeps Cancel/Update fixed at the bottom.
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

- Folders tab (0.56.0, kos25k #18): LibraryListActivity slot 4 (FAVOURITES_TAB_INDEX stays 3); lib/LibraryIndex/LibraryFolders bucketByFolder groups the Title order by each record's folder id; LibraryIndexFile::readFolderPath/forEachFolder read the folder table; folders open like author folders; always Title order.
- PDF (0.56.0): lib/Pdf/PdfCrypt has SHA-256/384/512, AES-256-CBC decrypt, AES-128-CBC encrypt (no padding); PdfText setupAes256 does the R5/R6 hash and unwraps /UE for an empty user password; columnOrder finds 2 to 4 columns; Type0 fonts with Uni*-UCS2/UTF16 CMaps are read as UTF-16, other CID glyphs without a ToUnicode map are counted (Result::droppedGlyphs) and never emitted; Status::UnreadableFont.
- Reading stats (0.56.0): lib/ReadingLog Stats has currentStreakDays, longestStreakDays, finishedCount, finishedThisYear, finished (up to 5), dayMinutes (this month, all books).
- LiveSync ids v1 (0.56.0): LiveSyncIds chunkIdV1/noteIdV1 (HMAC-SHA256 keyed by idDerivationKey), isIdKeyHex; the key is stored obfuscated (pushIdK, pushNfc); the push gate accepts only the legacy scheme or v1 with a 64-hex key and path normalization 0 or 1.

- Browse Files search, 0.64.0: lib/FileSearch (Walker over a DirectorySource: breadth-first, MAX_DEPTH 12, MAX_HITS 200, MAX_PENDING 4096, skips dot folders and System Volume Information, foldName folds accents and case; truncated() when the hit or folder cap cut the list) with FileSearchActivity (keyboard, progress, results; a hit opens, or "Show in folder" goes to FileBrowserActivity with revealName and centerPending). FileOps gained removeTree and transferInto. Random page now uses FsHelpers::isRandomPageBook and ReaderActivity::skipRecents so a peek never reaches the Recent list or the Continue card.
- Phone to reader, 0.64.0: no server runs inside an editor, so the companion leaves files on the card through File Transfer: /.vaultink/phone-text.txt (KeyboardEntryActivity "From phone" button, up to 2,000 bytes) and /.vaultink/sync-request (LiveSyncDevice::autoSyncDue consumes it at wake). /api/livesync/status reports syncRequest. Compressed Excalidraw (compressed-json fences) is read through LzString::decompressFromBase64.
- Status bar, 0.65.0: statusBarHidden moved into Customise Status Bar (StatusBarSettingsActivity rows, category STR_CUSTOMISE_STATUS_BAR in SettingsList). statusBarBookPage and statusBarPageWhenHidden feed StatusBarSpec::showBookPage (a hidden bar keeps only that item, so the text lane is reserved); EpubReaderActivity::bookPageNumber estimates the page from the book fraction and this chapter's pages per share (page 1 = first TOC chapter) and publishes it through bookpage::number() for BaseTheme::drawStatusBar, drawn as "(N)" in the left cluster; the XTC reader sets the exact page.
- Battery log, 0.65.0: a fifth field on each line (light 0/1, S lines: HalFrontlight::usedSinceBoot() during the awake period); lines without it have lightKnown false. compute() adds awakeLightOff and awakeLightOn from wake-to-sleep pairs that end at a line with the field; the page shows both rows instead of one awake row.
- Paper styles, 0.65.0: PAPYRUS_AGED (dots thickening towards the drawn region's edges), PAPYRUS_CRACKED, PAPYRUS_STAINED and PAPYRUS_CRUMPLED (contours of smooth noise, broken up) appended to paper_texture::Style (14 styles; the cycle mask uses 14 bits); the contrast screen shows seven tiles per row.
- Flashlight, 0.65.0: FlashlightActivity (games/), sixth built-in row of Home > Plugins (BUILTIN_ROWS 6): HalFrontlight brightness, warmth and on/off from a millis() step pattern (S.O.S. 3-3-3 in units of the flash speed, Blink, Steady); restores the light's earlier state on stop or exit; preventAutoSleep while running; settings in /.vaultink/flashlight.txt.
- Text features, 0.66.0: lib/EpdFont/TextFeatures.h holds a global mask (LIGATURES, KERNING, WIDOW_ORPHAN, KEEP_HEADINGS, HANGING) set from ReaderRenderSpec::textFeatures by Section::loadSectionFile and createSectionFile and reset to the defaults by ReaderActivity::onExit. EpdFont::getKerning/getLigature/applyLigatures return nothing when their bit is off; ChapterHtmlSlimParser::linesToCarry and the keepWithNext line count check their bits; ParsedText::extractLine hangs a leading opener (left) and a trailing closer on justified non-last lines (right) by the glyph's advance, at most 12 px, LTR left/justify only. The mask is one byte in the section header (SECTION_FILE_VERSION 76) and in readerRenderSpecSignature. UI: ExperimentalSettingsActivity (shown as Advanced Typography since 0.66.1; Settings > Reader, right after Search word), Text panel row 11 (a popup of the five switches), MenuAction::HIDE_STATUS_BAR in the More panel.
- 0.72.0 (kos25k #6, #15): SETTINGS.browserFormatMask is 16 bits (BROWSE_PICTURES/DRAWINGS/ZIP, BROWSE_ALL 1023 in LibraryFormats.h, browseAllowed()), saved with browserFormatMaskV 2 (a 0.71.0 mask gets the three new bits on); LibraryFormatsActivity(browser=true) shows 11 rows; FileBrowserActivity::handleCustomInput reloads when loadedFormats/loadedSort differ from the settings; ACTION_REFRESH button; list props sidePadding 6, rowInset 4, valueInset 2, textGap 8. filesearch::Walker::setFileFilter drops filtered files from hits.
- 0.71.0 (kos25k #17, #6): icon_house_sleep_56x24 (customListIcons.h) replaces the 24 px house: BaseTheme::headerHomeRect is 56 wide, HeaderHomeTapTarget never hidden (ActivityManager ignores a tap on it from the home activity), VaultInkHomeUi leaves 8 px more above the card. BaseTheme::drawConnectivity shows the Bluetooth mark when SETTINGS.ble.enabled or the radio is up. SETTINGS.browserFormatMask (127 = all) filters FileBrowserActivity::loadFiles through library::formatAllowed; LibraryFormatsActivity(browser=true) is the File types list, with an All row (also in the Library's).
- 0.70.0 (kos25k #12, #14, #17, #6): lib/ReadingPresets (platform-free Layout/Preset/PresetList, own JSON reader/writer, 10 slots, names up to 24 bytes) with src/ReadingPresetsStore (reading-presets.json, capture/apply/saveCurrent), ReadingPresetsActivity (Settings > Reader) and the More panel popup (EpubReaderActivity::showReadingPresetsPopup). EpubReaderActivity::buildMoreActions drops Frontlight/Night Mode and orders: FIRST = Search, Go to %, Go to page; LAST = favourite, screenshot, QR, delete cache, Go home, Reading presets. ActivityManager::loop: a long press on HeaderHomeTapTarget calls requestSleep(); icon_house_moon_24. HalFile::addedTime() = max(created, modified) via util/FileTime.h (simulator falls back to modified); FileSort has 6 modes. VaultInkHomeUi: the card's author check subtracts the star strip.
- 0.69.0 (kos25k #14, #17, #6, #15): U+2605 is in the UI fonts (convert-builtin-fonts.sh, UI_STAR_INTERVALS), used for the favourites star by VaultInkHomeUi::setFavourite, FileBrowserActivity::provideRow (before the extension) and FrontlightPanelActivity::addBookCard. BaseTheme::drawConnectivity draws icon_wifi_16 / icon_bluetooth_16 (statusIcons.h) left of the battery after each fui::header (WiFi.getMode(), bleturner::status()). Browse Files sort: lib/FsHelpers/FileSort.h (filesort::before, 7 modes, folders first), SETTINGS.fileSort, FileBrowserActivity::openSortMenu and loadFiles. Type icons: util/FileTypeIcons (forFile by extension, forFolder by the most common type of up to 300 entries, cached 64 folders), 44 px bitmaps in icons/fileTypeIcons.h.
- Go to page, 0.68.0 (kos25k #29): MenuAction::GO_TO_PAGE (More panel, after Go to %, not in vault notes). EpubReaderActivity::bookPageScale() holds the pages-per-progress scale bookPageNumber() uses; openGoToPage() opens EpubReaderPercentSelectionActivity in page mode (slider 1..total, result in PercentResult.percent, side step max(10, total/20)) and jumps with jumpToBookFraction(first + (page - 0.5) / scale). Home (kos25k #14): VaultInkHomeUi has one tile height (ROW_HEIGHT 68) and icon_book_open_48 (customListIcons.h) for Books; HomeActivity::booksOnlyCount() counts index records with library::countsAsBook(mask, name) (not .txt/.md/.canvas), cached by index header and mask.
- FB2, HTML and RTF (after 0.73.1, not released; host tests only, not run in the simulator or on a device): three platform-free converters that stream a file into the same XHTML page a MOBI becomes, plus lib/Txt/MarkupBook as the device glue (probe for title/author/language, streamHtml, buildCache with a companion cover like a .txt); Txt::isPlainBook covers them, so the Epub class reads them as one spine item (content.html), and ActivityManager::goToReader refuses a file MarkupBook::probe can't read with STR_MOBI_UNREADABLE. lib/Fb2 (Fb2ToHtml): expat as a stream in 1 KB reads; sections, titles (h1 for a body's, h2 to h4 by section depth), subtitle, p, emphasis/strong/strikethrough/sub/sup/code, epigraph/cite/annotation/poem as blockquote, a stanza as one p with br per verse, text-author, tables, every body (notes last); image is ignored and binary blocks are parsed past, never kept; windows-1251/1252/1253 and ISO-8859-7 through expat's unknown-encoding handler; caps: 64 nested elements (TooDeep), 64 KB for all of expat's allocations through a counting memory suite (Oom: one huge comment or attribute), 200 bytes per info field, 64 KB read for the info; a file that stops being XML after text was written is closed properly (Truncated, still opens). lib/Html (HtmlBook): sniffs 4 KB for BOM / meta charset / XML declaration / html lang / title, decodes to clean UTF-8 (lib/CodePage Decoder: bytes that are not UTF-8 read as Windows-1252, control characters dropped), then mobi::HtmlFilter; UTF-16 refused. MobiHtmlFilter changed with it: section/article/header/footer/aside/nav/main/figure/figcaption/caption/dl/dt/dd become div, del is s, cite is em, svg is dropped; inside a dropped element (head, script, style, title, svg) a bare '<' and entities are no longer written, a '<' restarts the tag so "i<n" can't hide </script>, and <body> ends an unclosed <head>. lib/Rtf (RtfToHtml): a byte state machine with a 48-deep group stack (deeper groups share formatting), a 64-entry font table (\fcharset161 / 204, \cpg), \ansicpg, \deff, \uN with surrogates and the \ucN skip, \'hh by the font's or the document's code page, \bin skipped by count, \par/\sect/\page/\row paragraphs, \line, \b/\i/\v/\plain/\pard, \qc/\qr, \outlinelevelN as h1 to h4, \cell as " | ", every \* group and fonttbl/colortbl/stylesheet/pict/object/shp/header/footer/footnote/fldinst skipped; title and author from {\info}, language from \deflang; readInfo stops when the info group closes or text starts, at most 256 KB. lib/CodePage: the code page tables (Windows-1251/1252/1253, ISO-8859-1/-7, about 0.4 KB), the UTF-8 Decoder and XhtmlOut (buffered writer, page head with the cache version tag that Txt::validateCache checks: FB2_/HTML_/RTF_CACHE_VERSION 1). Library: no new mask bit; .fb2/.html/.htm/.xhtml/.rtf return FMT_MOBI from formatBitOf, the row is "MOBI / AZW / FB2 / HTML / RTF" (the Library mask is a uint8_t with seven bits used and bit 128 is BROWSE_PICTURES in the Browse Files mask, so a new bit would widen and migrate both stored masks). FsHelpers::hasFb2Extension / hasHtmlExtension / hasRtfExtension / hasMarkupBookExtension, part of hasReflowableBookExtension, isRandomPageBook and isLibraryBookFile; page icons FB2 / HTM / RTF in fileTypeIcons.h. Tests: test/markup_books (MarkupBooksTest, and Fb2FirmwareExpatTest built with lib/expat and the firmware's XML_GE=0 / XML_CONTEXT_BYTES=0), plus cases in test/mobi, test/fs_helpers, test/library_index_file and test/library_builder. Flash cost not measured (no device build yet).
- MOBI, 0.67.0 (kos25k #26): lib/Mobi. MobiFormat (platform-free) parses the PDB header and record table, record 0 (PalmDOC header, MOBI header, EXTH title/author/language), PalmDOC LZ77, trailing entries, cp1252 to UTF-8 and streams one text record at a time; MobiHtmlFilter turns Mobipocket HTML into well-formed XHTML for the expat layout parser; Mobi is the device glue (probe, buildCache, images by recindex into the book cache). A .mobi/.prc goes through the Epub class like a .txt (Txt::isPlainBook; one spine item content.html). Encryption type != 0 is refused (STR_MOBI_DRM); Huffman/CDIC (17480) is decoded by mobi::HuffCdic since 0.67.1 (dict1 + mincode/maxcode tables, CDIC phrases cached up to 48 KB, else read per phrase), from ActivityManager::openBook via Mobi::probe. FMT_MOBI = 64 in the Library format mask (127 = all; a mask saved before 0.67.0 gets the bit on at load). About 12 KB of flash. No chapters, no cover from EXTH yet.
- 0.72.1 (kos25k #17, #15): CrossPointWebServerActivity::loop also treats mappedInput.wasHeaderHomeTap() as leave (its tight handleClient loop pumps input itself, so ActivityManager never saw the tap); FileBrowserActivity::cutLongName() cuts a name that would not fit two lines with two dots and moves the extension into the label.
- 0.72.2 (kos25k #6): filetypeicon::dominantKind skips files browseAllowed(SETTINGS.browserFormatMask) hides, so a folder's type icon follows File types (the earlier 'txt and md still appear' was folders with the TXT/MD icon, not files).
- 0.72.3 (kos25k #15): src/util/RowFit.{h,cpp} rowfit::nameAndType builds one label (prefix + name + ' ' + ext, or name alone when it fills both lines, or name cut + '..' + ext) tested with GfxRenderer::wrappedText at the row's label width; used by FileBrowserActivity::provideRow (no value column now) and LibraryListActivity::buildRows (percent prefix, maxLines 2, sidePadding 6, rowInset 4).
- 0.73.0 (kos25k #15, #29): util/MenuLayout.h (arrange/fullOrder/move: stored order as a string of '0'+id, hidden as a uint32 mask, locked entries, never an empty panel; host test test/menu_layout) with settings moreMenuOrder/moreMenuHidden/textMenuOrder/textMenuHidden; the toolbar More panel (EpubReaderMenuActivity::arrangeMoreItems then MenuLayout) and Text panel (textRows, textpanel::ROWS, row ids stay stable, row 12 = Style checkbox) use them; BookMenuOptionsActivity edits them. PDF: Limits::styleMarks makes PdfText emit control bytes (1+digit..4 heading, 2 bold, 3 italic) from font size against the commonest size so far (>=1.2x; 1.8x/1.4x for h1/h2) and the font's BaseFont/Flags/FontWeight; TxtToHtml turns them into h1-h3/b/i when styled and drops them when plain, only for /.vaultink/pdf/*.txt; Txt::syncPdfStyle keeps pdfstyle.txt in the book's cache folder and wipes html and sections when Style changed; PdfConvert stamps with a converter version so old text is converted again.
- 0.72.5 (kos25k #29): PdfText records filled `re` rectangles (f/F/f*/B/B*/b/b*, axis-aligned, up to 48 per page, one path of more than 8 marks the page as a grid); layoutPage puts 12 U+2014 above and below the text of a fragment's smallest enclosing rect. Dropped: rects under 1.5 lines high or 4 em wide, backgrounds (80%+ of the fragments), boxes of short labels (under 6 characters a fragment), more than 6 boxes with text, a box round everything. Host test only.
- 0.72.4 (kos25k #6): folders use the plain folder icon again; filetypeicon::forFolder/dominantKind/forget removed (no per-folder SD read while drawing rows).

## KOReader parity milestone (decided 2026-11-10)

Requirements, in the order of the ROADMAP milestone: hidden flows; time-based warmth and night mode; customisable gestures, profiles and a Quick Menu; skim, book map and page browser; FB2, HTML and RTF; Wikipedia lookup; highlight exports (Readwise, Memos, Joplin); WebDAV, FTP and SFTP clients; scroll mode (kos25k #30); an SSH terminal; a gamepad input scaffold. Out: text-to-speech, Dropbox, OCR and scanned-PDF reflow, DjVu, CHM and DOC in the firmware. Each step states its measured flash cost and is host-tested where it can be; device-only checks are named in the ROADMAP.
