# Gamepad buttons in the reader

Not released yet. Tested on a computer only (the button map, how it is saved, and how a press reaches the book).
**It has never been tried with a real gamepad**, on a reader or in the simulator, so treat everything below
about what a pad sends as read from the code, not seen.

This is input plumbing only: the buttons of one Bluetooth gamepad can do the reader's actions. There are no
games in VaultInk and this does not add any.

## What it does

A button of a paired Bluetooth gamepad can be given one of these actions, the same list the
[reader gestures](reading-and-files.md) and the Quick Menu use:

Next page, Previous page, Next chapter, Previous chapter, Reader menu, Quick Menu, Night mode, Frontlight,
Light panel, Bookmark, Refresh screen, Home, Screenshot.

With nothing learned, nothing changes: a remote or a keyboard works exactly as before.

## Setting it up

1. **Settings → Network → Bluetooth Page Turner**: switch it on, **Scan for Remotes**, and pair the gamepad like a
   page-turner remote. It has to show up as a Bluetooth LE HID device (a pad that only speaks classic Bluetooth
   will not be found).
2. With the pad connected, open **Gamepad Buttons** on the same screen.
3. Tap an action; its row says **Press the remote button (15 seconds)**. Press and let go of the pad button. The row
   then shows the button as `report:byte=value` (for example `1:0=04`).
4. Hold a row (or hold Confirm on it) to clear it. **Clear all gamepad buttons** empties the map.

Rules:

- One button per action and one action per button. Teaching a button that already had an action moves it,
  and the row note says the other action lost it.
- The map belongs to one gamepad (its Bluetooth address). Buttons of another device never run through it, and
  learning a button on a second pad replaces the first pad's map. Forgetting the pad under Paired Remotes
  deletes its map.
- A pad button that is not in the map falls back to the page turner: if its report reads as an arrow, Space,
  Enter, Page Up/Down or a volume key, it turns the page as on any remote.
- Actions other than the page and chapter turns work in EPUB books (they run through the same code as the
  Quick Menu rows). In TXT, XTC and the other readers only the four turns do something.
- A button acts on its press. There is no hold, no repeat and no combination of two buttons.
- The pad only works while **Bluetooth Page Turner** is switched on: the radio and its rules (when it starts,
  when it stops to save memory or battery) are the page turner's.
- The map is saved in `settings.json` under `gamepadAddr` and `gamepadButtons`, only while a button is mapped.

## What a gamepad's reports give, and what is not handled

The firmware does not parse a gamepad's HID report descriptor. It uses what the SDK's Bluetooth host already
reports for every remote, `BleKeyboardHost::popRawButton()` (in `freeink-sdk/libs/network/BleKeyboardHost`,
used unchanged). From its source:

- A button is "where the report first differs from its rest frame": the report id, the index of the first
  payload byte that differs, and that byte's value (`value | byteIndex << 8 | reportId << 16`). The rest frame
  is the report the pad sends with nothing pressed, which the host learns as it goes.
- Only the first **8 payload bytes** of a report are compared, and at most 4 report ids are tracked.
- Only the **first** differing byte counts. So:
  - **One button at a time.** A second button held with the first is not seen when its byte comes later in
    the report, and reads as a different button when its byte comes earlier.
  - **Buttons packed as bits in one byte give one code per combination.** A alone, B alone and A+B are three
    different "buttons". Each can be learned, but A+B is not "A and B".
  - **Analogue sticks and triggers cannot be used.** Every value a stick passes through is a new code. Worse,
    a stick that does not rest on exactly the same value each time changes a byte by itself, and that hides
    every button stored in the bytes after it. A pad whose stick bytes come before its button bytes and jitter
    at rest will not work.
  - **A hat switch (D-pad)** is a byte value like any other: one code per direction, if no earlier byte moves.
- One layout is known to the SDK's decoder as an "axis pair" pad (a status byte whose bit 0 is "pressed" and
  two 16-bit axes centred near 2000, from the "gamebrick" remote in its comments): its buttons arrive as one
  tap each with byte index `0xFF` (the code is `0xFFFF00` plus the zone, `0x40` to `0x48`), shown as `0x40` to
  `0x48`. Those can be learned like buttons.
- A press that turns out to be the pad idling on a non-zero byte is dropped while learning.

Real analogue axes and proper hat-switch decoding from the Generic Desktop page would need a change in the
SDK; that is not done here.

## Not verified

- Anything with a real gamepad: whether a given pad pairs, what its reports look like, whether its buttons
  give stable codes.
- The Gamepad Buttons screen on a reader or in the simulator (the firmware with this change has not been
  compiled for the device).
- Memory and battery with a pad connected (the radio rules are the page turner's, unchanged).

## Known limits from the review

- Gamepad button edges that arrive while the lock screen is up are not read then: they wait in the radio's queue and are applied when a book is in front again (only the Home screen drains the queue early). Do not map buttons to actions you would not want applied late.
- The 'Gamepad Buttons' view is offered for any linked device, so a plain page-turner remote can be learned into the pad path; leave it alone unless you want a button-to-action map for that remote.
- The pad path acts on each press without the rest-frame filter that learning uses.
