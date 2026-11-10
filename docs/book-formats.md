# Book formats

What VaultInk opens, how well, and what it leaves out. Put books in any folder on the SD card (the `books` folder is the usual one), or send them with File Transfer (Wi-Fi, USB or Calibre) or the [companion app](companion.md). The Library lists the ones ticked in **Settings → Library → Book formats** (or the gear in the Library header).

| Format | Opens as | Works | Doesn't work yet |
| --- | --- | --- | --- |
| **EPUB** | a book | Everything: chapters, contents, images, bookmarks, highlights, search, dictionary, hyphenation, fonts, KOReader sync | |
| **TXT** and **Markdown** | a book | the whole text, reflowed; a picture next to it with the same name, or called `cover`, is its cover | no chapters (one chapter) |
| **PDF** | a book made from the PDF's text, once ([PDF as text](reading-and-files.md#pdf-as-text)) | text in reading order, up to four columns, form field values, Greek | pictures, tables as tables, scans with no text, page numbers as pages |
| **MOBI**, **PRC** (0.67.0) | a book | text, bold and italic, headings, lists, tables and the pictures inside the book; Huffman-compressed books too (0.67.1); title and author from the file | chapters (the book is one chapter), links, the cover inside the file |
| **AZW**, **AZW3** (0.67.1) | the same way as MOBI | the text, in order | pictures, links and chapters of the newer KF8 format; a heading can sit apart from its paragraph |
| **FB2** (after 0.73.1) | a book | sections, titles, paragraphs, italic, bold, strikethrough, poems, quotes and epigraphs, tables, the notes at the end; title, author and language from the file; Greek and Cyrillic | pictures (skipped), chapters (one chapter), note links (the mark shows raised, it doesn't jump), zipped `.fb2.zip` / `.fbz` |
| **HTML** (`.html`, `.htm`, `.xhtml`) (after 0.73.1) | a book | headings, paragraphs, line breaks, italic and bold, lists, quotes, preformatted text, tables; scripts and styles are dropped; the page's title | pictures, links, CSS, chapters (one chapter), UTF-16 pages |
| **RTF** (after 0.73.1) | a book | paragraphs, bold and italic, headings (outline levels), centred and right-aligned text, Greek and other Unicode text; title and author from the file | pictures, footnotes, headers and footers (skipped); tables show as text with ` \| ` between cells; chapters (one chapter) |
| **XTC**, **XTCH** | comic or book pages | made by the [Banana Comic Converter](comics.md); XTCH turns pages faster | |
| **CBZ** (0.39.0) | comic pages | JPEG and PNG pages | other image types |

## MOBI, AZW and AZW3 in detail

They open like a text book: the file is read one record at a time, so a big book doesn't fill the memory, and the book is laid out by the same reader as an EPUB, so every reading setting (font, spacing, [Advanced Typography](reading-and-files.md#advanced-typography-0660-renamed-from-experimental-in-0661), page backgrounds) works.

- **DRM**: a book with DRM (most books bought from Kindle stores) is refused with "This book is DRM-protected and can't be opened". VaultInk doesn't break DRM, and won't.
- **Compression**: both ways Kindle books are packed (the simple one and the Huffman one) are read.
- **AZW3 / KF8**: this is the newer Amazon format. VaultInk reads its text in the order it is stored, which is the reading order in the books tried so far. Because the table that places the pieces isn't used, pictures (`kindle:embed` links), links and chapters don't work, and a heading can end up apart from its paragraph.
- **Library**: the formats row is called **MOBI / AZW / FB2 / HTML / RTF**; it covers `.mobi`, `.prc`, `.azw`, `.azw3`, `.fb2`, `.html`, `.htm`, `.xhtml` and `.rtf`. A Book formats choice saved before 0.67.0 gets it ticked.
- **Tried with**: a Project Gutenberg MOBI, and three non-DRM books (a MOBI, an AZW3 and an AZW with Huffman compression), in the simulator, and the three non-DRM ones also opened on kos25k's real X4 Pro.

If a book doesn't open, converting it to EPUB with [Calibre](https://calibre-ebook.com) always works (remove DRM only from books you own, where the law allows it).

## FB2, HTML and RTF in detail

These three are turned into the same kind of page a MOBI is, when the book is opened: the file is read a piece at a time (about 1 KB), so a big book doesn't fill the memory, and it is laid out by the same reader as an EPUB, so every reading setting works. The whole book is one chapter, like a `.txt`. A picture next to the book with the same name, or called `cover`, is its cover.

They are not in a release yet, and they have only been checked by the tests that run on a computer: they have not been opened in the simulator or on an X4 Pro.

- **FB2**: plain FictionBook 2 XML. Sections and their titles (the book's title biggest, then smaller by depth), subtitles, paragraphs, emphasis, strong, strikethrough, sub and superscript, epigraphs, cites and annotations as quotes, poems (a stanza is one paragraph with a line per verse), text authors on the right, tables, empty lines. Every body is read, so the notes come after the text. Pictures are skipped: the `<binary>` blocks at the end of the file are read past and not kept. Encodings: UTF-8, UTF-16, ISO-8859-1, Windows-1251, Windows-1252, Windows-1253 and ISO-8859-7; any other is refused. A file cut short shows what there is. Zipped FB2 (`.fb2.zip`, `.fbz`) doesn't open: unzip it first.
- **HTML**: a loose web page or an XHTML file. Headings, paragraphs, `br`, `hr`, `em` / `strong` / `i` / `b`, lists, `blockquote`, `pre`, tables; `script`, `style` and the head are dropped with their text; tags it doesn't know are dropped and their text kept; entities like `&alpha;` are decoded; tags left open are closed. The encoding comes from a BOM, `<meta charset>` or the XML declaration (UTF-8, Windows-1251 / 1252 / 1253, ISO-8859-1 / -7). With none, or one it doesn't know, the page is read as UTF-8 and any byte that isn't UTF-8 as Windows-1252. UTF-16 pages are refused. Pictures and links don't work, and CSS is ignored.
- **RTF**: a subset. Paragraphs, line breaks, bold, italic, hidden text (left out), headings from outline levels, centred and right-aligned paragraphs, quotes, dashes and bullets. Greek works in all three ways RTF writes it: `\uN` Unicode (with the `\ucN` fallback skipped), `\'hh` bytes with `\ansicpg1253`, and `\'hh` bytes in a font marked Greek in the font table (`\fcharset161`), which is what Word does. Windows-1252 and Windows-1251 (Cyrillic) work the same way; other code pages read as Windows-1252. Tables become text, a row per line with ` | ` between the cells. Pictures, embedded objects, shapes, headers, footers, footnotes and comments are skipped.
- **A file that isn't what its name says** (an `.rtf` that doesn't start with `{\rtf`, an `.fb2` that isn't FictionBook XML, a UTF-16 `.html`) is refused with "This book can't be read".
- **Library and Browse Files**: they are listed with the MOBI / AZW row in Book formats and File types, which is now called **MOBI / AZW / FB2 / HTML / RTF**. They share that switch because the saved Library choice has no free place for another one; this way a choice saved by an older version keeps working as it is. FB2 files count as books on the home screen, and so do `.html` and `.rtf` files.
- **File icon**: a page with FB2, HTM or RTF written in it.

## Comics

Comics go through the Banana Comic Converter, which cuts each page into panels for the small screen: see [comics.md](comics.md).

## Obsidian notes

`.md` notes in a vault open as book pages with their links, tasks, callouts, tables and embeds: see [editing.md](editing.md) and [livesync.md](livesync.md).
