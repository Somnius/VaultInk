# Changing notes on the device

From 0.22.0 you can tick tasks, and make, rename and delete notes on the device. From 0.23.0 you
can also edit a note line by line.

## Tick a task

Tap the box of a task (`- [ ]` or `- [x]`) in a note. It flips, the note is saved, and the page
comes back with the box ticked or unticked. This works even with touch reader controls off. The
box is narrow, so a tap a little to its left or right counts too.

The note is checked again before it's saved. If it changed since the page was drawn (for example
LiveSync brought a new version), nothing is ticked and the note is shown again as it is now.

Boxes inside quotes, and inside notes embedded with `![[...]]`, can't be ticked: their lines are
somewhere else.

## New note, rename, delete

In a note, open the reading menu (tap the middle of the screen), then **More**:

- **New note** asks for a name and makes `<name>.md` in the same folder, with the name as its
  title, then opens it.
- **Rename note** asks for the new name. The reading position comes along. Links to the note in
  other notes are **not** changed, so `[[Old name]]` stops working. Rename those yourself, or in
  Obsidian.
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
VaultInk can't send changes to the server yet (that's the next step), so a LiveSync pull now
treats those notes carefully:

- If the server has a newer version of a note you changed here, your note stays as it is, and
  the server's version is saved next to it as `Note (conflict 2026-10-27).md`.
- If the note was deleted or renamed on the server, your copy stays where it is.
- The LiveSync result line says how many notes this happened to.

Until push is ready, the list only grows. Once your changes are in Obsidian some other way,
delete `changed.txt` and LiveSync goes back to updating those notes normally.
