<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/vaultink/logo-dark.svg">
    <img src="docs/images/vaultink/logo-light.svg" width="96" alt="VaultInk logo">
  </picture>
</p>

<h1 align="center">VaultInk</h1>

<p align="center">Books and Obsidian vaults on the Xteink X4 Pro.</p>

<p align="center">
  <a href="https://somnius.github.io/VaultInk/"><b>Install from your browser</b></a> ·
  <a href="https://lefteros.com/apps/vaultink">Project page</a> ·
  <a href="https://github.com/Somnius/VaultInk/releases">Releases</a>
</p>

VaultInk is firmware for the Xteink X4 Pro e-reader. It keeps everything
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) does for books, and adds
a way to carry your [Obsidian](https://obsidian.md) notes with you. You can read them, search them
and follow their links. Later on you'll be able to edit and sync them too.

It's free and open source (MIT). It is not affiliated with Obsidian, Xteink or CrossPoint.

> [!WARNING]
> **Early days.** Version 0.4.0 opens your vaults, reads their notes, links and images, Greek
> included, and searches them. Sync isn't in yet. Nothing has been tested on real hardware so far. All the screenshots below
> come from the simulator. Please don't flash this onto your daily reader yet.

## Screenshots

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

1. **Welcome.** VaultInk is for your books now, and for your Obsidian vaults soon. The menus are in English, Greek and many other languages, listed on this page.
2. **Reading.** Swipe left for the next page and right for the previous one, or use the side buttons. Tap the middle of the page to open the reading menu.
3. **Quick settings.** Swipe down from the top of the screen, or tap the battery. You get the light, night mode, sleep, and a full refresh (to clear ghost images).
4. **Adding books.** Copy books into the `books` folder on the SD card, or send them from your computer with File Transfer (Wi-Fi, USB or Calibre).
5. **That's it.** Open the guide again from **Settings → System → Welcome Guide**. It also comes back once after an update that changes it. To stop that, turn off **Show Welcome Guide After Updates** in the same place.

In the guide, Next (or OK, or the right and down keys) moves on. Back goes back a page. On the first page, Back asks whether you want to skip.

## What works now

- **Reading.** Everything CrossPoint Reader offers: EPUB, TXT and XTC books, SD-card fonts,
  hyphenation, bookmarks, highlights, dictionaries, KOReader sync, OPDS catalogs, Calibre wireless
  transfer and SD-card plugins.
- **Obsidian notes.** Open a `.md` note from a vault and it reads like a book: headings, bold and
  italic, highlights, lists and tasks, quotes and callouts, tables, code, and embedded images found
  anywhere in the vault. Tap a `[[link]]` to open the other note (or jump to its heading), and press
  Back to return. A vault picker, search and the Vaults row on the home screen are next.
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
2. **Canvas files**, read only.
3. **Obsidian LiveSync**, so a vault stays in step with your other devices.
4. **Editing notes** on the device, then a **Bluetooth keyboard**.
5. **An Android companion app** for typing into the device, quick notes and bringing in notes from
   other apps.

The full feature list is in [SPECS.md](SPECS.md), and what is in and out of scope is in
[SCOPE.md](SCOPE.md).

## Installing

The easiest way is the **web installer** at **https://somnius.github.io/VaultInk/**. Open it in
Chrome or Edge on a computer, connect the X4 Pro over USB, and press Install. It writes the whole
image, so it also works the first time you move from the stock firmware.

Once VaultInk is on the device, it finds new versions itself under
**Settings → System → Check for Updates**. Releases also appear under
[Releases](https://github.com/Somnius/VaultInk/releases).

You can also build it yourself (see below) and flash it over USB with
[`esptool`](https://github.com/espressif/esptool):

```sh
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash 0x0 .pio/build/x4pro/firmware.factory.bin
```

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

## Credits

VaultInk stands on other people's work:

- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), which everything
  here is built on, and the [FreeInk SDK](https://freeink.org) under it.
- [CrossNext](https://github.com/manuelx900/CrossNext) and KOReader's SimpleUI plugin, for ideas.
- kos25k, who tested every step of the way.

The licences of the fonts, icons and libraries are listed in [NOTICE](NOTICE).

## License

MIT. See [LICENSE](LICENSE).
