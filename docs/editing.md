# Changing notes on the device

From 0.22.0 you can change notes a little on the device. Typing into a note comes later; for now
you can tick tasks, and make, rename and delete notes.

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
