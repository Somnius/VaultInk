# Reading settings and file actions

This page covers the reading and file features added in 0.20.0, from kos25k's issues #1 to #6.

## Page backgrounds: Paper Button Cycle

The paper tile in quick settings (swipe down from the top) changes the page background. By
default it steps through all nine: plain, paper, sandpaper, papyrus soft, medium and hard, toilet
paper, kitchen paper, bath towel.

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

Turn it off under **Settings → Display → Battery on Sleep Screen**. It shows on the dark and light
screens, the blank screen, covers and custom pictures. Quick Resume (which keeps the last page)
and the transparent overlay pictures don't get it.

## Line spacing, 1 to 10

Line spacing has ten levels, shown as numbers. **1** packs the lines tight, so they nearly touch.
**10** is the former Extra wide. The old four steps are now levels 5 (Tight), 6 (Normal), 8 (Wide)
and 10 (Extra wide). A setting saved by an older version loads as the same spacing.

Change it under **Settings → Reader → Text Settings → Layout → Line Spacing**, or inside a book or
note: tap the middle of the screen, open the Text panel, third row. Inside the reader it applies
at once.

SD-card fonts and the built-in font have separate scales (SD fonts 0.75× to 1.20× of the font's
line height, built-in 0.70× to 1.05×), because they measure their line height differently. With
some fonts, accents on capitals can touch the line above at level 1.

## Hide Status Bar

**Settings → Reader → Hide Status Bar** hides the whole status bar at the bottom of a book or
note, and the text uses that room. Your choices under Customise Status Bar are kept for when you
turn it back on.

There is no tap to show and hide it inside a book. A different page height means the chapter
has to be laid out again, which takes a few seconds each time. The bottom of the screen also
already turns the page.

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
- A new folder's name can't contain `/` or `\`, or be `.` or `..`. Names the SD card refuses
  (like `:` or `?`) give the "Could not create the folder" message.
