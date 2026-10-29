# VaultInk scope

VaultInk is firmware for the Xteink X4 Pro: a book reader and a portable device for Obsidian vaults.
It is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), whose
scope is narrower; this file replaces it for VaultInk. Features are specified in [SPECS.md](SPECS.md)
and ordered in [ROADMAP.md](ROADMAP.md).

## 1. Mission

Read, search, write and sync an Obsidian vault on e-ink, without losing the book reader.
Keep it lightweight: it must stay fast and stable on the X4 Pro (ESP32-S3, 8 MB PSRAM).

## 2. In scope

* **Books:** everything CrossPoint does today (EPUB, TXT, XTC, fonts, hyphenation, bookmarks, OPDS).
* **Comics and manga** (Phase 1c): XTC/XTCH/EPUB made with an external converter, later CBZ. No
  conversion on the device.
* **Vault reading:** the Obsidian Markdown features listed in SPECS.md 5.1, links, embeds, frontmatter,
  tags, backlinks, search.
* **Canvas:** read-only JSON Canvas with pan and zoom.
* **Sync:** Self-hosted LiveSync over CouchDB, pull first, then push.
* **Writing:** a note editor with touch keyboard, Bluetooth keyboard and the Android companion.
* **Languages:** English and Greek for the UI and content.
* **Attachments:** PNG, JPEG and GIF images now (WebP needs a decoder); PDF after a feasibility trial; audio and Excalidraw later.
* **Code quality:** refactors that make the above easier to maintain or cheaper in memory.

## 3. Out of scope

* Devices other than the X4 Pro (for now).
* Other sync services (Git, Syncthing, Dropbox, iCloud, Obsidian Sync) and LiveSync's object storage
  and P2P modes.
* Running Obsidian plugins (Dataview, Bases, Templater) or showing their output.
* Graph view, games, browsers, RSS.
* Licences, accounts, payments, telemetry. VaultInk is fully free.
* iOS companion.

## 4. Rules for new work

* It must serve a requirement in SPECS.md, or SPECS.md is updated first.
* Vault code lives in `lib/Vault` with host tests; screens stay thin.
* Stream from SD; no unbounded allocations; every cap has a failure path.
* Real vault contents and secrets never go into the repo, tests, logs or external tools.
* Keep changes to CrossPoint code small, so upstream fixes can still be merged.
