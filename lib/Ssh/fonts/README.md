# The terminal font

`../SshTermFontData.cpp` holds the SSH terminal's bitmaps. It is generated, not edited by hand.

- Source: Terminus Font 4.49.1, Copyright (C) 2020 Dimitar Toshkov Zhekov, SIL Open Font License
  1.1. The licence text shipped with it is `OFL.TXT` in this folder, copied unchanged.
- Download: <https://downloads.sourceforge.net/project/terminus-font/terminus-font-4.49/terminus-font-4.49.1.tar.gz>
  (sha256 `d961c1b781627bf417f9b340693d64fc219e0113ad3a3af1a3424c7aa373ef79`, as downloaded on
  2026-11-14).
- Files used: `ter-u12n.bdf` (6 x 12), `ter-u14n.bdf` (8 x 14), `ter-u16n.bdf` (8 x 16),
  `ter-u18n.bdf` (10 x 18): the normal weight only.
- Left out of the 1356 characters of each file: NUL, the combining marks (U+0300 to U+036F),
  Hebrew, the Braille patterns and the private-use glyphs. 1051 characters stay, 84,080 bytes for
  the four sizes with the code point table.

To make it again:

```
tar xzf terminus-font-4.49.1.tar.gz
python3 scripts/gen_ssh_font.py terminus-font-4.49.1
```

The licence reserves the name "Terminus Font" for the unmodified font. A subset in another format
is a modified version, so the firmware and its documentation call this "the terminal font" and
name Terminus only as the source.
