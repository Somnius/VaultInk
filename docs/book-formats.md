# Book formats

What VaultInk opens, how well, and what it leaves out. Put books in any folder on the SD card (the `books` folder is the usual one), or send them with File Transfer (Wi-Fi, USB or Calibre) or the [companion app](companion.md). The Library lists the ones ticked in **Settings → Library → Book formats** (or the gear in the Library header).

| Format | Opens as | Works | Doesn't work yet |
| --- | --- | --- | --- |
| **EPUB** | a book | Everything: chapters, contents, images, bookmarks, highlights, search, dictionary, hyphenation, fonts, KOReader sync | |
| **TXT** and **Markdown** | a book | the whole text, reflowed; a picture next to it with the same name, or called `cover`, is its cover | no chapters (one chapter) |
| **PDF** | a book made from the PDF's text, once ([PDF as text](reading-and-files.md#pdf-as-text)) | text in reading order, up to four columns, form field values, Greek | pictures, tables as tables, scans with no text, page numbers as pages |
| **MOBI**, **PRC** (0.67.0) | a book | text, bold and italic, headings, lists, tables and the pictures inside the book; Huffman-compressed books too (0.67.1); title and author from the file | chapters (the book is one chapter), links, the cover inside the file |
| **AZW**, **AZW3** (0.67.1) | the same way as MOBI | the text, in order | pictures, links and chapters of the newer KF8 format; a heading can sit apart from its paragraph |
| **XTC**, **XTCH** | comic or book pages | made by the [Banana Comic Converter](comics.md); XTCH turns pages faster | |
| **CBZ** (0.39.0) | comic pages | JPEG and PNG pages | other image types |

## MOBI, AZW and AZW3 in detail

They open like a text book: the file is read one record at a time, so a big book doesn't fill the memory, and the book is laid out by the same reader as an EPUB, so every reading setting (font, spacing, [Advanced Typography](reading-and-files.md#advanced-typography-0660-renamed-from-experimental-in-0661), page backgrounds) works.

- **DRM**: a book with DRM (most books bought from Kindle stores) is refused with "This book is DRM-protected and can't be opened". VaultInk doesn't break DRM, and won't.
- **Compression**: both ways Kindle books are packed (the simple one and the Huffman one) are read.
- **AZW3 / KF8**: this is the newer Amazon format. VaultInk reads its text in the order it is stored, which is the reading order in the books tried so far. Because the table that places the pieces isn't used, pictures (`kindle:embed` links), links and chapters don't work, and a heading can end up apart from its paragraph.
- **Library**: the formats row is called **MOBI / AZW**; it covers `.mobi`, `.prc`, `.azw` and `.azw3`. A Book formats choice saved before 0.67.0 gets it ticked.
- **Tried with**: a Project Gutenberg MOBI, and three non-DRM books (a MOBI, an AZW3 and an AZW with Huffman compression), all in the simulator, not on a device yet.

If a book doesn't open, converting it to EPUB with [Calibre](https://calibre-ebook.com) always works (remove DRM only from books you own, where the law allows it).

## Comics

Comics go through the Banana Comic Converter, which cuts each page into panels for the small screen: see [comics.md](comics.md).

## Obsidian notes

`.md` notes in a vault open as book pages with their links, tasks, callouts, tables and embeds: see [editing.md](editing.md) and [livesync.md](livesync.md).
