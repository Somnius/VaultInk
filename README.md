<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/vaultink/logo-dark.svg">
    <img src="docs/images/vaultink/logo-light.svg" width="96" alt="VaultInk logo">
  </picture>
</p>

<h1 align="center">VaultInk</h1>

<p align="center">Books and Obsidian vaults on the Xteink X4 Pro.</p>

<p align="center">
  <a href="https://github.com/Somnius/VaultInk/releases"><img src="https://img.shields.io/github/v/release/Somnius/VaultInk?label=version&sort=semver&cacheSeconds=300" alt="Version"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="MIT licence"></a>
  <img src="https://img.shields.io/badge/device-Xteink%20X4%20Pro-555" alt="Xteink X4 Pro">
  <img src="https://img.shields.io/badge/ESP32--S3-E7352C?logo=espressif&logoColor=white" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/C%2B%2B20-00599C?logo=cplusplus&logoColor=white" alt="C++20">
  <img src="https://img.shields.io/badge/PlatformIO-F5822A?logo=platformio&logoColor=white" alt="PlatformIO">
  <img src="https://img.shields.io/badge/Arduino%20%2B%20FreeRTOS-00878F?logo=arduino&logoColor=white" alt="Arduino and FreeRTOS">
  <img src="https://img.shields.io/badge/Obsidian-7C3AED?logo=obsidian&logoColor=white" alt="Obsidian">
  <img src="https://img.shields.io/badge/UI-English%20%C2%B7%20%CE%95%CE%BB%CE%BB%CE%B7%CE%BD%CE%B9%CE%BA%CE%AC-0A7BBB" alt="English and Greek">
</p>

<p align="center">
  <a href="https://somnius.github.io/VaultInk/"><b>Install from your browser</b></a> ·
  <a href="https://lefteros.com/en/apps/vaultink">Project page and donations</a> ·
  <a href="https://github.com/Somnius/VaultInk/releases">Releases</a>
</p>

VaultInk is firmware for the Xteink X4 Pro e-reader. It keeps everything
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) does for books, and adds
a way to carry your [Obsidian](https://obsidian.md) notes with you. You can read them, search them
and follow their links. Later on you'll be able to edit and sync them too.

It's free and open source (MIT). It is not affiliated with Obsidian, Xteink or CrossPoint.

> [!WARNING]
> **Early days.** Version 0.19.0 opens your vaults, reads their notes, links and images, Greek
> included, and searches them. Sync isn't in yet. Every release from 0.8.2 on has been tested by kos25k
> on his X4 Pro (UC8279 screen); the bugs he found are fixed and listed in the release notes. It's still
> one tester on one device, so back up your SD card. The screenshots below come from the simulator.

## Screenshots

### Vaults, notes and search

<p>
  <img src="docs/images/vaultink/vaults.png" width="190" alt="Vault picker">
  <img src="docs/images/vaultink/note.png" width="190" alt="A note with links, a callout, tasks and a table">
  <img src="docs/images/vaultink/search.png" width="190" alt="Search results">
</p>

The vault picker finds every vault on the SD card. A note reads like a book page, with links,
callouts, tasks and tables. Search looks through every vault at once. These show a small demo
vault.

### Greek and English keyboard

<p>
  <img src="docs/images/vaultink/keyboard-el.png" width="190" alt="Greek keyboard">
  <img src="docs/images/vaultink/keyboard-el-upper.png" width="190" alt="Greek keyboard with Shift">
  <img src="docs/images/vaultink/keyboard-en.png" width="190" alt="English keyboard">
</p>

The on-screen keyboard types Greek and English. The globe key switches between them. Hold a vowel
for its accent (ά έ ή ί ό ύ ώ); ϊ and ϋ have their own keys, and holding them gives ΐ and ΰ. With
the Greek interface the keyboard opens in Greek.

### Page backgrounds

<p><img src="docs/images/vaultink/paper.gif" width="240" alt="Swiping down quick settings and tapping the paper tile to change the page background"></p>

Swipe down from the top for quick settings, then tap the paper tile to change the page background
under the text. It's also under **Settings → Reader → Page Background**. The choices are plain,
paper, sandpaper, papyrus (soft, medium and hard), toilet paper, kitchen paper and bath towel.

While a background is on, text anti-aliasing is switched off, so the letters stay sharp on the
real screen.

This one is mostly for fun 😄. Plain is the default and the easiest to read, but the papyrus ones
are actually nice for a long read. The idea came from kos25k.

### Home, reading and settings

<p>
  <img src="docs/images/vaultink/home.png" width="190" alt="Home screen">
  <img src="docs/images/vaultink/home-el.png" width="190" alt="Home screen in Greek">
  <img src="docs/images/vaultink/quick.png" width="190" alt="Quick settings over a book">
  <img src="docs/images/vaultink/toolbar.png" width="190" alt="Reading toolbar">
  <img src="docs/images/vaultink/settings.png" width="190" alt="Settings">
  <img src="docs/images/vaultink/about.png" width="190" alt="About">
</p>

From left to right: the home screen, the same in Greek, quick settings pulled down over a book, the
reading toolbar, settings, and About.

## Getting started

The first time VaultInk starts, it shows a short welcome guide. The five pages are below.

<p>
  <img src="docs/images/vaultink/guide-1.png" width="150" alt="Welcome page">
  <img src="docs/images/vaultink/guide-2.png" width="150" alt="Reading page">
  <img src="docs/images/vaultink/guide-3.png" width="150" alt="Quick settings page">
  <img src="docs/images/vaultink/guide-4.png" width="150" alt="Adding books page">
  <img src="docs/images/vaultink/guide-5.png" width="150" alt="Last page">
</p>

1. **Welcome.** VaultInk is for your books and your Obsidian vaults. The menus are in English, Greek and many other languages, listed on this page.
2. **Reading.** Swipe left for the next page and right for the previous one, or use the side buttons. Tap the middle of the page to open the reading menu.
3. **Quick settings.** Swipe down from the top of the screen, or tap the battery. You get the light, night mode, sleep, and a full refresh (to clear ghost images).
4. **Adding books.** Copy books into the `books` folder on the SD card, or send them from your computer with File Transfer (Wi-Fi, USB or Calibre).
5. **That's it.** Open the guide again from **Settings → System → Welcome Guide**. It also comes back once after an update that changes it. To stop that, turn off **Show Welcome Guide After Updates** in the same place.

**Menus in Greek:** Settings → System → Language, then **Ελληνικά** (second in the list, right
after English). Back in Greek the same place is Ρυθμίσεις → Σύστημα → Γλώσσα.

In the guide, Next (or OK, or the right and down keys) moves on. Back goes back a page. On the first page, Back asks whether you want to skip.

## What works now

- **Reading.** Everything CrossPoint Reader offers: EPUB, TXT and XTC books, SD-card fonts,
  hyphenation, bookmarks, highlights, dictionaries, KOReader sync, OPDS catalogs, Calibre wireless
  transfer and SD-card plugins.
- **Obsidian notes.** Open a `.md` note from a vault and it reads like a book: headings, bold and
  italic, lists (nested and numbered) and tasks, quotes, tables, code, footnotes, and embedded
  images found anywhere in the vault.
  - **Callouts** (`> [!quote] Title`) are a framed box with the title in bold.
  - **Highlights** (`==text==`) are a light gray band behind the words. Settings → Vaults →
    Highlight style also offers Inverted (white on black) and Dark gray (white on dark gray).
  - **Embeds:** `![[Note]]` shows the other note inside this one, `![[Note#Heading]]` just that
    heading's section, and `![[Note#^id]]` just that paragraph or list item.
  - **Links:** tap a `[[link]]` to open the other note (or jump to its heading), and press Back to
    return. Links are found the way Obsidian finds them: by name, by a path from the vault root, or
    by the end of a path (`[[People/Anna]]` finds `Notes/People/Anna.md`), and names with dots
    (`[[Dr. Smith]]`, `[[Section 2.1]]`) work too.
- **Vaults and search.** The Vaults row lists every vault on the SD card (folders under `Vaults/`
  and any folder with `.obsidian` in it), and the home screen shows how many there are. Each vault
  remembers the note you read last and opens on it (Settings → Vaults → Open last note). Search
  looks through all of them by title, alias, tag and text. Accents don't matter, so `καλημερα` finds `Καλημέρα`. In a note, **More → Backlinks** lists
  the notes that link to it. The search index lives outside the vault (`/.vaultink/` on the SD
  card) and also keeps a list of the vault's files, so links and images open without scanning the
  card; Settings → Vaults → Rebuild Search Index starts it over.
- **LiveSync (read only).** Pull a vault from Obsidian Self-hosted LiveSync over Wi-Fi: paste the
  Setup URI on the device's web page, type its passphrase on the device, then Settings → Vaults →
  LiveSync → Sync now. End-to-end encryption (V2) and path obfuscation are supported; edits,
  deletes and renames come across, deleted files go to a trash folder. See
  [docs/livesync.md](docs/livesync.md).
- **Import a vault from a ZIP** on the device's web page (File Transfer, then Files →
  **Import Vault (.zip)**): it lands in `/Vaults/<name>/` ready to open. A
  [demo vault](demo-vault/VaultInk%20Demo) with notes, pictures and a canvas is in this repo
  and attached to the releases as `VaultInk-Demo.zip`.
- **Canvases.** `.canvas` files open in a viewer: the whole board fitted to the screen, cards,
  groups, labelled arrows, pictures in image cards, and coloured cards shaded gray. Pan with the
  buttons or swipes, zoom with the + / − / Fit buttons at the bottom right (or Confirm), and tap a
  card to open its note (or the card's own text). Back returns to the canvas.
- **Greek and English keyboard** on screen, switched with the globe key.
- **Page backgrounds**, from plain to papyrus to bath towel, [mostly for fun](#page-backgrounds).
- **A new home screen.** The book you're reading sits at the top, followed by Books and Vaults, then
  Files, Transfer, Plugins and Settings. Every row says in one line what's behind it. You can use
  touch or the buttons. CrossPoint's own home styles are still there under Settings → Display.
- **Quick settings.** Swipe down from the top, or tap the status bar, for brightness, warmth, night
  mode, refresh, orientation, sleep, file transfer and home. Inside a book the panel also shows the
  title and how far you've read.
- **Settings by task.** Settings are grouped under Reading and Device. Each group says what's in it.
- **Greek.** A full Greek interface, monotonic and polytonic Greek in the interface fonts, and Greek
  hyphenation for both. The built-in reading font has Greek too, polytonic included, so Greek books
  and notes read out of the box. SD-card fonts with Greek (Literata, Noto Serif, Gentium Book Plus)
  work as well.
- **A welcome guide** on the first start, described [above](#getting-started).
- **Updates.** The device checks this repository's releases for new versions.

## What's coming

The [roadmap](ROADMAP.md) has the details and the order. In short:

1. **The vault reader.** Open a vault from the SD card, browse folders, read notes with their links,
   embeds, callouts, tasks and tables, and search, with Greek accents taken into account.
2. **Canvas files**, read only (the first version is in: viewing, pan, zoom, open a card).
3. **Obsidian LiveSync**, so a vault stays in step with your other devices (pull is in; push comes
   with editing).
4. **Editing notes** on the device, then a **Bluetooth keyboard**.
5. **An Android companion app** (in `companion/` in this repo, released separately) for typing
   into the device, quick notes and bringing in notes from other apps.

The full feature list is in [SPECS.md](SPECS.md), and what is in and out of scope is in
[SCOPE.md](SCOPE.md).

## Installing

The easiest way is the **web installer** at **https://somnius.github.io/VaultInk/**. Pick your
device there and follow its steps. For the X4 Pro, in short:

1. Back up your SD card.
2. Copy `vaultink-<version>-x4pro.bin` from the [latest release](https://github.com/Somnius/VaultInk/releases/latest)
   (not the `factory` file) to the SD card, so you can reinstall from the reader under
   **Settings → System → SD Card Firmware Update**.
3. Put the reader in boot mode: switched off, hold **Power**, then also hold both page-turn
   buttons.
4. Connect it over USB-C to a computer with Chrome or Edge and press Install. Erasing the device
   wipes the reader's own memory (settings, Wi-Fi networks), not the SD card.

After that, updates come over Wi-Fi: **Settings → System → Check for Updates** downloads and
installs the newest release.

You can also build it yourself (see below) and flash it over USB with
[`esptool`](https://github.com/espressif/esptool):

```sh
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x0 .pio/build/x4pro/firmware.factory.bin
```

## Tech stack

| | |
| --- | --- |
| Device | Xteink X4 Pro: ESP32-S3, 480×800 e-ink touch screen, SD card |
| Firmware | C++20 on Arduino and FreeRTOS (ESP-IDF underneath), built with PlatformIO |
| Base | [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) and the [FreeInk SDK](https://freeink.org) (drivers, UI, keyboard) |
| Notes | A streaming Markdown to XHTML converter, so notes render through the book reader |
| Search | A per-vault index file on the SD card, updated incrementally, with Greek and accent folding |
| Desktop | An SDL2 simulator that runs the same code, and host tests with CMake and GoogleTest |
| Installer | ESP Web Tools on GitHub Pages; the firmware is built locally, not on CI |

## Building

You need [PlatformIO](https://platformio.org) and a copy of this repository with its submodules:

```sh
git clone --recursive https://github.com/Somnius/VaultInk.git
cd VaultInk
pio run -e x4pro            # firmware for the X4 Pro
pio run -e simulator_x4_pro # desktop simulator (Linux needs SDL2 and OpenSSL headers)
```

The simulator runs the same code in a window on your computer. It reads its "SD card" from a folder
called `fs_` in the directory you start it from:

```sh
mkdir -p fs_/books && cp some-book.epub fs_/books/
.pio/build/simulator_x4_pro/program
```

Arrow keys are the buttons, Enter is OK, Escape is Back, and the mouse works as touch.

## Translating

The menus are in English and Greek, and many other languages from CrossPoint Reader are partly
there. To ask for your language or help translate it, see [docs/translating.md](docs/translating.md),
or open a [translation request](https://github.com/Somnius/VaultInk/issues/new?template=translation_request.yml).

## Author and donations

VaultInk is made by **Lefteris Iliadis** ([lefteros.com](https://lefteros.com)). The app page,
[lefteros.com/en/apps/vaultink](https://lefteros.com/en/apps/vaultink), has more about it, and you
can donate there to support the project.

## Credits

VaultInk stands on other people's work:

- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), which everything
  here is built on, and the [FreeInk SDK](https://freeink.org) under it.
- [CrossNext](https://github.com/manuelx900/CrossNext) and KOReader's SimpleUI plugin, for ideas.
- kos25k, who tested every step of the way.

The licences of the fonts, icons and libraries are listed in [NOTICE](NOTICE).

## License

MIT. See [LICENSE](LICENSE).
