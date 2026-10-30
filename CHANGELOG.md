# VaultInk changelog

Every release, newest first. The device shows the new entries once after an update.

## 0.43.0 (2026-10-30)

Manga read right to left, and a new website.

- CBZ manga: a CBZ that says it's right-to-left in its ComicInfo.xml opens that way, so the usual "next" swipe and button go to the previous page and the other way round. Confirm or the menu tap in the middle of the page shows **Right to left (manga): On/Off**, to change it for that book; the choice is kept.
- The install page at somnius.github.io/VaultInk has a new design: clearer install steps, screenshots of what VaultInk does, and the comics and companion app parts.

Tested in the simulator: a manga CBZ opened right to left from its ComicInfo.xml, the swipes went the other way, and turning it off made them normal again; host tests for reading the ComicInfo flag. Not confirmed on the device yet.


## 0.42.0 (2026-10-30)

The home button works on the first tap, and the companion app is one scan away.

- The house at the top (kos25k #17) is easier to hit: its tap area is wider and reaches the very top edge, and a tap that slips a little no longer opens quick settings instead. A real pull-down still opens quick settings.
- After What's New, the reader shows a QR code for the Android companion app (once per update). Scan it with your phone to get the app.

Tested in the simulator: taps at the top edge, beside the icon and a short slip all went home, a long pull-down opened quick settings; the QR decoded to the companion page and showed only once. Not confirmed on the device yet.


## 0.41.2 (2026-10-30)

- In the Gallery, tap a picture and choose **Set as sleep picture** (kos25k #21). Before, it was only on the Confirm button while viewing the picture, and the touch screen didn't show that.

Tested in the simulator: a PNG set from the Gallery menu became the sleep picture. Not confirmed on the device yet.


## 0.41.1 (2026-10-30)

- No change you can see: a code check fix for 0.41.0's PNG sleep picture.

Tested with the code checks and in the simulator.


## 0.41.0 (2026-10-30)

A home button at the top, and PNG sleep pictures (kos25k #17, #21).

- A small house in the middle of the top bar, between the time and the battery, goes to the home screen with one tap. It's on every screen with that bar, the quick settings panel too, but not on book or note pages, where the top is text. Off in Settings → Display → Home Button at the Top.
- PNG pictures can now be the sleep picture like JPEG: open one in the Gallery or Browse Files, Set as sleep cover, and Settings → Display → Sleep Screen Cover Mode crops it to fill the screen or fits it. The battery percent stays in its corner.

Tested in the simulator: the house in the Library and on the quick settings panel over a book, and a tap on it going home from both; a PNG set as the sleep picture in Fit and in Crop, and the sleep screen showing it with the battery percent. Not confirmed on the device yet.


## 0.40.0 (2026-10-30)

- CBZ covers and progress: the first page is the cover on the home's Continue card, with the progress bar.
- CBZ chapters: in a CBZ with folders, Confirm (or the menu tap in the middle of the page) opens the list of chapters, one per folder; pick one to jump to its first page.
- The Library shows notes and CBZ comics by name without ".md" or ".cbz" on the Title and Author tabs, like books.

Tested in the simulator: a CBZ with two chapter folders opened its list, and picking the second chapter went to its first page; the cover and progress on the home card; the Library titles without extensions. Not confirmed on the device yet.


## 0.39.0 (2026-10-30)

CBZ comics open.

- A CBZ file copied onto the card opens like a book, from Browse Files or the Library: each page fitted to the screen in grayscale, swipe or the buttons to turn, and it remembers the page. JPEG and PNG pages, in natural order (2 before 10); macOS leftovers and other files inside are skipped.
- Not yet: pages over 8 megapixels (2000×3000 is fine) show "Page load error", no WebP or CBR, no chapters or cover yet. For the best result on the X4 Pro, converting with Banana Comic Converter to XTCH is still better.
- The companion app can share CBZ files to the reader's Comics folder too (in its next version).

Tested in the simulator and with host tests: a three-page CBZ in the right order with its page remembered, a CBZ with folders, and the 8 MP limit with a big JPEG and PNG. Not confirmed on the device yet.


## 0.38.0 (2026-10-30)

Renaming a note keeps the links to it working.

- When you rename a note that other notes link to, VaultInk asks **Update links?** and says how many links in how many notes. **Update** changes `[[Old]]`, `[[Old|text]]`, `[[Old#Heading]]`, `![[Old]]` and paths like `[[People/Old]]` to the new name, only where they really lead to that note. **Leave them** keeps them as they are.
- The Backlinks list now also finds links that give only the end of a path, like `[[People/Nikos]]` for `Notes/People/Nikos.md`, the way Obsidian matches them.
- Not changed: links inside code, Markdown links like `[text](Old.md)`, canvases, notes over 256 KB, and links in the renamed note to itself.

Tested in the simulator on the demo vault: renaming Nikos updated the embed in Features and the path link in Welcome (one line each, the rest of both notes byte for byte the same), and left the link written inside code alone; host tests for the link rewriting. Not confirmed on the device yet.


## 0.37.0 (2026-10-30)

Recent notes.

- The Vaults screen lists your **Recent notes** under the vaults: the last five notes you read, from any vault, with the vault's name. Tap one to open it.

Tested in the simulator: the list shows the demo vault's last notes (What's New and other notes outside a vault are left out), and a tap opens the note. Not confirmed on the device yet.


## 0.36.2 (2026-10-30)

A fix for notes that embed other notes.

- A note that shows another note inside it (`![[Note]]`, or a heading or block of it) now updates when that other note changes. Before, it kept showing the old text until the note itself changed.

Tested in the simulator: the demo vault's Features note embeds Nikos; after Nikos changed, Features was rebuilt with the new text, and opening it again without changes didn't rebuild it. Not confirmed on the device yet.


## 0.36.1 (2026-10-30)

Faster comic pages.

- XTCH comics made for the X4 / X4 Pro (480×800, like Banana Comic Converter's X4 preset) are drawn much faster: the page is copied into the screen buffer a byte at a time instead of one pixel at a time, four times over. Other page sizes are drawn as before.

Tested in the simulator and with host tests: the pages come out exactly the same as before, pixel for pixel. How much faster it is on the device hasn't been measured yet.


## 0.36.0 (2026-10-30)

The Library groups authors (kos25k #18).

- **Settings → Library → Authors as folders**: the Author tab opens with one folder per author who has two or more books, showing how many; authors with one book show as the book. Tap a folder for that author's books, Back to go back. "Jane Austen" and "jane austen" are the same author.
- A long press on any book in the Library now opens the book menu on every tab (Open, Delete, Refresh, Favourites, Info). On Title and Author it also has **Group by letter** / **Group by author**, which used to be a hidden long press.

Tested in the simulator: the folders with Books only on and off, a folder and Back, a one-book author opening its book, and the new menu on the Author tab. A search on the Author tab with folders on wasn't tried, and nothing is confirmed on the device yet.


## 0.35.3 (2026-10-29)

- If you use one of the CrossPoint home screens, What's New now says so: VaultInk works best with its own home screen. New features arrive there first, and some are only there. Switch in Settings → Display → UI Theme → VaultInk.

Tested in the simulator on Lyra 3 Covers: the tip shows at the top of What's New; on the VaultInk home it doesn't. Not confirmed on the device yet.


## 0.35.2 (2026-10-29)

Book info on the home's recent books too (kos25k #15).

- On the home styles with a row of recent covers (like Lyra 3 Covers), long-press a cover, or hold Confirm on a selected one, for its Info. On the Cover grid home, hold Confirm on it. Only the VaultInk home's Continue card had it before.

Tested in the simulator on Lyra 3 Covers: a long press on the second cover shows that book's info, and a slow press on a menu row still opens it. The button hold (and the Cover grid home) weren't tried. Not confirmed on the device yet.


## 0.35.1 (2026-10-29)

A fix: books keep your place when their folder moves.

- Renaming or moving a folder (in Browse Files, or over WebDAV) now keeps the reading position, bookmarks, highlights, Recent and Favourites of every book inside. Before, those books started again from page 1.
- Renaming or moving a single book from the web page, WebDAV or the companion app keeps its place too. Before, only Browse Files did.

Tested in the simulator: a folder moved in Browse Files and over WebDAV, and a book renamed on the web page, each kept its progress, cover and Recent entry. Not confirmed on the device yet.


## 0.35.0 (2026-10-29)

Book info and a books-only Library (kos25k #15).

- Long-press a book for **Info**: title, author, file name, size, how far you've read and whether it's a favourite. In Browse Files, in every Library tab, and on the home's Continue card.
- The Library has an **All | Books only** switch under its tabs. Books only hides the vault notes from Recent, Title and Author (search too). It's remembered.

Tested in the simulator: Info from Browse Files, the Library menus and the Continue card; the switch on Recent and Title, remembered after reopening, and the long-press menu on the filtered list. A search with Books only on wasn't tried yet, and nothing here is confirmed on the device yet.


## 0.34.0 (2026-10-29)

Comics: convert them from the X4 Pro.

- Settings → Library → Convert comics shows a QR code for Banana Comic Converter by u/curseofthebanana. Scan it with your phone, the converter opens there, and the screen reminds you what to pick: the X4 / X4 Pro preset and XTCH. Then upload the file with the companion app or File Transfer, and it opens like a book.
- More in the comics guide: https://github.com/Somnius/VaultInk/blob/main/docs/comics.md

Tested in the simulator, in English and Greek: the screen and its text, and the QR code decoded from a screenshot opens the converter's page. Not confirmed on the device yet.


## 0.33.2 (2026-10-29)

A fix for comics and other XTC/XTCH files with chapters.

- Chapters now open on the right page in files made by Banana Comic Converter and other tools that follow the XTC format description. VaultInk (like CrossPoint, where this code comes from) read the chapter pages one off, so every chapter after the first started on the last page of the chapter before. Files from tools that number the pages the other way still open right: VaultInk now tells the two apart.
- A file whose first chapter has no name and is only one page long also keeps its other chapters now.

Tested in the simulator with test files of both kinds (chapters starting at page 0 and at page 1) and the unnamed first chapter: each chapter opens on its own first page. Not confirmed on the device yet.


## 0.33.1 (2026-10-29)

A fix for kos25k's #15.

- In a vault note, a swipe from the left edge to the right now goes back, the same as the arrow at the top: to the note you came from through a link, otherwise to the note's folder. Before, in a note opened from a folder that swipe was taken as previous page, so a one-page note seemed stuck. In notes, the previous page is the left tap zone. In books nothing changes: the swipe still turns to the previous page.
- Also a small code clean-up the build checks asked for. Nothing you'd notice.

Tested in the simulator: the swipe in a note opened from its folder (back to the folder), and in a book on page 2 (back to page 1, the book stays open). Not confirmed on the device yet.


## 0.33.0 (2026-10-29)

Settings search, from kos25k's #13.

- Settings has a Search settings row at the top. Type part of a setting's name and press OK; capitals and Greek accents don't matter. Each result says which section it's in, and a tap opens that section with the setting selected.
- Settings inside Text Settings (line spacing, fonts and the rest) open the Reader section on its Text Settings row, one tap away.
- The last row searches your notes for the same words. Back comes back to the results.

From #13 I left out searching every file on the card: there's no index of them and reading the whole card for each search would be very slow. The section name stands in for the category icons, and the X4 Pro has no storage besides the card.

Tested in the simulator: searching sleep, spacing and tomato (settings, Text Settings rows, and notes), opening a result further down a section, and the notes row coming back. Greek accent folding is the vault search's own; I didn't try a Greek query in the simulator. Not confirmed on the device yet.


## 0.32.0 (2026-10-29)

The home rows can go in any order now, the second part of kos25k's #14.

- Settings → Display → Home Screen Rows: tap a row to pick it up, then tap where it should go. Reset to default order is at the end. The Continue card stays on top. This is for the VaultInk home style.
- Fixed in 0.31.0's code: a check the build tools flagged (it could never be true). No change you'd notice.
- Behind the scenes, the Greek texts had filled their table, so the table can hold twice as much now. Every language reads its texts the same as before.

Tested in the simulator: moving Settings to the top in Greek, the home showing the new order, the saved order surviving a restart, and the order code with host tests (unknown or repeated rows, and rows added later). Not confirmed on the device yet.


## 0.31.0 (2026-10-29)

Favourites, from kos25k's #14.

- The Library has a fourth tab, Favourites, next to Author. It lists the books you marked, newest first.
- To add a book: inside it, tap the middle of the screen, More, then Add to Favourites. Or in the Library's Recent tab, long-press a book: the last entry adds or removes it.
- In the Favourites tab, tap to open, long-press or swipe left to take it off the list. The book itself stays on the card.
- Renaming or moving a book (file browser, reading menu or web page) keeps it in Favourites. If its file is gone some other way, opening it says so and offers to remove it.
- The four Library tabs are as wide as their names now, so all four fit, in Greek too.

From #14 I didn't do the 3x2 home grid or the 1 to 6 covers: those rework the old CrossPoint home styles. Reordering the home rows comes next.

Tested in the simulator: adding from the reading menu and from Recent, the label switching to Remove, the tab, a missing file, removing, and the tabs in English and Greek. Not confirmed on the device yet.


## 0.30.0 (2026-10-29)

Three follow-ups from kos25k, after he tested the last releases on his X4 Pro (thanks!).

- Line spacing has a level 0 now: no space between the lines at all. Descenders and the accents on the next line can touch at 0, that is what it means. Your current spacing stays the same after the update (the levels moved up by one to make room).
- A new page background, Papyrus cloudy: soft blotches of dots, lighter and darker in patches, with no strips or straight lines. If you set up your own Paper Button Cycle before, tick the new one in Settings → Reader → Paper Button Cycle.
- The CrossPoint home styles with a menu list (like the 3-covers one) have a Gallery entry now, with an icon, after Library. All six entries fit on the screen. The cover-grid style is unchanged.

Tested in the simulator: level 0 with Greek accented and polytonic text, the cloudy background behind text, the six-entry menu on the 3-covers style (Gallery and Settings open the right screens), and old saved spacing loading one level up. Not confirmed on the device yet.


## 0.29.0 (2026-10-29)

Getting ready for the Android companion app, which comes out as its own release right after this one.

- The device now shows up on your network as vaultink.local (it was crosspoint.local) while File Transfer is open, and it announces itself so apps can find it without typing an address.
- Notes you upload, rename, move or delete in /Vaults through the web page (or the companion app) now count as changed on the device, so the next LiveSync sync sends them to the server, same as notes changed on the device itself.
- The update check only offers real firmware releases now (a plain version like 0.29.0). The companion app has its own releases, and the device ignores them.
- The web server docs had one thing wrong: an upload never overwrites a file with the same name, it fails and says the file is already there.

Tested in the simulator and with my phone against the simulator: finding the device, uploading notes into a vault and seeing them on the changed list, and the update check rule on sample tags. Not confirmed on the device yet.


## 0.28.0 (2026-10-28)

LiveSync goes both ways now. That finishes Phase 4.

- Every Sync now (and the Sync tile, and sync on wake) first sends the notes you changed on the device, then pulls. That covers ticked tasks, edited notes, and new, renamed and deleted notes. The result line tells you how many went up.
- Nothing gets overwritten. If a note changed in Obsidian too since your last sync, you get both: Obsidian's version is kept as "Note (conflict date).md" next to yours, and both go up to the server.
- A note only leaves the device's list of changes once the server has it. If Wi-Fi drops halfway, the next sync just sends it again.
- One thing to do: if you set up LiveSync before this version, set it up once more (paste the Setup URI on the device's web page, then Finish setup). The device needs two values from your vault passphrase to send notes, and older setups didn't keep them. Until then, it pulls as before and tells you changes aren't sent.
- It works with the default LiveSync settings (end-to-end encryption, path obfuscation on). Notes over 256 KB aren't sent yet.

Tested against a local CouchDB with the official LiveSync command line tool, which is the plugin's own code. Every id the device makes matches the plugin's. Notes the device sent came back byte for byte: a new Greek note in 10 pieces, an edit, a delete, and a conflict where both sides changed. I tested it in the simulator, not on the device yet, and not with a real Obsidian install. Please keep a backup of your vault for the first syncs.


## 0.27.0 (2026-10-28)

More from kos25k (#12, and the follow-up on #10).

- In a book, the Text panel has all the layout settings now. Next to font, size, line spacing, alignment, hyphenation and focus reading, it has Even Word Gaps, word spacing, character spacing, screen margin and paragraph indentation. Every change shows right away on the page you're reading. The panel scrolls.
- Quick settings has a Library tile when you open it anywhere except in a book, next to the paper tile. In a book that spot is still Home, and the book card at the top takes you to the library.
- Even Word Gaps has two levels now. Slight is how 0.24.0 did it: gaps grow by up to half a space. Medium lets them grow by up to one space, so more lines reach the right edge and fewer stay short. Off is still the default.

I didn't make hyphenation split words at any letter to fill the short lines. In Greek (and English) a break only reads right between syllables, so "βελτ-ιωμένη" would look worse than the gap.

Tested in the simulator: the new rows in the Text panel (word spacing changed live), the Library tile, and Slight vs Medium on a Greek paragraph. Not tested on the device yet.


## 0.26.0 (2026-10-28)

Another small one, from kos25k (#11).

- When you swipe down quick settings over a book or note, the card at the top with the title and progress does something now. A tap opens the library (for a note, the note's folder in the vault). A long press goes to the home screen. The whole card is the button.

Tested in the simulator: a tap went to the library (and to the folder for a note), a long press went home, and the other tiles still work. Not tested on the device yet.


## 0.25.0 (2026-10-28)

A small one: tap zones.

- Settings → Controls → Tap Zones. "Whole page" is how it was, and stays the default. "Corners only" turns the page only when you tap one of the four corners: right corners go forward, left corners go back. Taps anywhere else do nothing, so a stray finger while reading doesn't flip the page. Swipes and the buttons work as before.
- It only does something when your page gestures allow taps (Settings → Controls → Next/Previous page gesture).

Tested in the simulator: with Corners only, a tap in the middle-right did nothing and the corners turned pages forward and back. Not tested on the device yet.


## 0.24.0 (2026-10-28)

Five more from kos25k (issues #2, #7, #8, #9, #10).

- Battery on the sleep screen: on the default sleep screen the percent now sits above the VaultInk logo. On covers and pictures it stays in the corner.
- Unzip: in Browse Files, a .zip has "Extract here" now. It unpacks into a new folder with the zip's name, right next to it. Nothing outside that folder gets written, and if the card fills up, the half-made folder is removed.
- Gallery: Home → Gallery shows the pictures on the card as a grid, nine to a page. Or long-press a folder in Browse Files → "Open as gallery" for just that folder and its subfolders. Tap a picture to view it, delete it, move it or copy it. The first time a page shows, the thumbnails take a moment; after that they come back fast.
- What's New: after an update, the device shows what changed, like this text, before anything else. Back leaves it. It's also under Settings → System → What's New, and you can turn it off there. Coming from 0.23.0 or older, it shows only this release, because older versions didn't remember which version they were.
- Even Word Gaps: Settings → Reader → Even Word Gaps. With justified text, a line only gets stretched a little. A line that would need big gaps stays ragged instead, with normal spaces. It's off by default.

Tested in the simulator: the sleep screen, unzipping (including a zip that tried to write outside its folder), the gallery with a subfolder and View/Back, What's New after an update, and Even Word Gaps on a Greek paragraph. Not tested on the device yet.


## 0.23.0 (2026-10-27)

You can edit notes on the device now, line by line.

- In a note: reading menu → More → Edit note. You get the note's lines with their numbers. Tap a line to edit it on the keyboard, add a line above or below, delete it, or move it up or down. Undo takes back your last changes (up to 20), Save writes the note and shows it again. Back asks before throwing away changes you didn't save.
- Everything you didn't touch stays byte for byte the same, line endings and all, so Obsidian and sync see only the lines you changed.
- If the note changed on the card while you were editing (a sync, say), Save doesn't overwrite it, it tells you instead.
- LiveSync treats edited notes like ticked tasks: never overwritten, a newer server copy goes next to it as a "(conflict ...)" note. Sending changes back to the server is the next step.
- Limits for now: notes up to 256 KB, lines up to 2000 characters, and you can't split or join lines yet.
- Also: the CI check that failed on 0.22.0 (code style only, the firmware was fine) is fixed.

Tested in the simulator: deleting a line and saving gave exactly the right bytes, and an edit through the keyboard kept the note's Windows line endings. Not tested on the device yet.


## 0.22.0 (2026-10-27)

First bit of editing on the device, and debug logs.

- Tick tasks: tap the [ ] box of a task in a note and it's saved. Works even with touch controls off. If the note changed meanwhile (say LiveSync brought a new version), it doesn't tick anything, it just shows the note again.
- New note, Rename note and Delete note, in the reading menu under More. Delete moves the note to the vault's .trash folder, like Obsidian does, nothing is deleted for real. Renaming doesn't fix links to the note yet.
- LiveSync never overwrites a note you changed on the device. If the server has a newer version too, it's saved next to yours as "Note (conflict 2026-10-27).md". Sending changes back to the server comes later.
- Every save goes through a temp file, so a power cut mid-save doesn't leave half a note.
- Debug logs, off by default: Settings → System → Debug Logs. The device then writes its log to the debug folder on the SD card (1 MB at most). When something goes wrong, email me the files at me@lefteros.com, not on GitHub. Every boot also logs why it started, which should help with screens that turn on by themselves. Details in docs/debug-logs.md.

Tested in the simulator: ticking (and that a changed note is refused), new note, rename, delete to .trash, and the debug log. LiveSync with a changed note is tested on the host against the recorded database. Nothing of this is tested on the device yet.


## 0.21.0 (2026-10-27)

LiveSync, the rest of Phase 3.

- The server's certificate is checked now, and so is its name. Seven common root certificates are built in (Let's Encrypt, DigiCert, Google Trust Services/Cloudflare, Amazon, Sectigo), so a normal HTTPS server works with nothing to set up. For a self-signed server put its certificate at /.vaultink/livesync/ca.pem. Or turn off "Check certificate" on the LiveSync screen. The connection stays encrypted then, but the server isn't checked.
- If you set up LiveSync on 0.19.0 or 0.20.0, the check is on after this update. If your sync stops working, the message tells you what to do: ca.pem, or turn the check off.
- A Sync tile in quick settings (swipe down), once LiveSync is set up. It syncs right away and then takes you back to what you were reading.
- Sync on wake, off by default: on the LiveSync screen, tap "Sync on wake" for every 6 or 24 hours. When the device wakes and that much time has passed, it joins your saved Wi-Fi by itself (10 seconds at most), pulls the changes and opens your book again. No saved network, low battery or no network in reach, and it just skips it.
- A fix in the TLS code underneath: when it checked a certificate, it never checked that the certificate was for the right server. Now it does.

Tested in the simulator against a local LiveSync database: the tile, and sync on wake. The certificate check can't be tested in the simulator, so it's not tested yet. That needs the device: a Let's Encrypt server, a self-signed one with ca.pem, and one on an IP address. Going back to the book after the sync also needs the device.


## 0.20.0 (2026-10-27)

Six requests from kos25k (issues #1 to #6), all in this one.

- Paper Button Cycle: Settings → Reader → Paper Button Cycle has a checklist of the page backgrounds. The paper tile in quick settings now only steps through the ones you tick.
- Battery on the sleep screen: the battery percent shows at the bottom right while the device sleeps. It's the charge when it went to sleep. You can turn it off under Settings → Display → Battery on Sleep Screen.
- Line spacing has ten levels now, 1 to 10, instead of Tight/Normal/Wide/Extra wide. 1 packs the lines tight, 10 is the old Extra wide. Your current spacing stays the same, it just shows as a number now (Normal is 6). It's under Settings → Reader → Text Settings → Layout, and in the reader's Text panel too.
- Hide Status Bar: Settings → Reader → Hide Status Bar hides the whole bar and gives its room to the text. There's no tap to hide it inside a book, because every change has to lay out the chapter again.
- Hyphenation is on by default now. Justified text without it gets big gaps between words. If you set it before, your setting stays: turn it on in the reader's Text panel if the gaps bother you.
- File browser: long-press a file or folder for Open, Delete, Rename, Move, Copy and New folder. Folders can be renamed now too. Move and Copy open a folder picker, pick a folder and tap "Put it in this folder". Nothing gets overwritten, and a copy that runs out of space leaves nothing half-written. One thing: if you move or rename a folder with books inside, those books start from the beginning again (their reading progress doesn't follow yet).

Tested in the simulator: the checklist and the paper tile, line spacing 1 and 10 and the move from the old setting, the sleep screen percent on the dark screen and over a gray picture, and copy and move in the file browser (a copied folder came out identical). Not tested on the device yet, the percent over cover pictures especially.


## 0.19.0 (2026-10-27)

LiveSync, read only. VaultInk can now pull a vault from Obsidian's Self-hosted LiveSync over Wi-Fi.

How: start File Transfer, open the device's page in a browser, and paste your LiveSync Setup URI under Settings → LiveSync. Then on the device go to Settings → Vaults → LiveSync → Finish setup and type the Setup URI's passphrase. After that, Sync now brings the vault into /Vaults/<database name>. New and edited notes come across, and so do deletes and renames. Deleted files go to a trash folder, nothing is thrown away. If Wi-Fi drops halfway, the next sync just picks up again.

It needs end-to-end encryption on (V2) and compression off. Path obfuscation is fine. The passphrase is never stored. The saved password and vault key are only obfuscated with the device's ID, so treat the SD card like a key to that vault. The server's certificate isn't checked unless you put its CA certificate at /.vaultink/livesync/ca.pem. Your notes stay end-to-end encrypted either way. The details are in docs/livesync.md.

Nothing goes back to the server yet. Editing and pushing come later.

Not confirmed on the device yet. I tested it end to end in the simulator against a real LiveSync database (filled by the official LiveSync CLI), with edits, a delete and a rename. On the real device the first unlock may take a while (310,000 rounds of key derivation). I don't know yet how long, that needs a hardware test.


## 0.18.1 (2026-10-27)

Back is easier to find. The X4 Pro has no Back key, so inside a note the only way back was a swipe in from the left edge of the screen, and it's easy to miss (thanks Kostas for catching that). Now a note's menu has **« Back** at the top: it goes to the note you came from, or to the note's folder. A canvas has a **«** button on top of its zoom buttons that does the same.

Not confirmed on the device yet, checked in the simulator.


## 0.18.0 (2026-10-27)

Zoom buttons, pictures and colours on canvases. There are now + and − buttons (and Fit) at the bottom right of a canvas, so you can zoom without the Confirm button, and the zoom level shows between them. A card that points to an image now shows the picture inside the card, and coloured cards and groups are shaded gray. E-ink has one gray to give, so all of Obsidian's colours look the same, but you can tell coloured cards apart from plain ones.

Also fixed: picture sizes like `![[photo.jpg|300]]` are now respected. Before, every picture was drawn full width.

Not confirmed on the device yet. Checked in the simulator with the demo vault (VaultInk-Demo.zip is still attached to 0.17.0).


## 0.17.0 (2026-10-27)

You can now put a whole Obsidian vault on the device from its web page. Zip the vault on your computer, start File Transfer on the device, open the device's page in the browser, and in Files press **Import Vault (.zip)**. The browser unpacks it and sends everything into `/Vaults/<name>/`, then it's right there under Vaults. If a vault with that name already exists, the new one goes next to it as "<name> (2)" instead of mixing in. Obsidian's own `.obsidian` settings folder is skipped, the device doesn't need it.

There's also a demo vault attached below (`VaultInk-Demo.zip`), made for testing: notes in English and Greek, links, callouts, highlights, embeds, pictures and a canvas. Import it with the button above and have a look around.

Not confirmed on the device yet. I tested the import against the simulator's web server (the files arrive byte for byte, Greek names too) and opened the result in the simulator.


## 0.16.0 (2026-10-27)

Canvases! `.canvas` files from Obsidian now open on the device. You see the whole board fitted to the screen, with its cards, groups and arrows (with their labels). Pan with the buttons or by swiping, press Confirm to zoom in (2x, 4x, 8x, then back to the whole board), and tap a card to open it: a card that points to a note opens that note, and a text card opens as a note of its own. Back takes you back to the canvas.

Canvases show up in the file browser, open from `[[links]]`, and the Vaults list remembers one as the last thing you read.

Not there yet: card colours (a coloured card just gets a thicker frame for now), pictures inside cards, and pinch to zoom.

Not confirmed on the device yet. I checked it in the simulator and ran it against the canvases in my own vaults (all parse fully, 51 to 106 cards).


## 0.15.0 (2026-10-27)

Small one. In Settings, the Text Settings row now shows your reading font and size ("Noto Sans 14") and the Language row shows the current language, so you don't have to open them to check. And the quick settings card now shows how far you are in XTC books too, not only EPUB, TXT and notes.

Not confirmed on the device yet. The XTC part is only build-tested, I had no XTC book in the simulator.


## 0.14.0 (2026-10-27)

New in Settings → Vaults: Highlight style. Pick how `==highlights==` look:

- Light gray (the default): a light gray band behind black text.
- Inverted: white text on black.
- Dark gray: white text on a dark gray band. With text anti-aliasing turned off this one comes out black, same as Inverted.

Not confirmed on the device yet, so the exact shade of the dark gray band on a real screen is still to be seen.


## 0.13.0 (2026-10-27)

`==Highlights==` are now a light gray band behind the text, like on a Kindle or Kobo, instead of an underline. So they no longer look like links, and the line doesn't cut through g, p, q and y.

Also, links that give part of a path, like `[[People/Anna]]` when the note really lives in `Notes/People/Anna.md`, now find the note the way Obsidian does. On my friend's vault that took broken links from about 2,100 down to 92, and what's left points at folders or old Logseq names.

Notes you've already opened get rebuilt once.

Not confirmed on the device yet.


## 0.12.0 (2026-10-27)

The home screen now shows how many books and vaults you have, right on the Books and Vaults rows ("128 books · recent reads", "2 Obsidian vaults on the SD card"). The book count comes from the library index, so before that's built the row just shows its usual text.

Not confirmed on the device yet.


## 0.11.0 (2026-10-27)

Callouts now look like callouts: a framed box with the title in bold, instead of just a bold line. Plain quotes stay indented like before.

Notes you've already opened get rebuilt once, so section embeds from 0.10.0 and the new callouts show up there too.

Not confirmed on the device yet.


## 0.10.1 (2026-10-27)

Fixed links to notes whose name has a dot in it, like "Dr. Smith", "Section 2.1" or a title that ends in "…". They were being read as a file with an extension and came up as not found. I ran every note of my own vaults through the converter: all links, images, embeds and heading/block links now resolve.

Not confirmed on the device yet.


## 0.10.0 (2026-10-27)

Embeds can now point at part of a note, the way Obsidian does: `![[Note#Heading]]` shows just that heading's section, and `![[Note#^id]]` shows just the paragraph or list item with that block id. If the heading or id isn't there, you get the whole note like before.

Not confirmed on the device yet.


## 0.9.1 (2026-10-27)

Links, images and embedded notes in a vault are now found from the search index's list of files, so opening a note no longer scans the whole SD card looking for them. That matters once a vault has thousands of notes. The list is written each time Search updates the index. Anything newer than that is still found the old way.

Also fixed: a plugin that hasn't been set up yet (like WebDAV with no server filled in) now says "Set up this plugin first" instead of trying to connect to a nonsense address.

Not confirmed on the device yet.


## 0.9.0 (2026-10-27)

Each vault now remembers the note you read last: the Vaults list shows "Last: <note>" and opening the vault lands right on it. There's also a Vaults group in Settings (under Reading) with a switch for that and a button to rebuild the search index if search ever looks off.

Not confirmed on the device yet.


## 0.8.10 (2026-10-26)

One menu at a time (kos25k's idea): quick settings no longer opens over the reading menu, so menus can't stack and leave ghosts of each other.

Tested on the device by kos25k.


## 0.8.9 (2026-10-26)

On UC8279 X4 Pro screens the page is redrawn with a stronger refresh after the reading toolbar closes, so no faint copy of the toolbar stays behind.

Tested on the device by kos25k.


## 0.8.8 (2026-10-26)

Fuller lines with Greek hyphenation. Words can now break next to a one-letter syllable (ά-νε-μος, ι-δέ-α), like Greek typesetting allows, so less empty space at the right edge.

Tested on the device by kos25k.


## 0.8.7 (2026-10-26)

A cleaner screen after quick settings closes (it left a faint copy behind on some screens), and the refresh tile now really cleans pages with text smoothing.

On kos25k's X4 Pro the leftover now clears with a swipe instead of needing the power button, but it's not fully gone yet.


## 0.8.6 (2026-10-26)

Hyphenation can be switched while reading: tap the middle of the page → Text → Hyphenation.

Tested on the device by kos25k.


## 0.8.5 (2026-10-26)

Fix: long option lists (font sizes in a book, screen margins) ran off the top of the screen, so the top choices were out of reach. They now scroll with a "· · ·" row.

Tested on the device by kos25k: font sizes inside the book and the margins list work.


## 0.8.4 (2026-10-26)

Fonts can be moved into /.fonts from the Wi-Fi file manager (it said "protected folder"), and screen margins go down to 0.

Tested on the device by kos25k: smaller margins work, and a .ttf font gives sizes in 1 pt steps.


## 0.8.3 (2026-10-26)

Fix: with a page background on, the whole page could turn pale on some X4 Pro screens (UC8279). Text smoothing is now off while a background is on.

Tested on the device by kos25k: updating over Wi-Fi worked, and text stays black on backgrounds.


## 0.8.2 (2026-10-26)

Fix: tapping the paper tile in quick settings could hit the rotate tile and turn the book upside down.

Tested on the device by kos25k: no more upside-down book.


## 0.8.1 (2026-10-26)

English and Ελληνικά are now at the top of the language list (Greek was hard to find), and About credits me with a link for donations.

Not tested on the device yet.


## 0.8.0 (2026-10-26)

Better notes: nested lists are indented, numbered lists keep their first number, footnotes work, and ![[Note]] on its own line shows that note inline. The web installer got a device picker.

Not tested on the device yet.


## 0.7.0 (2026-10-26)

Backlinks: in a note, More → Backlinks lists the notes that link to it, and Back takes you to where you were.

kos25k ran this one on his X4 Pro: it boots, the menus and quick settings work.


## 0.6.0 (2026-10-26)

Page backgrounds, mostly for fun: paper, sandpaper, three kinds of papyrus, toilet paper, kitchen paper and bath towel. Settings → Reader → Page Background, or the paper tile in quick settings.

Not tested on the device yet.


## 0.5.0 (2026-10-26)

Search across all your vaults (titles, aliases, tags and text, accents don't matter) and a Greek + English on-screen keyboard.

Not tested on the device yet, only in the simulator.

