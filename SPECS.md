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
- Library percents (0.75.0): the first paint after onEnter skips percents it does not have
  (`deferPercents`), then asks for one more paint that loads them; pinned recents and favourites are
  kept by path in `src/util/PathPercentCache.h` (index rows keep `percentCache`). `loadBookProgress`
  calls `Epub::load(false, true, true)`: cached numbers only, no protected-content check.
  `Epub::openProtection` asks `ZipFile::probe` first (skips only on NotArchive, not on Unreadable), so single-file books never reach the
  ZIP end-record scan.
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
- Random page, 0.60.0: Row::RandomPage (id 9, appended before Settings in DEFAULT_ORDER; homerows::hidden treats its bit in homeRowsHidden as "shown", so it is off by default). HomeActivity::openRandomPage picks an EPUB from the library index, sets randompage::request() and opens the reader; EpubReaderActivity::beginRandomPeek (in loadBook) records the saved place as the search session origin, sets search.active and jumps to a random byte of the book through locateBookSize. With 10 rows VaultInkHomeUi::drawRows is compact (gap 3, first two rows as tall as the rest). Since the row-fit fix (after 0.74.0) util/HomeRowLayout.h (homelayout::compute, host test test/home_row_layout) picks the layout from the space under the card and the three UI line heights: row height up to 68, then gap 8 to 3, then Books and Vaults lose the title font and the 48 px icon, then the status lines go; a row is never under 44 px or under its icon and text (the list grows a row to its content, which is what pushed the last rows off the screen before). When even that is too tall (10 rows on a landscape screen) `fits` is false and the bottom rows are still cut.
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
  caches `/.crosspoint/thumbs/<hash>_<size>_<w>x<h>.pxc`; after 0.74.0 render() draws the cached
  thumbs and frames, shows the page, then decodes the missing ones with a refresh every 2 s at most;
  `PngToFramebufferConverter` falls back to `PngSlowDecoder` (Adam7 and 16-bit, a gray canvas of the
  output size in PSRAM or heap; the two scanlines in PSRAM up to 64 KB each, on the heap up to 16 KB each; a failed thumbnail is kept in `src/util/FailedKeys.h` until the next scan and not decoded again) when PNGdec's open() returns 5), `src/util/FileOps` (copy, shared).
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
  skipped, up to 20,000), rewritten whenever the vault's listing changed. Links, images and note embeds with a
  bare name look it up there first (same order as the scan: fewest folders deep, then shortest
  path); the SD scan runs only when there is no list yet or the listed file is gone (renamed, or
  added since the list was written). Settings → Vaults → Rebuild Search Index deletes both files.
- Update cost (0.75.0): `state.txt` next to `index.tsv` holds one line: a 64-bit fingerprint
  (FNV-1a) of every file path in the vault plus each note's stamp, the note and file counts, and
  the stamps (size and time) of `index.tsv` and `files.txt` as written. It is set to `invalid`
  before either file is written again, so a failed or power-cut update can't leave a fingerprint
  for other contents, and a cut, emptied or replaced file doesn't match its stamp (such an
  `index.tsv` is rebuilt from the notes, not kept line by line). An update
  first walks the vault's folders once; stamps come from the folder entries, so no note is opened.
  Same state line and a current `index.tsv` header: nothing else is read or written. Otherwise the old index is checked line by line (path and stamp only); it is written
  again only when a note was added, changed or removed, and only those notes are read; then
  `files.txt` and `state.txt` are written. `state.txt` is written last and not at all when a note
  couldn't be read, so a cut-short update is redone. The walk keeps the note paths in one block
  of memory and a note's words are deduplicated without a heap node per word (both were small
  allocations, which stay in the internal heap). Search shows the keyboard first and updates the
  indexes when the words are in. On the device, before this, opening Search on an unchanged
  284-note vault took about 10 s and the internal heap fell to 604 bytes free; the new cost is
  not measured on a device yet.
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
- Server certificates are verified by default: a 7-root PEM bundle in flash (`src/util/CaBundle.h`
  until 0.73.1; 0.74.0: `lib/TlsPolicy/CaBundle.h` with 10 roots, see the KOReader parity
  milestone; made by `scripts/gen_ca_bundle.sh` from the Mozilla store), or `/.vaultink/livesync/ca.pem`
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
- Developer link (0.75.0, host tests only, not compiled for the device; docs/devlink.md):
  `[env:x4pro-dev]` = env:x4pro + `VAULTINK_DEVLINK`, `LOG_DBG_RUNTIME` and `debug_tool = esp-builtin`
  (debug builds keep `-Os`). Developer only, never a release build. lib/DevLink (platform-free, no
  heap; 22 host tests in test/dev_link): LineAssembler (non-blocking, 256-byte lines, an overlong
  line is dropped up to the next newline, CR dropped), parse() for `CMD:<NAME> [args]`, JsonLine
  (one object per line in a 768-byte buffer, escapes, a reply that does not fit becomes an error
  object). src/main.cpp reads serial bytes through LineAssembler in every build (the old
  `readStringUntil` could hold the loop for the Stream timeout on a partial line; a command now needs
  its newline) and answers `CMD:SCREENSHOT` as before. Only with `VAULTINK_DEVLINK`,
  src/util/DevLinkDevice answers PING, HELP, STATUS (version, uptime, heap, PSRAM, battery mV and
  percent, USB, charging, wake cause, reset reason, CPU MHz, frontlight, activity name, auto-sleep
  setting, idle time), LOG 0-3 (switches debug lines only: the other levels are compile-time), REBOOT
  and BOOTLOADER (`RTC_CNTL_FORCE_DOWNLOAD_BOOT`, then restart), one JSON line each sent as a single
  write into a 1024-byte TX queue; unknown commands get `{"ok":false,"error":"unknown"}`.
  scripts/devlink.py (stdlib, termios): status, cmd, screenshot to PNG/BMP, bench to CSV with
  reconnects; 17 tests against a fake device on a pty (test/test_devlink_script.py). No SD, settings,
  input injection or flashing over the link: next slice. Unverified: everything on the device.
- Developer link, second slice (0.75.0, host tests only, not compiled for the device):
  - SD bench log, `VAULTINK_DEVLINK` only. `CMD:BENCH [minutes 0-1440]`; the interval lives in
    `/.vaultink/bench.cfg` (no file = off, the default). `devlinkdevice::benchTick()` runs in
    `loop()` after the serial block (so after the USB Drive early return), never in the sleep path,
    no timer, no radio. A due row takes `RenderLock(Mode::Try)` for the two battery reads and
    retries next pass when the render task holds it; then one open/append/close of
    `/.vaultink/bench.csv`. At 256 KB (`BENCH_FILE_CAP`) the file is renamed to `bench.old.csv`.
    Columns: epoch (0 = clock not trusted), uptime_ms, battery_mv, battery_percent, usb, light_on,
    light_brightness, light_warmth, cpu_mhz, heap_free, heap_min, psram_free, activity, wifi, ble,
    wake_cause. Pure parts in lib/DevLink/DevLinkBench.{h,cpp} (`formatBenchRow` into a fixed
    192-byte buffer, `benchFileAction`, `benchDue` with unsigned millis arithmetic,
    `parseBenchMinutes`), a separate file so only the dev build links it; 10 host tests in
    test/dev_link/DevLinkBenchTest.cpp. `scripts/devlink.py bench-file <path>` prints the percent
    and mV drop per hour per awake stretch, grouped by light off / on / changed / cable in
    (7 more tests in test/test_devlink_script.py, 24 in all).
  - Developer label: `[env:x4pro-dev]` sets `CROSSPOINT_VERSION` to `<version>-x4pro-dev` (the
    X4 Pro flags moved to `[x4pro_flags]`, which env:x4pro and env:x4pro-dev both use, so the
    version is defined once per build); STATUS adds `"build":"dev"` and `bench_minutes`.
    `OtaUpdater::isUpdateNewer` is unchanged (three numbers): an equal release is not offered, a
    higher one is, and under `VAULTINK_DEVLINK` OtaUpdateActivity then draws "Developer build:
    Update installs the release build" and logs the decision at info level.
  - `tlspolicy::VERIFY_GITHUB` = `VAULTINK_VERIFY_GITHUB != 0`, a macro that defaults to 0 in
    TlsPolicy.h. Only `[env:x4pro-dev-verify]` (= x4pro-dev + `-DVAULTINK_VERIFY_GITHUB=1`) sets
    it. test/tls_policy builds its tests twice (TlsPolicyTest, TlsPolicyVerifyGithubTest).
  - Shipped builds: one more entry in the command name table of lib/DevLink (`BENCH`, parsed as a
    known name and then ignored, like the other names); everything else is behind
    `#ifdef VAULTINK_DEVLINK` or the macro default. Not measured: no device build was run.

- Developer link, third slice (0.75.0, host tests only, not compiled for the device). All of
  it inside `#ifdef VAULTINK_DEVLINK`; no version bump.
  - Input: lib/DevLink/DevLinkInput.{h,cpp} `devlink::InputInjector`, a FIFO of 16 events
    (KeyDown/KeyUp/TouchDown/TouchUp), each due a delay after the one before was played (50 ms
    between gestures, the hold time between down and up), and never before a pass of the input
    loop ran 10 ms or more after the last event (the SDK's debounce takes a state read unchanged
    for more than 5 ms). `HalGPIO::update()` calls `advance(millis())` before `inputMgr.update()`.
    Keys: `HalGPIO::begin()` registers `InputManager::setButtonHook`, the SDK's seam for extra
    buttons, which ORs the injector's mask (bits 0-5 only; the power bit cannot be queued) into the
    state the SDK debounces, so press/release edges and held time are the SDK's. Touch: the SDK
    has no seam, so HalGPIO's touch methods (`wasTouchTap`, `wasTouchDown`, `wasTouchReleased`,
    `isTouchTapCandidate`, `isTouchHeldAt`, `wasTouchLongPress`, `wasSwipe`, `wasTouchActivity`,
    `lastTouchHeldMs`, `suppressTouchContact`) ask the injector first; it copies the SDK's
    thresholds (28 px hold slop, 59 px tap slop, 60 px swipe, 700 ms swipe limit, 500 ms long
    press) in panel pixels and returns the middle of a pixel as 0..1 so
    `GfxRenderer::tapToLogical` gives the pixel back. Commands take screen pixels;
    `devlink::logicalToPanel` is the inverse of `tapToLogical` for the four orientations.
    MappedInputManager and main.cpp are unchanged: `wasAnyPressed` / `wasTouchActivity` reset the
    auto-sleep timer as for real input.
  - Files: `src/util/DevLinkPaths.h` `devlinkpaths::isAllowed(path, List|Read|Write|Flash)`, an
    allow-list over one canonical spelling (`isPlainPath`), reusing `protectedpaths::isShortAlias`,
    `isSensitivePath` and `isInDeviceFolder`. `/debug/**` list/read/write, `/screenshots/**`
    list/read, `/.vaultink` list, read of `bench.csv`, `bench.old.csv`, `bench.cfg`,
    `battery-log.tsv`, write of `bench.cfg`; Flash: any `.bin` outside the device folders. LS: 200
    entries at most, one JSON line each without `ok`. GET: 1-2880 bytes, streamed as 540-byte
    pieces of base64 with `len`, `eof` and `ok` after the data. PUT: 180 bytes a command, offset 0
    truncates, any other offset must equal the file size, 1 MB cap. lib/DevLink/DevLinkArgs.{h,cpp}:
    `splitArgs`, `parseKeyArgs` (POWER is its own result), `parseTapArgs`, `parseSwipeArgs`,
    `parseGetArgs` / `parsePutArgs` (split from the end, so a path keeps its blanks), strict base64.
  - FLASH: battery under 30% without USB refuses; `esp_ota_get_next_update_partition`, size check,
    `firmware_flash::validateImageFile`, then `firmware_flash::flashFromSdPath` (which validates
    again and switches otadata); progress lines every 10%; the reply names the partition label; no
    `ESP.restart()`.
  - Tests: test/dev_link builds two programs: DevLinkTest (the table as shipped builds compile it,
    35 tests; `CMD:FLASH` is Unknown there) and DevLinkDevTest with `VAULTINK_DEVLINK=1`
    (and, since the debug page, `VAULTINK_DEVLINK_FULL=1`; 46
    tests: names, arguments, base64, the queue against a copy of the SDK debounce at loop steps of
    1 to 300 ms, the path rule with traversal, case, 8.3 aliases, trimmed names and double
    slashes). scripts/devlink.py: `key`, `tap`, `swipe`, `ls`, `get`, `put`, `flash` (an SD path;
    it copies nothing and sends no REBOOT); 41 tests in test/test_devlink_script.py.
  - Shipped builds: seven more values in `devlink::Command` (an enum, no bytes); their names and
    everything else are compiled only with `VAULTINK_DEVLINK`, so the name table is the size it
    was. Not measured: no device build was run. Relies on PlatformIO passing `build_flags` to the
    libraries in lib/ (as `LOG_DBG_RUNTIME` for lib/Logging already does).
  - Three macros since the debug page (2026-11-13; host tests only, no device build run):
    `VAULTINK_DEVLINK` compiles the link in (line dispatch, PING, HELP, STATUS, LOG, BENCH, REBOOT,
    the "Developer build" lines of the update screen, the 1024-byte TX queue);
    `VAULTINK_DEVLINK_FULL` adds BOOTLOADER, KEY, TAP, SWIPE, FLASH, LS, GET, PUT (all of
    lib/DevLink/DevLinkArgs and DevLinkInput, the injector in lib/hal/HalGPIO, the handlers in
    src/util/DevLinkDevice.cpp); `VAULTINK_DEVLINK_USER` marks the read-only build and is a
    compile error together with `_FULL` (lib/DevLink/DevLink.h). `[env:x4pro-dev]` and
    `x4pro-dev-verify` define DEVLINK + FULL (behaviour as before). `[env:x4pro-dev-user]` extends
    `x4pro-gh_release` with its flag list written out again plus DEVLINK + USER, `LOG_LEVEL=2`,
    `LOG_DBG_RUNTIME=1` and the version `<version>-dev`; STATUS says `"build":"dev-user"`, HELP
    lists seven names, BOOTLOADER answers an error. The release defines none of the three, so
    its preprocessed source is what it was. test/dev_link: a third program, DevLinkUserTest (4
    tests), 85 in all.
  - Device test harness (2026-11-14; the four x4pro envs compile and link, host tests only,
    nothing run on a reader). All firmware parts are inside `#ifdef VAULTINK_DEVLINK_FULL`:
    `devlink::Command` gains `Wait` and `ShotCrc` (names in the table and in HELP only with
    `_FULL`). lib/DevLink/DevLinkArgs: `parseWaitArgs` ("[activity] [timeout_ms]", a last word of
    digits is the time, 100 to 60000, default 5000, name 31 bytes at most), `WaitGate`
    (`start`, `tick(nowMs, atRest)` -> Idle/Pending/Done/TimedOut; Done after 3 passes in a row at
    rest that span 150 ms, wrap-safe), `crc32` (zlib's, bitwise, no table). ActivityManager:
    `renderCount` (atomic, +1 after each `render()`), `renders()`, `isAtRest()` = no pending
    action, `requestedUpdate` false, `paintedVisit == screenVisit`, `!RenderLock::peek()`.
    src/util/DevLinkDevice: `replyStatus(..., lite)` (`CMD:STATUS LITE` skips the battery block and
    its RenderLock; without `_FULL` `lite` is a constant false and the fields and their order are
    as before), eight more STATUS fields (`visit`, `renders`, `queued`, `wifi`, `ble`,
    `orientation`, `screen_w`, `screen_h`; worst case 765 of 767 bytes, pinned by
    `DevStatus.TheLongestReplyFitsOneLine`), `CMD:WAIT` stored and answered by `waitTick()` (called
    from the main loop after `benchTick()`; at rest = `devInput().queued() == 0 && isAtRest()` and
    the wanted name in front; a new WAIT replaces a pending one), `sendScreenshot()` (main.cpp
    calls it for `CMD:SCREENSHOT` with `_FULL`: same three parts, 512-byte pieces that wait for
    `availableForWrite()`, no end marker after 2 s without room; other builds keep the single
    write), `CMD:SHOTCRC` (`crc32`, `bytes`, `visit`, `renders`; framebuffer read without the
    lock). test/dev_link: DevLinkDevTest 54 tests (8 new: `DevWait`, `DevShotCrc`, `DevStatus`).
    scripts/devlink.py: `wait_command`, `wait`, `screenshot_checked` (3 tries, compares
    `zlib.crc32` and the byte count), `wait` and `screenshot --verify` on the command line.
    scripts/devtest.py (stdlib, imports devlink.py): `parse_scenario` (every refusal happens here,
    before a port is opened), `Runner` (one port; first STATUS must have `build == "dev"` and the
    battery at `battery_min` >= 30; `STATUS LITE` after every step for the forbidden screens
    UsbDrive, Sleep, OtaUpdate, SdFirmwareUpdate + `forbid_activity`; full STATUS every 10 steps
    for the battery; per-step `timeout_ms` and the run's `timeout_s`; on a failure one STATUS and
    one checked screenshot `failure.png`, skipped when the reader does not answer), JSONL log and
    pictures in `.work/devtest/<name>-<date>-<time>/`, exit 0/1/2, `--dry-run`.
    test/test_devtest_script.py: 28 tests against an in-memory port (parser, refusals, runs).
  - A build over Wi-Fi, `CMD:FETCH` (2026-11-14; host tests only, not compiled for the device,
    nothing run on a reader). All of it inside `#ifdef VAULTINK_DEVLINK_FULL`; `devlink::Command`
    gains `Fetch` (the name and HELP's `FETCH`, after `PUT`, only with `_FULL`).
    lib/DevLink/DevLinkFetch.{h,cpp}, platform-free and heap-free: `checkFetchUrl` (exactly
    `http://`, four decimal octets with no leading zero, `isPrivateIpv4` = 10/8, 172.16/12,
    192.168/16, port 1-65535 or 80, path of `[A-Za-z0-9._~/-]` with no `..`, no `//`, no end slash,
    160 bytes at most; results Ok/TooLong/NotHttp/BadHost/NotPrivate/BadPort/BadPath),
    `parseFetchArgs` ("<url> <sd path> <sha256>", split from both ends so the path keeps its
    blanks), `parseSha256Hex`/`sha256Hex`, `HttpHead` (status line and headers a byte at a time,
    160-byte line buffer, 4096-byte head cap, Content-Length strict, Transfer-Encoding noted) and
    `checkHead` (200 only, so no redirect is followed; length 1 to `FETCH_MAX_BYTES` = 0x7F0000).
    src/util/DevLinkDevice.cpp `replyFetch`: args, URL, `devlinkpaths::isAllowed(path, Flash)`,
    `WiFi.status() == WL_CONNECTED` (no radio started, no credentials), the 30% battery floor of
    FLASH, cap = min(0x7F0000, next OTA partition size), `WiFiClient::connect(ip, port, 5000)`,
    `GET <path> HTTP/1.0` + `Host`, head check, then 2048-byte chunks (heap) to
    `Storage.open(path + ".part", O_WRONLY|O_CREAT|O_EXCL)` and into wolfCrypt `wc_Sha256` (heap),
    progress lines `{"fetch":"writing",...}` every tenth, never past Content-Length; stop reasons
    from the pure `devlink::fetchWaitStep` (cancel by a byte on the serial port, 180 s in all, 10 s
    since a byte was really read, so an `available()` that yields nothing ends too, closed early);
    the part is renamed to the path only after the hash matches; any failure removes the part and
    `deleted` reports the result of that remove; reply `{ok,bytes,sha256,path,ms,next}`. Target
    rules after review (src/util/DevLinkPaths.h): `fetchMayReplace` (an existing file or stale part
    is replaced only when the name starts with `fw` or the path is under `/debug`),
    `fetchMayCreateFolder` (only `/debug`; any other parent must exist). `FLASH <sd path> [sha256]`
    (`devlink::parseFlashArgs`: a last word of 64 hex digits is the hash): `hashFile` before the
    image checks, a mismatch flashes nothing.
    It does not use HttpDownloader (that one follows redirects, has no hash hook and no deadline).
    scripts/devlink.py: `fetch <url> <sd path> <sha256>` (the same URL rule before anything is
    sent) and `push-update <file.bin> --expect <version> [--sd-path --host-ip --http-port
    --boot-timeout]`: `check_push_file`, STATUS must be `build: dev` + `wifi`, `lan_ip()` (UDP
    socket, no packet), `FileServer` (http.server in a thread of the script, bound to that address,
    `GET /firmware.bin` only and 404 for the rest, no child process and no temp folder, stopped
    after FETCH, at exit and on SIGINT/SIGTERM/SIGHUP), FETCH, FLASH with the hash, REBOOT,
    `wait_for_version` (STATUS every 2 s until the version matches and `uptime_ms` is from after
    the REBOOT); exit 0/1/2. scripts/deploy_web_installer.sh also refuses an `x4pro-dev-user` image
    that holds `usage: FETCH http://`. Tests: test/dev_link/DevLinkFetchTest.cpp (36 tests, in
    DevLinkDevTest, 90 there, 129 in the three programs); test/test_devlink_script.py 73 tests
    (32 new: the texts, the file server on 127.0.0.1, push-update against the fake device).
  - Debug page: site/debug.html (one static page, plain JS, Web Serial, vendor filter 0x303a,
    115200) + site/debuglib.js (no DOM; UMD so Node can load it): `createStreamParser` (lines
    from arbitrary chunks, CR dropped, `SCREENSHOT_START:<n>` switches to exactly n raw bytes,
    then `SCREENSHOT_END` within 20 lines, 8192-byte line cap, `reset()` for a lost port),
    `parseJsonLine`/`parseReply`/`parseScreenshotStart` and `rotateFramebuffer` ported from
    scripts/devlink.py (800x480 1-bit to 480x800, checked against devlink.py by hash),
    `statusRows`, `redact` (URLs, e-mail, MAC, IPv6, IPv4, SSIDs in the firmware's log wording,
    SD paths and file names with the ending kept, host names, the user's own words) and
    `buildRedactedReport` (redaction over the note, STATUS values and log lines only), `replyMatches` (a reply is taken only by the command it answers), `abandonPicture` (a timed-out picture is discarded, not shown as lines).
    test/debuglib_test.js: 40 cases, `node test/debuglib_test.js`, no packages. The page keeps
    5000 log lines, one command at a time (3 s reply timeout, 20 s for a picture), reopens the
    port on the browser's `connect` event, never writes reader text as HTML, and uploads nothing.
  - Publishing: scripts/deploy_web_installer.sh builds `x4pro-gh_release` then `x4pro-dev-user`,
    refuses a developer image that holds the full link's HELP text, writes `manifest-dev.json`
    (name "VaultInk (developer build)", version `<version>-dev`) and
    `vaultink-x4pro-dev-factory.bin` to gh-pages, and attaches
    `vaultink-<version>-x4pro-dev-factory.bin` to the release (the update check asks for exactly
    `vaultink-<tag>-x4pro.bin`, compared with strcmp in lib/JsonParser/ReleaseJsonParser.cpp).
    `--dry-run` stops before the first push. scripts/deploy_site_only.sh republishes the developer
    image from the release when it is there; without `manifest-dev.json` the install page hides
    the button. Neither script has been run with these changes.

## 14. Licences and credits

- VaultInk: MIT, keeping CrossPoint's copyright notice.
- Third-party: CrossPoint Reader (MIT), FreeInk SDK, MicroMarkD parser (MIT, Daniil Pankov) if used,
  fonts (OFL), NimBLE (Apache-2.0). All listed in NOTICE.
- The LiveSync wire format is implemented from the public source; no LiveSync code is copied unless
  its licence allows it and it is credited.
- Licence list (0.75.0; host tests only, not compiled for the device, the companion change not
  built): docs/third-party.json is the one source (name, version, used_for, licence as an SPDX id or
  `not verified`, copyright, source, used_in = firmware / device web pages / companion / site,
  verified_from = the file the licence was read from, optional notes and build_only).
  scripts/gen_licenses.py (stdlib) writes docs/licenses.md, the component list in NOTICE (between its
  two fixed sentences; the wolfSSL and wolfSSH statements are kept word for word) and src/util/ThirdPartyData.h
  (`thirdparty::COMPONENTS`, a constexpr table of five `const char*` a row for the firmware and
  device-web entries that are in the binary; URLs without the scheme; `STRING_BYTES`; the script
  refuses more than 6 KB of strings). `--check` exits 1 when an output is stale.
  LicencesActivity (Settings > System > About, first row, `STR_LICENCES`) is a UiListActivity: the
  intro line `STR_LICENCES_INTRO`, then a row per component (name, licence as the subtitle); a row
  sets `detail_` and the same screen draws the detail view (name, Licence, Used for, Source,
  Copyright), which takes every input until Back, Confirm or a tap. AboutActivity has 12 rows.
  Companion: `APP_LICENCES` pills and a link to docs/licenses.md in AboutScreen.kt.
  test/test_licenses.py: outputs in sync, each in-repo verified_from file exists, `not verified`
  only with an empty verified_from, the string budget, the companion list and the site link.

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
- 0.70.0 (kos25k #12, #14, #17, #6): lib/ReadingPresets (platform-free Layout/Preset/PresetList, own JSON reader/writer, 10 slots, names up to 24 bytes) with src/ReadingPresetsStore (reading-presets.json, capture/apply/saveCurrent), ReadingPresetsActivity (Settings > Reader) and the More panel popup (EpubReaderActivity::showReadingPresetsPopup). Gestures in a preset (unreleased, computer tests only, not compiled for the device): Preset has hasGestures + Gestures {slots[9], quickMenu[6]} (raw bytes; the store runs gestures::sanitizeSlot / sanitizeQuick on capture and apply); JSON `"gestures":{"slots":[9 numbers],"quick":[6 numbers]}` written only for a saved preset with the flag, so a layout-only preset serialises to the same bytes as before and kFileVersion stays 1; on parse any other shape of the block is skipped and the preset stays layout only; save(list, slot, layout) drops gestures, save(list, slot, layout, gestures) keeps them; find(layout) is unchanged and find(layout, gestures) is what currentSlot uses. ReadingPresetsStore::saveCurrent(index, name, includeGestures = false), apply(index, bool* gesturesSet); save buffer 7424 bytes. ReadingPresetsActivity asks "Include gestures" (No/Yes, touch boards only) between the slot and the name; the reader's Save preset keeps the slot's flag; both say "Gestures set from the preset" after an apply that set them. EpubReaderActivity::buildMoreActions drops Frontlight/Night Mode and orders: FIRST = Search, Go to %, Go to page; LAST = favourite, screenshot, QR, delete cache, Go home, Reading presets. ActivityManager::loop: a long press on HeaderHomeTapTarget calls requestSleep(); icon_house_moon_24. HalFile::addedTime() = max(created, modified) via util/FileTime.h (simulator falls back to modified); FileSort has 6 modes. VaultInkHomeUi: the card's author check subtracts the star strip.
- 0.69.0 (kos25k #14, #17, #6, #15): U+2605 is in the UI fonts (convert-builtin-fonts.sh, UI_STAR_INTERVALS), used for the favourites star by VaultInkHomeUi::setFavourite, FileBrowserActivity::provideRow (before the extension) and FrontlightPanelActivity::addBookCard. BaseTheme::drawConnectivity draws icon_wifi_16 / icon_bluetooth_16 (statusIcons.h) left of the battery after each fui::header (WiFi.getMode(), bleturner::status()). Browse Files sort: lib/FsHelpers/FileSort.h (filesort::before, 7 modes, folders first), SETTINGS.fileSort, FileBrowserActivity::openSortMenu and loadFiles. Type icons: util/FileTypeIcons (forFile by extension, forFolder by the most common type of up to 300 entries, cached 64 folders), 44 px bitmaps in icons/fileTypeIcons.h.
- Go to page, 0.68.0 (kos25k #29): MenuAction::GO_TO_PAGE (More panel, after Go to %, not in vault notes). EpubReaderActivity::bookPageScale() holds the pages-per-progress scale bookPageNumber() uses; openGoToPage() opens EpubReaderPercentSelectionActivity in page mode (slider 1..total, result in PercentResult.percent, side step max(10, total/20)) and jumps with jumpToBookFraction(first + (page - 0.5) / scale). Home (kos25k #14): VaultInkHomeUi has one tile height (ROW_HEIGHT 68) and icon_book_open_48 (customListIcons.h) for Books; HomeActivity::booksOnlyCount() counts index records with library::countsAsBook(mask, name) (not .txt/.md/.canvas), cached by index header and mask.
- FB2, HTML and RTF (after 0.73.1, 0.74.0; host tests only, not run in the simulator or on a device): three platform-free converters that stream a file into the same XHTML page a MOBI becomes, plus lib/Txt/MarkupBook as the device glue (probe for title/author/language, streamHtml, buildCache with a companion cover like a .txt); Txt::isPlainBook covers them, so the Epub class reads them as one spine item (content.html), and ActivityManager::goToReader refuses a file MarkupBook::probe can't read with STR_MOBI_UNREADABLE. lib/Fb2 (Fb2ToHtml): expat as a stream in 1 KB reads; sections, titles (h1 for a body's, h2 to h4 by section depth), subtitle, p, emphasis/strong/strikethrough/sub/sup/code, epigraph/cite/annotation/poem as blockquote, a stanza as one p with br per verse, text-author, tables, every body (notes last); image is ignored and binary blocks are parsed past, never kept; windows-1251/1252/1253 and ISO-8859-7 through expat's unknown-encoding handler; caps: 64 nested elements (TooDeep), 64 KB for all of expat's allocations through a counting memory suite (Oom: one huge comment or attribute), 200 bytes per info field, 64 KB read for the info; a file that stops being XML after text was written is closed properly (Truncated, still opens). lib/Html (HtmlBook): sniffs 4 KB for BOM / meta charset / XML declaration / html lang / title, decodes to clean UTF-8 (lib/CodePage Decoder: bytes that are not UTF-8 read as Windows-1252, control characters dropped), then mobi::HtmlFilter; UTF-16 refused. MobiHtmlFilter changed with it: section/article/header/footer/aside/nav/main/figure/figcaption/caption/dl/dt/dd become div, del is s, cite is em, svg is dropped; inside a dropped element (head, script, style, title, svg) a bare '<' and entities are no longer written, a '<' restarts the tag so "i<n" can't hide </script>, and <body> ends an unclosed <head>. lib/Rtf (RtfToHtml): a byte state machine with a 48-deep group stack (deeper groups share formatting), a 64-entry font table (\fcharset161 / 204, \cpg), \ansicpg, \deff, \uN with surrogates and the \ucN skip, \'hh by the font's or the document's code page, \bin skipped by count, \par/\sect/\page/\row paragraphs, \line, \b/\i/\v/\plain/\pard, \qc/\qr, \outlinelevelN as h1 to h4, \cell as " | ", every \* group and fonttbl/colortbl/stylesheet/pict/object/shp/header/footer/footnote/fldinst skipped; title and author from {\info}, language from \deflang; readInfo stops when the info group closes or text starts, at most 256 KB. lib/CodePage: the code page tables (Windows-1251/1252/1253, ISO-8859-1/-7, about 0.4 KB), the UTF-8 Decoder and XhtmlOut (buffered writer, page head with the cache version tag that Txt::validateCache checks: FB2_/HTML_/RTF_CACHE_VERSION 1). Library: no new mask bit; .fb2/.html/.htm/.xhtml/.rtf return FMT_MOBI from formatBitOf, the row is "MOBI / AZW / FB2 / HTML / RTF" (the Library mask is a uint8_t with seven bits used and bit 128 is BROWSE_PICTURES in the Browse Files mask, so a new bit would widen and migrate both stored masks). FsHelpers::hasFb2Extension / hasHtmlExtension / hasRtfExtension / hasMarkupBookExtension, part of hasReflowableBookExtension, isRandomPageBook and isLibraryBookFile; page icons FB2 / HTM / RTF in fileTypeIcons.h. Tests: test/markup_books (MarkupBooksTest, and Fb2FirmwareExpatTest built with lib/expat and the firmware's XML_GE=0 / XML_CONTEXT_BYTES=0), plus cases in test/mobi, test/fs_helpers, test/library_index_file and test/library_builder. Flash cost not measured (no device build yet).
- DOC and CHM (0.75.0; host tests only, not compiled for the device, not run in the simulator or on a device): two more platform-free converters behind lib/Txt/MarkupBook, which now also dispatches on FsHelpers::hasDocExtension / hasChmExtension (both inside hasMarkupBookExtension, so Txt::isPlainBook, the Library scan, Random page and the format bit FMT_MOBI cover them with no other change). Both are containers, so they read through blocksource::Source (lib/CodePage/BlockSource.h: readAt(offset) over HalFile::seek + read) instead of the front-to-back Read callback. No third-party code: nothing added to docs/third-party.json. lib/Doc (DocToHtml): [MS-CFB] header, DIFAT (header entries and DIFAT sectors), FAT read through a 512-byte window, MiniFAT and mini stream, directory walked as the root storage's child tree (streams inside other storages are not looked at); a stream is a list of extents (12 bytes a run, MAX_EXTENTS 4096). [MS-DOC] FIB: wIdent 0xA5EC (0xA5DC Word 6/95 is Unsupported), fEncrypted (Encrypted, checked before anything else is trusted), fWhichTblStm picks 0Table / 1Table, ccpText / ccpFtn / ccpHdd / ccpMcr / ccpAtn / ccpEdn, fcClx / lcbClx; the Clx's Prc entries are stepped over and the PlcPcd read an entry at a time (MAX_PIECES 65536); a piece is 8-bit (FcCompressed: 0x82..0x9F through Windows-1252, the rest as is) or UTF-16LE. TextWriter: 0x0D and 0x0C end a paragraph, 0x0B is br, 0x09 a space, 0x07 a cell end (two in a row end the row), 0x13 / 0x14 / 0x15 fields show the result only (MAX_FIELD_DEPTH 16), 0x1E a hyphen, other control characters dropped, surrogate pairs joined. Main text, then footnotes and endnotes after an hr each. Title and author from the SummaryInformation property set (VT_LPSTR in its code page, VT_LPWSTR), language from the FIB's lid. The text read is capped at the WordDocument stream's size (overlapping pieces stop with Truncated). Statuses Ok / Truncated open the book; ActivityManager shows STR_DOC_ENCRYPTED for Encrypted and STR_MOBI_UNREADABLE for the rest. lib/Chm (ChmBook + Lzx): ITSF v2 / v3 header, ITSP directory header, every PMGL chunk read in file order (PMGI index chunks skipped), entries with 7-bit variable-length numbers; kept: #SYSTEM, the MSCompressed Content / ControlData / ResetTable entries, up to 4 .hhc entries, and for each .htm / .html page 16 bytes (name hash, section, offset, length; MAX_TOPICS 4096, the rest left out with Truncated). #SYSTEM gives the title (code 3), the contents file (code 0) and the LCID (code 4, else the ITSF one). ControlData: LZXC version 1 (bytes) or 2 (units of 32768), window 2^15 to 2^21; the reset interval is taken as it is (how 7-Zip reads it; the third number is not used). The ResetTable gives each 32768-byte frame's offset; a frame is read whole (at most 38912 bytes) and decoded by lzx::Decoder, which only goes forward, so a page before the current frame or past the next reset restarts at the reset before it. lzx::Decoder: 16-bit little-endian words, verbatim / aligned offset / uncompressed blocks, canonical Huffman decoded a bit at a time (no lookup tables: about 3 KB of state), lengths carried between blocks until a reset, R0..R2, a circular window; the last frame may be short or filled to 32768 (Microsoft's compiler fills it); the E8 translation is Unsupported and a match reaching before the last reset is Corrupt. The .hhc is scanned for object / param Name / param Local, each page listed once in that order with its title (MAX_TOC_TITLE 160 bytes, MAX_TITLE_POOL 64 KB, MAX_TOC_FILE 4 MB); each page goes through htmlbook::sniff (encoding; htmlbook::Info::named is new) and its own mobi::HtmlFilter, under an h2 with the contents title, then the unlisted pages after an hr each. The big blocks (window, frame input, page table, title pool) come from chm::Memory, which MarkupBook points at HalMemory::allocatePsram (never the internal heap). The page bytes written are capped at the file size plus the unpacked size. Tests (test/markup_books, 129 tests with the older three formats; run with ASan and UBSan): DocFixture.h writes OLE2 + Word files and ChmFixture.h writes CHM files with its own LZX encoder; every shorter file, 6000 and 4000 damaged files with a fixed seed, random bytes, a refusing sink, a failing allocator. Outside the tests' own writers: fixtures/libreoffice.doc (LibreOffice's MS Word 97 filter, from libreoffice.fodt), 7-Zip unpacking the encoder's files to the same bytes (ChmBook.WritesItsFixturesWhenAsked writes them), and one real 10 KB .chm (not in the repo) unpacking like 7-Zip. test/markup_books/MarkupConvertTool.cpp converts a .doc or .chm on the computer for trying real files. Not verified: a Word-written .doc, a real multi-frame .chm with a contents list, verbatim and uncompressed LZX blocks from Microsoft's compiler, any device behaviour (PSRAM allocation, speed, the icons and the longer row label on screen). Flash: 19.7 KB of object code at -Os with the ESP32-S3 toolchain (DocToHtml 7.1, ChmBook 10.2, Lzx 2.4), not a linked measurement.
- DOC and CHM hardening after review (2026-11-14, host tests only, test/markup_books/HostileBooksTest.cpp): xhtml::Out stops at xhtml::MAX_PAGE_BYTES (16 MB) and every converter (FB2, HTML, RTF, DOC, CHM) then fails with its TooBig status; blocksource::Source::ioFailed marks a read that failed inside the file, and DOC and CHM return IoError for it from readInfo and streamHtml, so Truncated (which MarkupBook accepts and Section promotes into html/0.html) now only means damage in the file itself; CHM Oom part way (the contents order, the LZX window) is Oom, not a cut book; chm::Status::TooComplex when the frames unpacked pass 4 x the section's frames + 2 x pages + 256 or MAX_DECODED (256 MB); the ResetTable's unpacked size is refused above MAX_PLAIN_SIZE (256 MB) or MAX_RATIO (64) x the packed size + one frame (TooBig); chm::Memory::pause is called every 8 frames and every 64 KB of page (MarkupBook gives vTaskDelay(1)); a directory number is 9 bytes at most; doc::Status::NoText for a document with nothing to show, found in readInfo by running the text to its first letter, so the refusal popup shows it. Checked in the code, not changed: Section.cpp writes the page to html/.tmp_N.html, removes it when the stream returns false and renames it to N.html only on success, so a failed convert leaves no page for Txt::validateCache to trust. Not done: pages are still written in contents order, not reordered to unpack forwards.
- MOBI, 0.67.0 (kos25k #26): lib/Mobi. MobiFormat (platform-free) parses the PDB header and record table, record 0 (PalmDOC header, MOBI header, EXTH title/author/language), PalmDOC LZ77, trailing entries, cp1252 to UTF-8 and streams one text record at a time; MobiHtmlFilter turns Mobipocket HTML into well-formed XHTML for the expat layout parser; Mobi is the device glue (probe, buildCache, images by recindex into the book cache). A .mobi/.prc goes through the Epub class like a .txt (Txt::isPlainBook; one spine item content.html). Encryption type != 0 is refused (STR_MOBI_DRM); Huffman/CDIC (17480) is decoded by mobi::HuffCdic since 0.67.1 (dict1 + mincode/maxcode tables, CDIC phrases cached up to 48 KB, else read per phrase), from ActivityManager::openBook via Mobi::probe. FMT_MOBI = 64 in the Library format mask (127 = all; a mask saved before 0.67.0 gets the bit on at load). About 12 KB of flash. No chapters, no cover from EXTH yet.
- 0.72.1 (kos25k #17, #15): CrossPointWebServerActivity::loop also treats mappedInput.wasHeaderHomeTap() as leave (its tight handleClient loop pumps input itself, so ActivityManager never saw the tap); FileBrowserActivity::cutLongName() cuts a name that would not fit two lines with two dots and moves the extension into the label.
- 0.72.2 (kos25k #6): filetypeicon::dominantKind skips files browseAllowed(SETTINGS.browserFormatMask) hides, so a folder's type icon follows File types (the earlier 'txt and md still appear' was folders with the TXT/MD icon, not files).
- 0.72.3 (kos25k #15): src/util/RowFit.{h,cpp} rowfit::nameAndType builds one label (prefix + name + ' ' + ext, or name alone when it fills both lines, or name cut + '..' + ext) tested with GfxRenderer::wrappedText at the row's label width; used by FileBrowserActivity::provideRow (no value column now) and LibraryListActivity::buildRows (percent prefix, maxLines 2, sidePadding 6, rowInset 4).
- 0.73.0 (kos25k #15, #29): util/MenuLayout.h (arrange/fullOrder/move: stored order as a string of '0'+id, hidden as a uint32 mask, locked entries, never an empty panel; host test test/menu_layout) with settings moreMenuOrder/moreMenuHidden/textMenuOrder/textMenuHidden; the toolbar More panel (EpubReaderMenuActivity::arrangeMoreItems then MenuLayout) and Text panel (textRows, textpanel::ROWS, row ids stay stable, row 12 = Style checkbox) use them; BookMenuOptionsActivity edits them. PDF: Limits::styleMarks makes PdfText emit control bytes (1+digit..4 heading, 2 bold, 3 italic) from font size against the commonest size so far (>=1.2x; 1.8x/1.4x for h1/h2) and the font's BaseFont/Flags/FontWeight; TxtToHtml turns them into h1-h3/b/i when styled and drops them when plain, only for /.vaultink/pdf/*.txt; Txt::syncPdfStyle keeps pdfstyle.txt in the book's cache folder and wipes html and sections when Style changed; PdfConvert stamps with a converter version so old text is converted again.
- 0.72.5 (kos25k #29): PdfText records filled `re` rectangles (f/F/f*/B/B*/b/b*, axis-aligned, up to 48 per page, one path of more than 8 marks the page as a grid); layoutPage puts 12 U+2014 above and below the text of a fragment's smallest enclosing rect. Dropped: rects under 1.5 lines high or 4 em wide, backgrounds (80%+ of the fragments), boxes of short labels (under 6 characters a fragment), more than 6 boxes with text, a box round everything. Host test only.
- 0.72.4 (kos25k #6): folders use the plain folder icon again; filetypeicon::forFolder/dominantKind/forget removed (no per-folder SD read while drawing rows).
- Scroll mode (kos25k #30, 0.75.0; first run on a real X4 Pro 2026-11-14 in a developer build: works, about 1.4 s a step): CrossPointSettings scrollMode (0), scrollStep (scrollview::StepKind, default kStepHalfScreen), scrollRefreshEvery (1-30, default 8); Text panel row 13 (textpanel::ROWS). lib/ScrollView (header-only, test/scroll_view): Row/Pos/Segment/View, buildRows, compose (whole rows across page ends, reachesEnd), advance, advanceByPixels, endStart, scroll (never passes an unseen row, stops at the chapter's last full screen), stepFor, capRefreshCounter. EpubReaderActivity::composeScrollView() builds one Page from the rows of section->currentPage from scrollRow on and of the pages after it (elements moved by yShift, PageBorderBox::clipVertically, links moved, per-element source page and first word index for clipping highlights); pageTurn() queues pendingScroll and changes chapter only when scrollEndVisible or at row 0 of page 0; renderBook() caps pagesUntilFullRefresh at scrollRefreshEvery. Section cache format unchanged. Progress stays spine + page (the page of the top row); <cache>/scroll.bin holds spine, page, row (u16 LE each) and is ignored when it does not match progress.bin. MappedInputManager::wasSwipe() also gives the end point.

## KOReader parity milestone (decided 2026-11-10)

Requirements, in the order of the ROADMAP milestone: hidden flows; time-based warmth and night mode; customisable gestures, profiles and a Quick Menu; skim, book map and page browser; FB2, HTML and RTF; Wikipedia lookup; highlight exports (Readwise, Memos, Joplin); WebDAV, FTP and SFTP clients; scroll mode (kos25k #30); an SSH terminal; a gamepad input scaffold. Out: text-to-speech, Dropbox, OCR and scanned-PDF reflow, DjVu, CHM and DOC in the firmware. Each step states its measured flash cost and is host-tested where it can be; device-only checks are named in the ROADMAP.
- Certificate policy (0.74.0, Release B step 1, computer tests only): lib/TlsPolicy. `TlsPolicy.h/.cpp` is platform-free (test/tls_policy): `hostOf`, `isPublicHost` (github.com, githubusercontent.com, wikipedia.org and names under them, matched on a dot boundary), `choose` -> Mode Plain / Insecure / Verify / CustomCa, and `decide`, which also loads ca.pem and makes sure the clock is set through two hooks (Clock, CaFile). Rules: a public host is always Verify with the public roots, no switch, ca.pem ignored; a server the user set up is Insecure unless its Check certificate switch is on, then CustomCa when `/.crosspoint/ca.pem` exists (unreadable or empty = Error::CaUnreadable, never a fallback to the built-in roots), else Verify with every root; a checked mode with `trustedtime::trustedNow() == 0` calls `syncNow(8000)` and fails with Error::NoClock when the date is still unknown. `TlsPolicyDevice.h/.cpp` binds it to SecureHttpClient, HalStorage and TrustedTime: `tlspolicy::apply(http, startUrl, userVerify, &error)`; on an error it sets an empty CA so a request sent anyway fails at the handshake. The rule is taken from the URL a request started at and applied to every redirect hop, so a redirect cannot lower it. ca.pem is read once and kept until the next boot (SecureHttpClient keeps the pointer). Callers: HttpDownloader (OTA check and firmware, fonts, OPDS, catalogs), PluginHttp, the web server's plugin fetch, KOReaderSyncClient and ClippingSync (`verifyCert` in the KOReader store, default false, a Check certificate row in the device and web settings). LiveSync keeps its own code and `/.vaultink/livesync/ca.pem`. OPDS has no per-server switch yet (stays unchecked). No certificate pinning: SecureClient keeps `_ssl`/`_ctx` private and has no verify callback or peer-certificate accessor, and the SDK is read-only. `lib/TlsPolicy/CaBundle.h` (moved from src/util, generated): 10 roots in one string, self-hosted group first (DigiCert G2, GTS R1, GTS R4, Amazon 1, USERTrust RSA), public group last (ISRG X1, ISRG X2, USERTrust ECC, Sectigo E46, GlobalSign R3); `ca_bundle::publicPem()` points into the same string, so the public group is not stored twice. PEM text 10,146 -> 12,992 bytes (+2,846 bytes of flash before the code; the real figure needs a device build). Chains read with `openssl s_client` on 2026-11-10: *.wikipedia.org = Let's Encrypt YE2 -> ISRG Root YE -> ISRG Root X2 -> ISRG Root X1; github.com and api.github.com = Sectigo DV E36 -> Sectigo Root E46 -> USERTrust ECC; release-assets and objects.githubusercontent.com = Let's Encrypt YR1 -> ISRG Root YR -> ISRG Root X1; all six verify with OpenSSL against the public group alone. GlobalSign R3 is in for Wikimedia but no chain read that day used it. Not verified: a wolfSSL handshake on a device with these roots (P-384 and RSA-4096 signatures, heap).
- Wikipedia lookup (0.74.0, Release B step 2, computer tests only): lib/Wikipedia (platform-free, test/wikipedia): `languageFor(book, ui)` (2-3 letter code before `-`/`_`, lower case, nb -> no, else the UI code, else en), `encodeTitle` (trims spaces, quotes and punctuation around the selection, drops soft hyphens, spaces to `_`, percent-encodes everything outside `A-Za-z0-9-._~`, 200 bytes at most), `summaryUrl` = `https://{lang}.wikipedia.org/api/rest_v1/page/summary/{title}`, and `SummaryParser`: a small streaming reader that keeps only the top-level `type`, `title` and `extract` strings (escapes and surrogate pairs to UTF-8), stops at 8 KB and, when the limit falls inside the extract, keeps what came, cut on a whole character, with an ellipsis. It is its own reader because the SDK's JsonSax drops any string over its 2 KB token buffer, which a long summary is. `finish(status)`: 404 = NotFound, `type` disambiguation = Disambiguation, no extract = Invalid. src/network/WikipediaClient: one GET through SecureHttpClient with `tlspolicy::apply` (always Verify), a User-Agent with the firmware version, up to 3 redirects on the same client (same certificate rule), a 15 s timeout, 40 KB free / 20 KB largest-block floor. UI: DictionaryWordSelectActivity's Not found popup becomes "Not found. Look up on Wikipedia?" for 8 s when the reader set a handler (Confirm or a tap on the popup takes it); EpubReaderActivity::launchWikipediaLookup flushes the reading session and replaces the reader with WikipediaLookupActivity (as KOReader sync does, so Wi-Fi and TLS get the reader's memory), which uses WifiSelectionActivity when not connected, fetches, calls `esp_wifi_stop()` at once, shows the result in DictionaryDefinitionActivity as plain text, and leaves through `silentRestartToReader()`. No timer or wake-up is added. The lock screen (boot only) and USB Drive mode (replaces every activity) come before a book is open, so the lookup cannot start there. Not verified: anything on a device or in the simulator (heap, the popup's width in Greek, the restart back to the page).
- Translation lookup (0.75.0, computer tests only, src/ not compiled for the device): lib/Translate (platform-free, test/translate): `languagePair(book, ui)` (source = the book's 2-letter code or empty, target = the UI code, else en; source == target gives target en, and no pair for en/en), `cleanQuery` (trim, runs of spaces and line ends to one space, no soft hyphens or control characters, empty over 500 bytes: MyMemory's documented "Max 500 bytes", kept for both services), `percentEncode`, `myMemoryUrl` = `https://api.mymemory.translated.net/get?q=..&langpair=src%7Ctgt` (empty without a source: no documented language detection; the `de` e-mail parameter is never sent), `libreTranslateUrl` ({server}/translate, http or https only), `libreTranslateBody` (JSON `q`, `source` or "auto", `target`, `format` "text", `api_key` when set, own escaping), and `AnswerParser`: one bounded streaming reader for both services that keeps `translatedText` (top level or under `responseData`), `responseStatus` (number or string), `responseDetails`, `quotaFinished` and `error`, stops at 8 KB, keeps 2 KB of text cut on a whole character, and decodes numeric and the five named HTML entities. `finishMyMemory`: HTTP 429, status 429, quotaFinished true or a text starting "MYMEMORY WARNING" = Quota; 5xx = ServerError; 403 = BadLanguage when the details mention LANGUAGE, else Refused (MyMemory answers HTTP 200 with `"responseStatus":"403"`, captured 2026-11-13); other non-200 = Invalid; empty text = NoTranslation. `finishLibreTranslate`: 429 Quota, 403 Refused, 5xx ServerError, 400 BadLanguage/Refused by the error wording (from memory) else Invalid. src/TranslateStore (`/.crosspoint/translate.json`: serverUrl, apiKey_obf obfuscated like the other tokens, verifyCert default false, the two notice flags; a new address asks the notice again). src/network/TranslateClient: the user's server when its address is set (POST, `tlspolicy::apply` with its Check certificate switch), else MyMemory (GET, always checked); no redirects, no connection reuse, 15 s, the same heap floor as Wikipedia; only the host is logged. TlsPolicy: `alwaysChecked("api.mymemory.translated.net")` (that one name), against every built-in root: chain read with `openssl s_client` on 2026-11-13 = mymemory.translated.net -> Amazon RSA 2048 M04 -> Amazon Root CA 1, verifies with that root of CaBundle.h alone and not with the public group. UI: DictionaryWordSelectActivity's offer becomes "Not found. Wikipedia or Translate?" when both handlers are set (Confirm or the popup's left half = Wikipedia, Right or its right half = Translate); EpubReaderActivity::launchTranslateLookup replaces the reader with TranslateLookupActivity, which asks once per service through ConfirmationActivity before any radio use (only the selection and the two language codes are sent), then follows WikipediaLookupActivity (Wi-Fi join, fetch, `esp_wifi_stop()`, DictionaryDefinitionActivity with "MyMemory (en > el)" or "LibreTranslate (..)" under the text, `silentRestartToReader()`). Settings > Connectivity > Translation (TranslateSettingsActivity) and web keys translateServerUrl, translateApiKey (obfuscated: the page gets only hasPassword), translateVerifyCert. Not verified: any of it on a device or in the simulator, the device's handshake with MyMemory, MyMemory's quota answer (shape from memory), a real LibreTranslate server.
- Gamepad scaffold (0.75.0, computer tests only, src/ not compiled for the device, never tried with a real gamepad): input plumbing only. lib/Gamepad/Gamepad.h (platform-free, test/gamepad): `Map` {addr of one pad, up to 16 (code, action)}, code = the SDK's `RawButtonEvent::code()` (value | byte index << 8 | report id << 16), actions = the 13 of `kLearnable` from lib/Gestures (no Default, no None); `learn` (one button per action, one action per button: Saved / Moved on a conflict / NewPad when another pad's map is dropped / Refused), `lookup`, `buttonFor`, `clear`, `reset`, `routes(map, linkedAddr)`, `Listener`/`hear`/`heardAtTimeout` (a press is kept on its release, a `wasRest` release drops it, a press with no release is kept after 15 s), `format`/`parse` ("RRBBVV:A" joined by commas; garbage, unknown actions and doubles are dropped). GamepadJson.h: settings.json keys gamepadAddr and gamepadButtons, written only while a button is mapped (SETTINGS.gamepad). The SDK's raw ring has one reader, lib/BlePageTurner, so the hook is there: `bleturner::Host` gets two optional pointers, `padRoutes(addr)` (asked once per link) and `padPress(code, &acted)`; while the host's map is for the linked device, serveReader gives every raw press to the host first and, as with a remote's table, lets the raw edges decide the rest (the key of the same raw press through `pageActionFor`), so no frame acts twice. main.cpp: padPress = `gamepad::lookup` then `ActivityManager::remoteAction`: page and chapter turns through `remoteTurn`, the others through `ReaderActivity::remoteAction` under the `remoteTurnAccepted` gate; EpubReaderActivity leaves the action in `pendingQuickAction`, so it runs through `runReaderAction` on the next loop pass like a Quick Menu row. Other readers take only the four turns. UI: a Gamepad Buttons view inside BleSettingsActivity (a view, not a screen, because the radio stops on every screen change): learn, hold to clear, clear all; forgetting the bond resets the map. Limits from the SDK source (docs/gamepad.md): first 8 payload bytes, first differing byte only, a byte value not a bit, so no sticks, triggers or two buttons at once.
- Auto warmth (0.74.0): lib/AutoSchedule (inNightWindow, next: a night window that may cross midnight; acts only on a switch of the window), src/util/AutoWarmthState.h (autoWarmthApplied bit 0 night side applied, bit 1 the schedule turned night mode on; the night value is what a manual change inside the window edits), main.cpp handleAutoSchedule (every 30 s, needs trustedNow() > 0 and the RTC, turning it off restores the day side once). Settings: autoWarmth, autoWarmthStart, autoWarmthEnd, autoWarmthNight, autoNightMode.
- Sunset to sunrise for Auto warmth (0.75.0, computer tests only, not compiled for the device): lib/AutoSchedule/SunTimes.h (platform-free, header only, double maths from <cmath>): sunTimes(year, month, day, latitude and longitude in tenths of a degree, the clock's UTC offset in minutes) gives sunrise and sunset in minutes of the local day (they can be below 0 or over 1439) or PolarDay / PolarNight / NoLocation; the formulas are NOAA's solar calculator (Meeus), evaluated at local noon, zenith 90.833°. inSunNight handles a night that sits inside one calendar day; nextSun is next() for the sun and hands PolarDay, PolarNight and NoLocation to the fixed hours; parseDegrees / formatDegrees read and write "37.9"; utcOffsetMinutes works the offset out from a local and a UTC breakdown of one moment. main.cpp sunTimesFor: HalClock::localTime, mktime + gmtime_r for the offset (DST included), one calculation per date/place/offset. Settings: autoWarmthWindow (0 fixed hours, the default; 1 sunset to sunrise), sunLatitude and sunLongitude (char[8] text, the first SettingInfo::String rows on the device's Settings screen: SettingsActivity opens the keyboard for a String with a category and checks these two with parseDegrees). Host test: test/auto_schedule against USNO's one-day tables (aa.usno.navy.mil/api/rstt/oneday, fetched 2026-11-13) within 2 minutes, plus polar cases and invariants.
- Book map (0.74.0): lib/BookMap (platform-free, test/book_map): groupChapters makes a partition of the spine from the contents entries with hiddenflows::chapterRange (entries that share an item or point backwards are skipped, uncovered items join the group before, no contents = one group per spine item), with each group's counted bytes and start as hiddenflows counts them; mergeGroups caps them at maxGroupsFor(width) = width / 6; layout gives hidden groups a fixed 6 px sliver and shares the rest by bytes with a 3 px minimum taken off the larger ones; segmentAt hits a segment narrower than 24 px up to 12 px from its centre; fractionIn/fractionAt and xAt map pixel and share of the book both ways (always inside a segment, so the jump does not land at the end of the chapter before); placeTicks, nearestTick and hitTest (a mark within 12 px beats a segment); pageAt and chapterStartPage are the status bar's page estimate. src/activities/reader/EpubReaderBookMapActivity (Activity + UiAppHost, one InputDrag hit region over the strip) takes a BookMapData of plain numbers plus a title callback and answers with a BookMapResult: a share of the book for jumpToBookFraction, a spine index for a hidden chapter, or a bookmark index, which the reader opens with bookmarkjump::resultFor + applyProgressChange (the same exact-offset jump as the bookmarks screen, now in BookmarkJump.h). EpubReaderActivity::openBookMap builds the data once (cumulative sizes, hidden flags, contents spine indexes; bookmarks by computedSpineIndex and page through visibleProgress, clippings from the already loaded ClippingStore, one per 1/512 of the book). Refresh: FreeInkDisplay has an experimental displayWindow, but HalDisplay and GfxRenderer do not expose it and nothing uses it, so scrubbing redraws the frame with FAST_REFRESH, at most one per 200 ms after the last frame, dropping positions in between; the first frame is HALF_REFRESH. MenuAction::BOOK_MAP is the 33rd action: the More panel's order and hidden mask have ids 0-31, so EpubReaderMenuActivity::layoutId gives it the id of SELECT_CHAPTER, which is never in More. Page browser: no thumbnails; EpubReaderChapterSelectionActivity (given pages per byte, so it stays right when a chapter is hidden there) and the toolbar's Contents panel show bookmap::chapterStartPage of each chapter as the row value.
- Hidden flows (0.74.0): lib/HiddenFlows (platform-free: visibleSize, visibleBytesAt, visibleFraction, locate, locateFraction over the cumulative spine sizes and one hidden flag per spine item; chapterRange for a contents entry; encode/decode of the file; everything or nothing hidden answers the raw numbers), host tests in test/hidden_flows. Epub: loadHiddenFlows, setSpineRangeHidden, getTocSpineRange, isSpineHidden, visibleBookSize, visibleProgress, locateVisibleSize, clearCacheKeepHidden; calculateProgress and getBookSize stay raw (KOReader sync through ProgressMapper, bookmarks). The list is `<book cache>/hidden.bin` (version byte 1, 16-bit count, then length + spine href each), not in book.bin, so BOOK_CACHE_VERSION is not bumped; it is matched by exact href in one pass over the spine. It moves with the cache folder (moveBookWithState), is kept by the reader's Delete Book Cache, and is lost with Clear Reading Cache in Settings and clearBookCache (an upload over the book). The flags and totals are in RAM only while something is hidden (5 bytes per spine item). Reader: bookPercentFor, renderStatusBar, bookPageScale, openGoToPage, jumpToPercent, jumpToBookFraction, beginRandomPeek, locateBookSize, the toolbar, getScreenshotInfo and getProgressBasisPoints use the visible numbers, and so does util/BookProgress (home and Library). UI: EpubReaderChapterSelectionActivity::onRowLongPress (touch long press, or Confirm held 1 s) and the toolbar Contents panel (ReaderToolbarUi Model::rowLongPress, Routed::longPress; Confirm held 1 s), marked with STR_CHAPTER_HIDDEN. EPUB only. Out: syncing the list, the companion, hiding by guide type, one sub-entry of a file.
- Gestures and Quick Menu (0.74.0, computer tests only): lib/Gestures (platform-free, header only: Slot = four corners, centre, four swipes; Action stored by value, 0 = Default, append only; Legacy = pageTurnGesture, previousPageGesture, tapZones, showReaderMenu, rtlBook; legacyTap and legacySwipe give what ReaderUtils::detectTouchPageTurn and isTouchMenuTap give; tapSlot, swipeSlot, resolve, tapAction, swipeAction, anyCustom, reset, sanitizeSlot, sanitizeQuick, quickMenuItems, slotKey, quickMenuKey), host tests in test/gestures (every combination of the four settings and both book directions against a copy of the old decoding). Settings: gestureAction[9] under "gesture.cornerTL" .. "gesture.swipeDown" and quickMenu[6] under "quickMenu.1" .. "quickMenu.6", written only when not 0, unknown values read as 0. Reader (EpubReaderActivity only): with every slot at Default detectTouchPageTurn and isTouchMenuTap run unchanged; with any slot changed and touchReaderControls on, ReaderUtils::detectTouchGesture decodes taps and swipes through the table (a touch in no slot, and a slot at Default, follow Legacy; the centre tap has the table as its only owner, isTouchMenuGesture(includeTap = false)); page turns continue on the old path, other actions go to runReaderAction after the link taps. MappedInputManager::wasGlobalEdgeSwipe keeps swipes from the top, left and bottom edge bands out of the slots. Fixed: buttons, edge swipes, header targets, long press. Quick Menu: an OptionPopup painted by renderBook, the picked row runs on the next loop pass (pendingQuickAction). UI: GesturesActivity (Settings > Reader > Gestures, touch boards), util/GestureLabels.h. Out for now: the other readers (ReaderActivity, XTC, CBZ), gestures inside Reading presets, brightness and warmth steps, two-finger gestures (no multi-touch API).
- Highlight exports (0.74.0, Release B, computer tests only, not compiled for the device, API shapes from memory and never tried against the real services): lib/HighlightExport (platform-free, test/highlight_export): `appendJsonString` (own escaper: control characters as escapes, invalid UTF-8 as U+FFFD, other UTF-8 kept, a character limit), `iso8601` (UTC), `ReadwiseBatch` (`add` returns false at 20 highlights or once the body passed 12 KB; fields text, title, author, source_type `vaultink`, category `books`, optional note, location with location_type `order` (reading-order rank; the reader has no book-wide page for a clipping), highlighted_at only for a timestamp from 2020 on, optional highlight_url), `memosContent`/`memosBody` (block quote, a line with book, author and chapter, tags `#highlights #vaultink` plus a tag from the title and the author; visibility PRIVATE), `memosUrl` (https when no scheme, `/api/v1/memos` appended once), `mapResponse(status, retryAfter)` (2xx Ok, <=0 NotConnected, 401/403 BadToken, 404 NotFound, 429 RateLimited with seconds, other 3xx/4xx Rejected, else ServerError), `sentKey(id, destination)` (FNV-1a 64) and `SentMarkers` (file `VSNT`, version, count, u64 keys, FNV-1a 32 check; anything that does not check out reads as empty; 2048 keys at most, oldest dropped). src/HighlightExportStore: PersistableStore at `/.crosspoint/exports.json` (readwiseToken_obf, memosUrl, memosToken_obf, memosVerifyCert; tokens obfuscated like the KOReader password, 512 characters at most, control characters refused), loaded at boot. src/network/HighlightExportClient: `Job::begin(destination)` (40 KB free / 20 KB largest-block floor, `tlspolicy::apply` with the Memos switch, redirects off so the token is not forwarded), `post(body)` (Authorization `Token`/`Bearer`, 20 s timeout, the answer body discarded past 16 KB, Retry-After read), the token wiped at `end()`; only the host and the status are logged. lib/TlsPolicy: `alwaysChecked` includes readwise.io, which is not in `isPublicHost`, so it is verified against all ten roots (PluginHttp, which asks `alwaysChecked`, treats it the same). UI: HighlightExportSettingsActivity (Settings > Connectivity > Highlight Exports; token rows show Set / Not Set and the keyboard opens empty), four web settings (readwiseToken and memosToken `withObfuscated`, memosUrl, memosVerifyCert); EpubReaderActivity: EXPORT_CLIPPINGS shows a choice (vault note, Readwise, Memos) on the reader's full-screen popup when a destination is set up (no new menu action: the 32 More-panel ids are used up), EpubReaderClippingListActivity adds the send rows to its row popup and returns `SendHighlightsResult`; `launchSendHighlights` saves the place and replaces the reader with SendHighlightsActivity (as KOReader sync and the Wikipedia lookup do), which loads the clippings, calls `ClippingStore::prepareSync()` so every clipping has its id, asks Send N / Send all N again / Cancel before any radio, joins Wi-Fi through WifiSelectionActivity, sends in reading order (Readwise in batches, Memos one by one, marks saved after each batch or every 10 memos to `/.vaultink/exports/<fnv64 of the book path>.sent`), calls `esp_wifi_stop()`, shows the result in a message popup and leaves through `silentRestartToReader()`. No timer or wake-up. The lock screen (boot only) and USB Drive mode (replaces every activity) come before a book is open, so a send cannot start there. Also fixed on the way: the vault note export set its message but not `showClippingMessage`, so its result never showed. Joplin: cut.
- Certificate policy note (0.74.0): `tlspolicy::VERIFY_GITHUB` is false, so GitHub keeps its encrypted-but-unchecked mode and only Wikipedia and Readwise (`alwaysChecked`) are always verified; PluginHttp uses `alwaysChecked` to decide on a fresh client.
- Certificate policy note (0.74.0): `tlspolicy::VERIFY_GITHUB` is false, so GitHub keeps its encrypted-but-unchecked mode and only Wikipedia (`alwaysChecked`) is always verified; PluginHttp uses `alwaysChecked` to decide on a fresh client.
- WebDAV and FTP clients (0.74.0, Release B, computer tests only, not compiled for the device, no real server tried): docs/webdav-ftp.md. Platform-free: lib/RemoteFiles (Entry {name, size 64-bit, mtime, isDir}; Listing with the caps MAX_ENTRIES 500, MAX_NAME_BYTES_TOTAL 16 KB, MAX_NAME_BYTES 255, each one setting `truncated` or counting `skipped`; `safeName` refuses empty, "." and "..", `/ \ " * : < > ? |`, control characters, a trailing dot or space; normalizePath refuses a path that climbs above the root; epochUtc). lib/WebDav (parseUrl: http/https only, no user info, no IPv6 literal, path decoded, normalised and encoded again; percentDecode refuses broken escapes and %00; hrefPath takes a path or a whole URL; resourceUrl / decodedRequestPath map a path under the server's base folder; `redirectAllowed`: same host, never https to http, same port unless http to https; base64 and `basicAuthorization`; `requestHead`, which returns nothing when a method, header or path holds a CR, LF or space; statusOf; parseHttpDate for RFC 1123; `MultistatusParser` on lib/expat without namespace processing, so element prefixes are stripped: href only as a direct child of response, properties only from a propstat whose status is 2xx or absent, a folder from resourcetype/collection or, when a server sends no resourcetype, from the href's trailing slash; only direct children of the folder asked for; at most 512 KB of answer and 2 KB per href; feed() returns false at a cap so the rest is not downloaded). lib/Ftp (parseUrl with or without `ftp://`; `command` refuses CR, LF and NUL in an argument; ReplyParser for multi-line replies, at most 64 lines of 1 KB; parsePasv, which ignores the address, and parseEpsv; parseListLine for Unix lines, found by the month column so a missing group works, and DOS lines; parseMlsdLine; `Session` over abstract Channel/Transport: login, list (MLSD, then LIST after a 500-504, remembered), retrieve, store as `name.part` + RNFR/RNTO with one DELE of the old file when RNTO is refused and a DELE of the .part on failure, makeDir, quit). Host tests: test/webdav (28 tests, firmware expat with XML_GE=0 and XML_CONTEXT_BYTES=0; golden answers in test/webdav/golden are hand-written, not captured) and test/ftp (26 tests, a scripted in-memory server). Device: src/RemoteServerStore (`/.crosspoint/remotes.json`, up to 6 servers, fields up to 127 bytes, `password_obf` through obfuscation::obfuscateToBase64 as OpdsServerStore does, `verifyCert` true when missing; the file is in protectedpaths::SENSITIVE_FILES); src/network/RemoteFsClient.h (`remotefs::Client`: list, download, upload, makeDir, close; `makeClient(server, plainConfirmed, error)` gives PlainRefused for FTP or http:// without the user's yes); WebDavClient.cpp (PROPFIND, GET and MKCOL through SecureHttpClient with redirects followed by hand, at most 3 hops, `tlspolicy::apply` with the server's switch on every hop; PUT written by hand on freeink::SecureClient or WiFiClient because SecureHttpClient only sends a body held in memory: Content-Length, a 4 KB buffer, no redirect; `tlspolicy::apply` got an overload for SecureClient); FtpClient.cpp (two WiFiClient connections, 20 s timeouts, the control connection kept while the browser is open and reopened once when the server dropped it). Both are stubs under SIMULATOR. Screens in src/activities/remote: RemoteServerListActivity and RemoteServerActivity (Settings > Connectivity; the password keyboard starts empty), RemoteBrowserActivity on CatalogActivity (Wi-Fi up on entry and down on exit as for OPDS; the "not encrypted" ConfirmationActivity before Wi-Fi for a plain server, every time; rows through a rowProvider; the list is freed before a transfer and read again after it; downloads refuse a destination and uploads a source that is a credential store). FileBrowserActivity has a new Mode::PickFile. CatalogActivity gained beginTransfer / transferProgress and an optional heading for the progress screen. Web: /api/remotes (GET, POST, POST delete), a section on SettingsPage.html; GET sends `hasPassword`, never the password. Out: DELETE/rename/move, resume, Digest, FTPS, active mode, IPv6 literals.
- SSH terminal and SFTP client (written 2026-11-13, 0.75.0, X4 Pro only; computer tests and a live run against OpenSSH 10.6 on 127.0.0.1; every new object compiled with the ESP32-S3 toolchain outside PlatformIO; **no PlatformIO build, no device run**): docs/ssh.md. Library: wolfSSH 1.6.0 (tag v1.6.0-stable, commit 8221e4ee), GPL-3.0-or-later, vendored byte for byte in lib/wolfssh (`src/internal.c io.c log.c misc.c ossh.c port.c ssh.c wolfsftp.c`, `wolfssh/*.h`, LICENSING; COPYING is the SPDX GPL-3 text); PlatformIO compiles only the wrappers in `lib/wolfssh/vaultink/ws_*.c` (`library.json` srcDir), each `#if VAULTINK_SSH` around `#include "../vaultink_wolfssh_config.h"` and `#include "../src/<file>.c"` (the relative paths matter: Arduino-wolfSSL has a `src/internal.c` of its own on the include path). Options in `vaultink_wolfssh_config.h`: `NO_WOLFSSH_SERVER`, `WOLFSSH_SFTP`, `WOLFSSH_TERM` + `NO_TERMIOS`, `WOLFSSH_USER_IO`, `WOLFSSH_NO_DH`, `DEFAULT_WINDOW_SZ` 32768, `DEFAULT_MAX_PACKET_SZ` 16384, `WOLFSSH_MAX_SFTP_RW` 8192; Arduino-wolfSSL's `NO_FILESYSTEM` is undefined for wolfSSH's own files and `WOLFSSH_USER_FILESYSTEM` selects `myFilesystem.h`, where every call fails (wolfsftp.c does not compile without a filesystem layer; the firmware only uses the buffer functions). `ws_wolfsftp.c` adds `VaultInk_SFTP_OpenDir` / `VaultInk_SFTP_ReadDir` after the include (wolfSSH_SFTP_GetHandle is static and the public wolfSSH_SFTP_LS reads a whole folder into one unbounded list). platformio.ini: `[vaultink_ssh]` (`-DVAULTINK_SSH=1 -DWOLFSSL_WOLFSSH -DHAVE_ED25519 -DWOLFSSL_ED25519_STREAMING_VERIFY -DWOLFSSL_SHA512`) in `[x4pro_flags]`, `x4pro-gh_release` and `x4pro-gh_release_rc`. Platform-free, lib/Ssh (test/ssh: 47 tests in SshTest and 6 in RemoteServerJsonTest): `sshterm::Terminal` (cells of 4 bytes in one block from a caller-supplied allocator, a ring of rows + scrollback lines; C0 controls BS HT LF VT FF CR; ESC 7 8 D E M c, charset and keypad sequences swallowed; CSI A B C D E F G H f d e a ` J K L M S T P @ X m r s u n c and the modes ?1 ?7 ?25, with ?47/?1047/?1049 counted and answered by erasing the screen since there is no alternate screen; OSC/DCS/APC/PM strings swallowed; UTF-8 to BMP code points; deferred wrap; a scroll region; answers to CSI 6n, 5n and c; `Changes` = dirty rows, scrolled lines, cleared) and `sshterm::RedrawPolicy` (fast refresh at most every 500 ms, full after a clear or 120 scrolled lines); `sshkh` (OpenSSH known_hosts lines, `[host]:port` for a port other than 22, check = Unknown / Match / Mismatch where any other key or key type for a pinned host is a mismatch, add with caps of 64 lines and 32 KB and a refusal of host names that could forge a line, remove, `fingerprint` = "SHA256:" + unpadded base64, strict base64); `sshkey` (the 97-byte key file: "VISK", version 1, iv 12, sealed seed and public key 64, GCM tag 16, the 5-byte header as additional data; the 51-byte public blob; the authorized_keys line; the 48-byte RFC 8410 PKCS#8 private key wolfSSH takes); `sshaddr` (`host[:port][/path]`, `[v6]:port`, optional ssh:// or sftp://, a user name in the address refused; `remotePath` under the start folder); `sshinput` (textinput events and the Ctrl latch to bytes; arrows as ESC [ or ESC O). lib/TextInput: `Special::Tab/Up/Down/PageUp/PageDown` and `HidTranslator::setTerminalKeys` (off by default: Ctrl with a letter or `[ \ ]` gives the control character, Tab and the up/down/page keys become events; Alt and Gui still give nothing; `apply` ignores all of them); `bleturner::setTerminalKeys`, reset by `setTextEntry(false)`. lib/RemoteFiles/RemoteServer.h: `struct RemoteServer` moved here with `Protocol::Sftp` and `keyAuth`, and `remoteserver::toJson/fromJson` (an SSH password is never written and never read, `keyAuth` only for SSH, an unknown protocol name is WebDAV). lib/SshClient/SshSession (wolfSSH through an abstract `Socket`; compiled for the computer by test/ssh_live): `openShell` / `openSftp` (host key algorithms asked for: ssh-ed25519, ecdsa-sha2-nistp256, rsa-sha2-256; the host-key callback accepts only a known_hosts match and otherwise records the key and Unknown or Changed; password and key each offered once; the password, the seed and the DER are wiped after the attempt), `readShell` (never waits; stderr data is shown too), `writeShell`, `resizeShell`, SFTP home/list/stat/open/read/write/close/mkdir/rename/remove with 8 KB chunks, a cancel flag and a timeout checked while waiting for the server; `generateKey`, `sealKeyFile` / `openKeyFile` (AES-256-GCM under SHA-256("ssh-key:" || device secret), the public half re-derived from the seed on open), `fingerprintOf`. Device: src/network/SshDevice (`/.vaultink/ssh/id_ed25519.key` and `known_hosts`, written through a .tmp and `replaceFile`; `trustHost` only adds where nothing is pinned; `heapAllowsConnection` = 60 KB free internal heap and a 24 KB block, not measured), SshWifiSocket.h (WiFiClient), SftpClient.cpp (`remotefs::Client`: the session kept while the browser is open and reopened once after a drop; listing with the lib/RemoteFiles caps, up to 32 symbolic links resolved by stat, other file types skipped; download to `.part` then `replaceFile`; upload straight to the name; `remotefs::Error` gained HostKeyUnknown, HostKeyChanged, NoCipher, Unavailable and `Client` gained `hostKey()` / `trustHostKey()`). `/.vaultink/ssh` is in `protectedpaths::SENSITIVE_FILES` (prefix match), so the web server, plugins, the remote file clients and the developer link (which also allows only named files under /.vaultink) cannot reach it. Screens: `RemoteServerListActivity(sshOnly)` for Settings > Connectivity > SSH servers (the entry only where `sshdevice::available()`), `RemoteServerActivity` rows Terminal, Browse files, Sign in with, Show public key, Forget the server's key and no password row for SSH; `SshKeyActivity` (QR, the line, the fingerprint, Make a new key); `SshTerminalActivity` (grid drawn glyph by glyph with `drawScaledCodepoint` from SMALL_FONT_ID into 10-pixel cells when the screen is at least 800 wide, else 8-pixel cells; 500 lines of scrollback in PSRAM; at most 4 reads of 1 KB per loop pass; the render lock around feeding; two rows of touch keys; onExit closes the session, switches Wi-Fi off and calls `silentRestartToSettings`, which does nothing on the way to sleep); RemoteBrowserActivity asks for the SSH password on entry and again on Retry, and shows the fingerprint question on HostKeyUnknown. The web settings page offers SSH as a protocol and never stores a password for it. Measured with the ESP32-S3 toolchain (-Os, before the linker drops unused functions): wolfSSH 75.3 KB, wolfCrypt additions about 67 KB (ge_operations 47.8, sha512 10.6, ed25519 3.7, asn +2.3, hash +1.2, kdf +1.2), lib/Ssh + lib/SshClient 25.8 KB, the new src files about 29 KB. Negotiated with OpenSSH 10.6: curve25519-sha256, ssh-ed25519 (ECDSA and RSA host keys also tried), aes256-gcm@openssh.com. Out: keyboard-interactive, agent, forwarding, SCP, an alternate screen, AES-CTR and chacha20-poly1305, delete/rename/resume in SFTP.
- SSH review fixes (2026-11-14, computer tests and test/ssh_live only; not rebuilt with PlatformIO): `sshkh::canonical` (the pinning lines as `host type key\n`, host in lower case, in file order), `sshkh::Mac` (the caller's keyed MAC), `sshkh::verify(present, readOk, text, mac)` -> FileState Missing / Good / Unreadable (read failed, over MAX_FILE_BYTES = 32 KB + 128, a NUL or control byte, or the MAC cannot be computed) / Changed (not exactly one `# vaultink-mac <64 hex>` line as the last non-blank line, or a wrong value; compared without an early exit), `sshkh::seal` (drops old MAC lines, appends the new one; false and no change when the MAC cannot be computed). `sshclient::knownHostsMac` = HMAC-SHA256 under SHA-256("ssh-known-hosts:" || device secret). `sshdevice::knownHosts(text)` -> Ok / Unreadable / Changed: reads by the file's size, at most MAX_FILE_BYTES, and checks that nothing follows; `trustHost` and `forgetHost` write only from Ok (through seal, .tmp and replaceFile), so a pin is added to the pins that are there and never to an empty text; `forgetAllHosts` deletes the file unread; `remotefs::Error::HostKeysUnreadable / HostKeysChanged`; SftpClient and the terminal stop before the TCP connection. `sshkh::KeyGate` (begin, accept, signedIn, reset): before the sign-in a key passes on a known_hosts Match and is remembered; after it only that blob passes (same length, compared without an early exit); `Session` owns one, the host-key callback uses it for the whole life of the session and a refusal after the sign-in sets `hostKeyChangedMidSession()` and ends the session at the next read or write. `Session::rekey()` (wolfSSH_TriggerKeyExchange) exists for the live check. `Login::deadlineMs` (30 s): `Io::deadlineStart/deadlineMs` checked at the start of every send and receive callback and while waiting, with the cancel flag, so a server that trickles bytes cannot stretch the sign-in; cleared after it. `writeShell` runs with `shellWriteTimeoutMs_` (3 s), and on WS_REKEYING / WS_WINDOW_FULL takes in what has arrived with `wolfSSH_worker` in non-blocking mode and tries again within that time; past it the session is closed. `sftpRead`: 0 from wolfSSH (its only return for the EOF or OK status) is the end of the file; every negative return is an error (NotFound turned into Refused for an open file). `sshaddr::downloadMayTake(received, more, expectedSize)` with MAX_DOWNLOAD_BYTES 2 GiB, used by SftpClient::download before each write. `sshkey::wipeConst` for the KeyboardResult text; SftpClient wipes its password after the sign-in (kept only across a HostKeyUnknown answer) and does not reconnect in password mode; RemoteBrowserActivity wipes its copy once the client is made. Terminal: `SshWifiSocket::setIdleHook` (about every 50 ms while waiting) reads Back and taps into the cancel flag during connectNow; TCP connect 8 s. `sshterm`: a ninth CSI parameter and later ones set `paramOverflow_` and are dropped. Tests: test/ssh now 60 in SshTest (MAC and file states, card-only edits, KeyGate call sequences, the download bound, CSI overflow); test/ssh_live adds the real HMAC, three real key exchanges mid-session (one SFTP, two shell, seen in the sshd log), a refused read, and the deadline against a trickling server.
- Transfer & Remotes (2026-11-14; computer tests and one simulator build, no PlatformIO device build, no device run): the Home row STR_FILE_TRANSFER reads "Transfer & Remotes" / "Μεταφορά & Διακομιστές" and opens CrossPointWebServerActivity as before. Its picker (NetworkModeSelectionActivity) builds its rows at run time from `networkmodes::buildRows(usbDrive, ssh)` (src/activities/network/NetworkModeRows.h, no device headers, test/network_mode_rows): Join a Network, Calibre Wireless, Create Hotspot, USB Drive (FREEINK_CAP_USB_MSC), Remote servers, SSH terminal (`sshdevice::available()`); a row's mode is looked up in that table by row index, not cast from it (UiListActivity passes the row index for a tap and for Confirm, so the index stays the action value). `NetworkMode::REMOTE_SERVERS` and `SSH_TERMINAL` are handled first in `onNetworkModeSelected`, before the Join Network restart and any Wi-Fi call: `showRemoteServers(sshOnly)` starts RemoteServerListActivity as a child and its result handler calls `showModePicker()` (one helper now, in place of four copies of the picker launch). The picker takes the mode to put the cursor on (`networkmodes::startRow`, set after UiListActivity::onEnter(), which resets it), so Back from a list or from Calibre lands on the row that was opened. The lists bring up no Wi-Fi themselves; a session that did still switches Wi-Fi off and restarts the device: RemoteBrowserActivity through CatalogActivity::onExit, always to Home; SshTerminalActivity::onExit by `remoteorigin::terminalExit(origin, wifiStarted, radioOn)` (src/activities/remote/RemoteOrigin.h, tested in test/network_mode_rows): `RemoteOrigin::Home` (passed by the picker through RemoteServerListActivity and RemoteServerActivity) restarts to Home, `RemoteOrigin::Settings` (Settings > Connectivity, the default) to Settings, and no restart when the radio is off (WiFi.getMode() is WIFI_MODE_NULL). Settings > Connectivity keeps both rows. The web server screen's header uses the new key STR_WIFI_TRANSFER ("File Transfer"). New keys in english.yaml and greek.yaml only; the other languages fall back to English for them.
- SSH terminal font (2026-11-14, computer tests only; not rebuilt with PlatformIO, not seen on a device). Cause of the bad text on the X4 Pro: `SshTerminalActivity::drawGrid` drew each cell with `drawScaledCodepoint` from SMALL_FONT_ID (Ubuntu 8 pt, proportional) into 8-pixel cells; a glyph whose advance was over 8 pixels was scaled by `cellW * 256 / advance` in both directions (GfxRenderer.cpp, `dstW` and `dstH` from one `scale256`) and resampled with a threshold, so wide letters came out lower and deformed, each glyph was centred by its own advance, bold was a second copy one pixel to the right, and a character the font lacks got `EpdFont::getGlyph`'s U+FFFD glyph (the diamonds; the built-in font has no U+2500 block). Now `lib/Ssh/SshTermFont.h/.cpp` (`sshfont`, no platform code): `Font {cellW, cellH, rowBytes, rows}`; `fontForHeight(12|14|16|18)` = 6 x 12, 8 x 14, 8 x 16, 10 x 18 (any other value: 16); `nextHeight`; `glyphIndex` (binary search in one sorted table of 1051 BMP code points shared by the four sizes); `fallbackFor` (a table of stand-ins that are all in the font: dashed box lines U+2504-2507 and U+254C-254F as the plain ones, U+2594/2595 as half blocks, small triangles and heavy prompt arrows as `>` `<` `^` `v` or the plain arrow; then `+` for U+2500-257F, `#` for U+2580-259F, a blank for control characters, `?` for the rest); `resolveGlyph`; `rowBits` (the first column in bit 15, masked to the cell); `boldRowBits` (the row OR itself shifted right by one, cut at the cell, the last column left clear when the glyph has ink in column 0) and `boldAllowed` (cells of 8 pixels or more, never U+2500-259F); `drawCell(font, cp, bold, plot)` calls `plot(dx, dy)` inside the cell only; `gridFor(width, height, font, maxCols, maxRows)` = whole cells, 20 x 4 at least, `left` = half the unused width. `lib/Ssh/SshTermFontData.cpp` is written by `scripts/gen_ssh_font.py` from Terminus Font 4.49.1 `ter-u12n/14n/16n/18n.bdf` (OFL-1.1, `lib/Ssh/fonts/OFL.TXT` and README.md with the download checksum; NUL, combining marks, Hebrew, Braille and private-use glyphs left out): 84,080 bytes of tables, referenced only from the SSH terminal; excluded from clang-format. The activity: `SETTINGS.sshTextSize` (uint8, the cell height, default 16; `SettingInfo::Value` {12, 18, 2}, key `sshTextSize`, no category) read in `onEnter`; `gridTop` from the UI font's line height (the header line stays in SMALL_FONT_ID); `drawGrid` fills an inverse cell black and plots the glyph white, underline on the cell's last pixel row, a 2-pixel cursor bar, and skips any cell that would pass the screen's right edge. `RemoteServerListActivity` (SSH list): a row **Terminal text size** with the value `W x H, N columns` (N from `gridFor` and the screen width), a tap sets `nextHeight` and saves. Strings `STR_SSH_TEXT_SIZE`, `STR_SSH_COLUMNS` (English, Greek). docs/third-party.json: Terminus Font, `only_with` VAULTINK_SSH. Tests: test/ssh/SshTermFontTest.cpp, 13 (sizes, table order, coverage of ASCII, Latin-1, Greek, Cyrillic, U+2500-259F, every stand-in present, the 8 x 16 `m` pixel for pixel, one x-height and baseline for the lower-case letters in each size, every glyph inside its cell regular and bold, box lines reaching the cell edges, block elements by ink count, bold, the grid on 480, 479, 485 and 800 pixels with the 132-column and 64-row caps, a long URL and a 60-cell box line wrapping at the grid's columns); SshTest is 73 tests. Out: changing the size during a session (`resizeShell` exists), a second font family, a bold weight of its own, two-cell wide characters, combining marks, a font from the SD card.
- SSH terminal font, review fixes (2026-11-14, computer tests only): `sshfont::snapHeight` (constexpr: outside 12 to 18 gives 16, an odd value between gives the next size up) used by `fontForHeight`, by `nextHeight` (counts from the snapped value, so a tap always changes the size), by `CrossPointSettings::loadFromFile` after the generic range clamp (a changed value is saved again) and by the web settings POST for the key `sshTextSize`; the setting is `withWebHidden()` in builds without the terminal. `sshterm::Terminal::isZeroWidth` (U+0300-036F, 1AB0-1AFF, 1DC0-1DFF, 200B-200F, 2028-202E, 2060-2064, 20D0-20FF, FE00-FE0F, FE20-FE2F, FEFF): `putChar` drops these before anything else, so they take no cell. The stand-in entry for U+FEFF is gone (it never reached a cell). PSRAM for the grid: about 180 KB at 80 columns, at most about 280 KB at the 132-column cap. test/ssh: SshTest is 74 tests.
- SSH terminal, zero-width characters, second review (2026-11-14, computer tests only): `isZeroWidth` no longer holds U+2028 and U+2029 (`putChar` turns each into column 0 plus `lineFeed`, like CR LF) and gains U+0483-0489, U+061C, U+2066-2069, U+1F3FB-1F3FF, U+E0020-E007F and U+E0100-E01EF; both checks run on the whole code point before one above U+FFFF becomes `?`. test/ssh: SshTest is 76 tests (both edges and both neighbours of every range, the ranges above the BMP fed as UTF-8, the two separators including at the bottom row and after a full row).
