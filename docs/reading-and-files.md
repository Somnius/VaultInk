# Reading settings and file actions

This page covers the reading and file features from kos25k's issues: #1 to #6 (0.20.0) and #2, #7,
#8, #9, #10 (0.24.0).

## Page backgrounds: Paper Button Cycle

The paper tile in quick settings (swipe down from the top) changes the page background. By
default it steps through all ten: plain, paper, sandpaper, papyrus soft, medium and hard, toilet
paper, kitchen paper, bath towel, and (from 0.30.0) papyrus cloudy: soft blotches of dots with no
strips or lines, like a cloudy sky. A cycle you set up before 0.30.0 doesn't include the new one;
tick it in the checklist below.

**Settings → Reader → Paper Button Cycle** has a checklist of them. Untick the ones you don't
want, and the tile skips them. At least one always stays ticked. If the background in use isn't
ticked, the next tap goes to the next ticked one.

All ticked is saved as "all", so a background added in a later version joins the cycle too.
Settings → Reader → Page Background still picks any background directly. A background change
only redraws the page, nothing is laid out again.

## Battery percent on the sleep screen

While the device sleeps, the battery percent shows at the bottom right, black on a small white
patch so it stays readable over a cover or a custom picture. It is the charge at the moment the
device went to sleep: the screen isn't updated while it sleeps.

On the default dark or light sleep screen it sits centred above the VaultInk logo (0.24.0); on
covers and pictures it stays in the corner. Turn it off under **Settings → Display → Battery on
Sleep Screen**. It shows on the dark and light
screens, the blank screen, covers and custom pictures. Quick Resume (which keeps the last page)
and the transparent overlay pictures don't get it.

## Line spacing, 0 to 10

Line spacing has eleven levels, shown as numbers. **0** (from 0.30.0) leaves no space between the
lines: descenders and the accents on the line below can touch. **1** packs them tight, so they nearly
touch. **10** is the former Extra wide. The old four steps are now levels 5 (Tight), 6 (Normal), 8
(Wide) and 10 (Extra wide). A setting saved by an older version loads as the same spacing.

Change it under **Settings → Reader → Text Settings → Layout → Line Spacing**, or inside a book or
note: tap the middle of the screen, open the Text panel, third row. Inside the reader it applies
at once.

SD-card fonts and the built-in font have separate scales (SD fonts 0.70× to 1.20× of the font's
line height, built-in 0.65× to 1.05×), because they measure their line height differently. With
some fonts, accents on capitals can touch the line above at levels 0 and 1.

## Hide Status Bar

**Settings → Reader → Hide Status Bar** hides the whole status bar at the bottom of a book or
note, and the text uses that room. Your choices under Customise Status Bar are kept for when you
turn it back on.

There is no tap to show and hide it inside a book. A different page height means the chapter
has to be laid out again, which takes a few seconds each time. The bottom of the screen also
already turns the page.

## The book card in quick settings (0.26.0)

When quick settings (swipe down from the top) is opened over a book or note, the card at the top
with its title and progress can be tapped: a **tap** opens the library (for a note: the note's
folder in the vault), a **long press** goes to the home screen. The whole card is the target.

## Tap Zones: corners only (0.25.0)

**Settings → Controls → Tap Zones** chooses where a tap turns the page. **Whole page** (the
default) is as before: the left third goes back, the rest forward, and the middle opens the menu.
**Corners only** turns pages only from a tap in one of the four corners (a square about a sixth
of the screen's short side): the right corners go forward, the left ones back (the other way round
for right-to-left books and Inverted Tap). Taps anywhere else do nothing, so resting a finger or
tapping by mistake while reading doesn't turn the page. Swipes and the buttons work as always. The
status bar strip at the very bottom still opens quick settings, also in the bottom corners. Taps
only turn pages when the page gestures (Settings → Controls) allow taps.

## Even Word Gaps (0.24.0, levels in 0.27.0)

**Settings → Reader → Even Word Gaps (justified)** keeps justified text from stretching word gaps
much. **Slight**: a line is justified when its gaps grow by up to half a space. **Medium**: up to
one space, so more lines are filled to the right edge. A line that would need more stays ragged,
with normal gaps. **Off** (the default) justifies every line, as before. A change lays the open
chapter out again.

## In the book: the Text panel (0.27.0)

Inside a book or note, the Text panel (tap the middle, then Text) has every layout setting now:
font, size, line spacing, alignment, hyphenation, focus reading, **Even Word Gaps, word spacing,
character spacing, screen margin and paragraph indentation**. Each change applies at once to the
page you're on. The panel scrolls.

## Favourites (0.31.0)

The Library has a fourth tab, **Favourites**, next to Author. It lists the books you marked, the
newest first (up to 50).

- **Add or remove a book** from inside it: tap the middle of the screen, **More**, then **Add to
  Favourites** (or **Remove from Favourites**). Notes don't have this entry.
- Or in the Library's **Recent** tab: long-press a book, and the last entry adds or removes it.
- In the Favourites tab, tap a book to open it. Long-press (or swipe left) for **Remove from
  Favourites**: only the list entry goes, the book stays on the card.
- Books renamed or moved with the file browser, the reading menu or the web page keep their place.
  A book deleted from the Library or the web page leaves the list. If a favourite's file is gone
  some other way (or its folder was renamed), opening it says so and offers to remove it.
- Search from the Favourites tab searches the whole library, on the Title tab.

The four tabs are as wide as their names (in Greek too) instead of four equal quarters.

The list is kept in `/.crosspoint/favourites.json`. If that file can't be read, it's kept as
`favourites.json.bad` instead of being overwritten.

## Going back from a note (0.33.1)

In a vault note, a swipe from the left edge to the right goes back, the same as the arrow at the
top: to the note you came from if you opened it through a link, otherwise to its folder. Before
0.33.1 this swipe turned to the previous page in notes opened from a folder, so a one-page note
seemed stuck (kos25k #15). In a note, the previous page is the left tap zone now. In books the
swipe still turns to the previous page.

## Search settings (0.33.0)

**Settings → Search settings** (the first row) finds a setting by name. Type part of it and press
OK; capitals and Greek accents don't matter. Each result says which section it's in. Tap one and
that section opens with the setting selected. Settings that live in **Text Settings** (line spacing,
word spacing, fonts and so on) open the Reader section on its Text Settings row, one tap away.

The last row, **Search notes for "…"**, searches your vault notes for the same words, the same as
**Home → Search**. Back from there comes back to the settings results. Tap the top row to search
again. It doesn't search other files on the card: there's no index of them, and reading the whole
card on every search would be far too slow.

## Home screen rows (0.32.0)

**Settings → Display → Home Screen Rows** sets the order of the rows on the VaultInk home (Books,
Vaults, Search, Files, Gallery, Transfer, Plugins, Settings). Tap a row to pick it up, then tap
where it should go; tap it again to put it back. **Reset to default order** at the end undoes it
all. The order is saved when you leave the screen. The Continue card always stays on top, and the
Plugins row keeps its place even while it's hidden. This is for the VaultInk home style only; the
CrossPoint styles keep their own menus.

## Library in quick settings (0.27.0)

When quick settings is opened anywhere except over a book, the second row has a **Library** tile
next to the paper tile. Over a book that place keeps the **Home** tile, and the book card at the
top opens the library instead.

## Word gaps and hyphenation

Justified text spreads the room left on a line over its word gaps. Without hyphenation, a long
word that doesn't fit leaves a lot of room, so some lines get big gaps. From 0.20.0 hyphenation
is **on by default**. Greek, English and the other languages in [hyphenation](hyphenation-trie-format.md)
are split at real syllables.

A setting you saved before stays as it was. Turn hyphenation on inside the reader (Text panel →
Hyphenation) or under Settings → Reader → Text Settings. Left alignment has no stretched gaps at
all, if you prefer a ragged right edge.

## File browser: Move, Copy, New folder, Rename

In **Browse Files**, long-press a file or a folder for:

| Action | What it does |
|---|---|
| Open | Opens the book, note or picture, or goes into the folder. |
| Delete | Asks first, then deletes (a folder with everything in it). |
| Rename | Types a new name. Files keep their extension. Folders can be renamed too. |
| Move | Opens a folder picker. Go to the target folder and tap **« Put it in this folder**. |
| Copy | The same picker. Files and whole folders can be copied. |
| New folder | Makes a folder inside the folder you're looking at. |

A **.zip** has one more action, **Extract here** (0.24.0), and tapping it offers the same menu.
The zip is unpacked into a new folder with the zip's name next to it. Nothing outside that folder
is written: entries with `..` or an absolute path, names the card can't hold, and macOS
`__MACOSX` leftovers are skipped. If that folder already exists you get a message; if the card
fills up, the half-made folder is removed. A **folder** has one more action, **Open as gallery**.

The details:

- Nothing is ever overwritten. If the target folder already has that name, you get a message
  and nothing changes.
- A folder can't be moved or copied into itself.
- A copy is written as `name.part` and renamed when it's complete. If the card fills up or a read
  fails, the part file is removed, and for a folder the half-made copy is removed too.
- Moving or renaming a **book** takes its reading position, bookmarks, clippings and recent-books
  entry with it.
- Moving or renaming a **folder** is one rename on the card. Books inside it start again from the
  beginning (their caches are rebuilt), and a vault's search index is built again the next time
  you search it or open backlinks. Carrying their state along is on the roadmap.
- Copying doesn't copy reading positions. The copy is a new book.
- A new folder's name can't contain `/` or `\`, or be `.` or `..`. If the SD card refuses
  the name, you get "Could not create the folder".

## Gallery (0.24.0)

**Home → Gallery** (in every home style with a menu list; from 0.30.0 also the CrossPoint styles)
shows every picture on the card (PNG, JPEG, and BMP/GIF by name), nine to a
page, from all folders except hidden ones. In Browse Files, long-press a folder → **Open as
gallery** for just that folder and its subfolders. Swipe left or right (or the page buttons) to
turn pages. Tap a picture for:

- **View**: the picture full screen; swipe for the next one in its folder, Back returns to the grid.
- **Delete** (asks first), **Move** and **Copy** (a folder picker, nothing overwritten).

The first time a page is shown its thumbnails are decoded, which takes a moment per picture; they
are kept in `/.crosspoint/thumbs`, so the page comes back fast. Up to 2000 pictures are listed.

## What's New after an update (0.24.0)

After an update, the first start opens a note with what changed since the version you had before
(the release notes, from `CHANGELOG.md` in this repository). Back leaves it. **Settings → System →
What's New** opens the last twelve releases any time, and **Show What's New After Updates** turns
the note after updates off. Updating from 0.23.0 or older shows only the newest release's notes,
because older versions didn't remember which version they were.
