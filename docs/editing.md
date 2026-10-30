# Changing notes on the device

From 0.22.0 you can tick tasks, and make, rename and delete notes on the device. From 0.23.0 you
can also edit a note line by line.

## Tick a task

Tap the box of a task (`- [ ]` or `- [x]`) in a note. It flips, the note is saved, and the page
comes back with the box ticked or unticked. This works even with touch reader controls off. The
box is narrow, so a tap a little to its left or right counts too.

Since 0.50.0 the box is drawn as ☐ / ☑ with the built-in reading font. With a font from the SD
card it stays `[ ]` / `[x]`, since those fonts may not have the symbols.

The note is checked again before it's saved. If it changed since the page was drawn (for example
LiveSync brought a new version), nothing is ticked and the note is shown again as it is now.

Boxes inside quotes, and inside notes embedded with `![[...]]`, can't be ticked: their lines are
somewhere else.

## New note, rename, delete

In a note, open the reading menu (tap the middle of the screen), then **More**:

- **New note** asks for a name and makes `<name>.md` in the same folder, with the name as its
  title, then opens it.
- **Rename note** asks for the new name. The reading position comes along. If other notes link
  to it (0.38.0), you're asked **Update links?** with how many links in how many notes; **Update**
  changes them to the new name, **Leave them** doesn't. Changed are `[[Old]]`, `[[Old|text]]`,
  `[[Old#Heading]]`, `[[Old#^block]]`, `![[Old]]` and paths ending in the name (`[[People/Old]]`),
  only where they lead to this note: a note with the same name in another folder is left alone.
  (next release) Markdown links to it change too (`[text](Old.md)`, `[text](Folder/Old%20Name.md#Part)`;
  spaces in the new name are written as `%20`).
  Not changed: links inside code, links with a title or in angle brackets, web links, canvases, notes
  over 256 KB, and links in the renamed note to itself. The changed notes are sent to LiveSync like any edit.
  The links are found with the vault's search index, which is brought up to date first.
- **Delete note** asks first, then moves the note to the vault's **`.trash`** folder, the same
  folder Obsidian uses for "Move to Obsidian trash". If a note with that name is already in the
  trash, the time is added to the name. Nothing is deleted outright.

A name can't contain `\ / : * ? " < > |`, start with a dot or a space, or end with a space or a
dot, and it can be 100 bytes at most. A name that's already there is refused.

## Edit a note, line by line

In a note, open the reading menu, then **More → Edit note**. The note's lines are listed with
their numbers (a long line shows wrapped, but it's still one line). Tap a line for:

- **Edit**: the keyboard opens with the line in it.
- **Insert line above** / **Insert line below**: the keyboard opens empty.
- **Delete line**, **Move up**, **Move down**.
- (next release) **Join With Next Line**: the line and the one after it become one line, with a space
  between (none next to an empty line). One Undo puts them back. Splitting a line waits for a
  multi-line keyboard.

**Undo** at the top takes back the last change, up to 20. **Save** writes the note and shows it
again in the reader. Back (the header arrow or the left-edge swipe) asks before throwing away
unsaved changes.

What stays as it was: every line you didn't touch keeps its exact bytes, its line ending (LF or
CRLF, even mixed in one note), and a byte order mark at the start. A note that ended without a
newline still does. New lines use the ending most of the note uses.

Limits:

- A note over 256 KB can't be opened in the editor.
- A note with a line over 2000 characters opens to look at only (the keyboard can't hold it).
- There's no way to split one line into two or join two lines yet: edit one, insert another.
- If the note changed on the card while the editor was open (say a LiveSync sync), Save refuses
  and says so. Nothing is overwritten; open it again and redo the change.

## Saving safely

Every change is written to `name.md.tmp` first and then put in place. If the power goes in
between, the next time the note is opened the complete temp file is put back (or, when the note
is still there, the possibly half-written temp file is removed).

## Notes changed on the device and LiveSync

Each vault keeps a list of the notes changed on the device: `/.vaultink/<vault id>/changed.txt`.
From 0.28.0 every LiveSync sync sends these notes to the server first (see
[livesync.md](livesync.md)), and a note leaves the list once the server has it. The pull treats
the notes still on the list carefully:

- If the server has a newer version of a note you changed here, your note stays as it is, and
  the server's version is saved next to it as `Note (conflict 2026-10-27).md`.
- If the note was deleted or renamed on the server, your copy stays where it is.
- The LiveSync result line says how many notes this happened to.

Without LiveSync the list just records what you changed; nothing else reads it.
