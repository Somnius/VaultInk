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
| **DOC** (Word 97-2003, `.doc`) (not released yet) | a book | the text in order: paragraphs, line breaks, the result of fields (a link's text, a page number), tables as text, footnotes and endnotes at the end; Greek and other Unicode text; title and author from the file | bold, italic, headings, pictures, headers and footers, comments, text boxes; password-protected files; Word 6 / 95 files; `.docx` |
| **CHM** (compiled HTML Help, `.chm`) (not released yet) | a book | every page inside, in the order of its contents list, each under its contents title; headings, lists, tables, bold and italic as in an HTML page; the title from the file | pictures, links between pages, the index, CSS, chapters (one chapter) |
| **XTC**, **XTCH** | comic or book pages | made by the [Banana Comic Converter](comics.md); XTCH turns pages faster | |
| **CBZ** (0.39.0) | comic pages | JPEG and PNG pages | other image types |

## MOBI, AZW and AZW3 in detail

They open like a text book: the file is read one record at a time, so a big book doesn't fill the memory, and the book is laid out by the same reader as an EPUB, so every reading setting (font, spacing, [Advanced Typography](reading-and-files.md#advanced-typography-0660-renamed-from-experimental-in-0661), page backgrounds) works.

- **DRM**: a book with DRM (most books bought from Kindle stores) is refused with "This book is DRM-protected and can't be opened". VaultInk doesn't break DRM, and won't.
- **Compression**: both ways Kindle books are packed (the simple one and the Huffman one) are read.
- **AZW3 / KF8**: this is the newer Amazon format. VaultInk reads its text in the order it is stored, which is the reading order in the books tried so far. Because the table that places the pieces isn't used, pictures (`kindle:embed` links), links and chapters don't work, and a heading can end up apart from its paragraph.
- **Library**: the formats row is called **MOBI / AZW / FB2 / HTML / RTF / DOC / CHM**; it covers `.mobi`, `.prc`, `.azw`, `.azw3`, `.fb2`, `.html`, `.htm`, `.xhtml`, `.rtf`, `.doc` and `.chm`. A Book formats choice saved before 0.67.0 gets it ticked.
- **Tried with**: a Project Gutenberg MOBI, and three non-DRM books (a MOBI, an AZW3 and an AZW with Huffman compression), in the simulator, and the three non-DRM ones also opened on kos25k's real X4 Pro.

If a book doesn't open, converting it to EPUB with [Calibre](https://calibre-ebook.com) always works (remove DRM only from books you own, where the law allows it).

## FB2, HTML and RTF in detail

These three are turned into the same kind of page a MOBI is, when the book is opened: the file is read a piece at a time (about 1 KB), so a big book doesn't fill the memory, and it is laid out by the same reader as an EPUB, so every reading setting works. The whole book is one chapter, like a `.txt`. A picture next to the book with the same name, or called `cover`, is its cover.

They are not in a release yet, and they have only been checked by the tests that run on a computer: they have not been opened in the simulator or on an X4 Pro.

- **FB2**: plain FictionBook 2 XML. Sections and their titles (the book's title biggest, then smaller by depth), subtitles, paragraphs, emphasis, strong, strikethrough, sub and superscript, epigraphs, cites and annotations as quotes, poems (a stanza is one paragraph with a line per verse), text authors on the right, tables, empty lines. Every body is read, so the notes come after the text. Pictures are skipped: the `<binary>` blocks at the end of the file are read past and not kept. Encodings: UTF-8, UTF-16, ISO-8859-1, Windows-1251, Windows-1252, Windows-1253 and ISO-8859-7; any other is refused. A file cut short shows what there is. Zipped FB2 (`.fb2.zip`, `.fbz`) doesn't open: unzip it first.
- **HTML**: a loose web page or an XHTML file. Headings, paragraphs, `br`, `hr`, `em` / `strong` / `i` / `b`, lists, `blockquote`, `pre`, tables; `script`, `style` and the head are dropped with their text; tags it doesn't know are dropped and their text kept; entities like `&alpha;` are decoded; tags left open are closed. The encoding comes from a BOM, `<meta charset>` or the XML declaration (UTF-8, Windows-1251 / 1252 / 1253, ISO-8859-1 / -7). With none, or one it doesn't know, the page is read as UTF-8 and any byte that isn't UTF-8 as Windows-1252. UTF-16 pages are refused. Pictures and links don't work, and CSS is ignored.
- **RTF**: a subset. Paragraphs, line breaks, bold, italic, hidden text (left out), headings from outline levels, centred and right-aligned paragraphs, quotes, dashes and bullets. Greek works in all three ways RTF writes it: `\uN` Unicode (with the `\ucN` fallback skipped), `\'hh` bytes with `\ansicpg1253`, and `\'hh` bytes in a font marked Greek in the font table (`\fcharset161`), which is what Word does. Windows-1252 and Windows-1251 (Cyrillic) work the same way; other code pages read as Windows-1252. Tables become text, a row per line with ` | ` between the cells. Pictures, embedded objects, shapes, headers, footers, footnotes and comments are skipped.
- **A file that isn't what its name says** (an `.rtf` that doesn't start with `{\rtf`, an `.fb2` that isn't FictionBook XML, a UTF-16 `.html`) is refused with "This book can't be read".
- **Library and Browse Files**: they are listed with the MOBI / AZW row in Book formats and File types, which is now called **MOBI / AZW / FB2 / HTML / RTF / DOC / CHM** (DOC and CHM are not released yet). They share that switch because the saved Library choice has no free place for another one; this way a choice saved by an older version keeps working as it is. FB2 files count as books on the home screen, and so do `.html` and `.rtf` files.
- **File icon**: a page with FB2, HTM or RTF written in it.

## DOC and CHM in detail

Not in a release yet. Both open like the formats above: converted to one page when the book is opened, laid out by the same reader as an EPUB, the whole book one chapter. A picture next to the book with the same name, or called `cover`, is its cover. Both are VaultInk's own code (MIT), written from the published descriptions of the formats: no library was added, so there is nothing new in [licenses.md](licenses.md).

**They have only been run on a computer** (the tests, with AddressSanitizer and UBSan). They have not been compiled into the firmware, and not opened in the simulator or on an X4 Pro. What the tests could and could not check is listed under each.

### DOC (Word 97, 2000, XP, 2003)

- **What you get**: the text, in order. Paragraphs, line breaks, page breaks as paragraph ends, tabs as spaces. A field shows its result, not its code: a link shows its text, a page number its number. A table is text, a row per line with ` | ` between the cells. Footnotes come after the text, then endnotes, each after a line. Title and author come from the document's properties, the language from the document.
- **What you don't get**: bold, italic, headings as headings, pictures, drawings, headers and footers, comments, text boxes. Text marked hidden, and text deleted with tracked changes on, is shown like the rest, because the character formatting is not read. An empty table cell can move a row break by one cell, because rows are found from the cell marks alone.
- **Greek**: Word stores Greek as UTF-16 and that is read as it is. The 8-bit text Word writes for Western text is read as Windows-1252, which is what Microsoft's description of the format says. A file from another program that wrote Greek as 8-bit Windows-1253 text would show wrong letters: none was at hand to try. Title and author in Windows-1252 and Windows-1253 are read (tested); Windows-1251 and UTF-8 go the same way (not tested).
- **Refused**: a password-protected document ("This document is password-protected and can't be opened"); a Word 6 or Word 95 file, a file that is not a Word document under a `.doc` name (some are RTF or plain text: rename those to `.rtf` or `.txt`), a damaged file ("This book can't be read").
- **`.docx` is not read.** It is a different format (a zip of XML). Convert it to EPUB with [Calibre](https://calibre-ebook.com), or save it as RTF or as "Word 97-2003" from the word processor.
- **Limits**: files up to 256 MB; a file whose streams are in more than 4096 separate runs (very fragmented) or with more than 65536 text pieces is refused; nested fields deeper than 16 are hidden. A file cut short shows the text up to the cut.
- **Tried with**: files made by the tests' own writer (8-bit and UTF-16 pieces, a table, fields, notes, small and fragmented files, a file big enough to need the second kind of allocation table), and one file LibreOffice 26 wrote with its "MS Word 97" filter (`test/markup_books/fixtures/libreoffice.doc`: headings, Greek, a link, a page number, a footnote, a table), which reads right down to the title and author. **No file written by Microsoft Word itself has been tried.** Fast-saved files (many text pieces) follow the same path as the tests' several pieces, but none from Word was read.

### CHM

- **What you get**: every `.htm` / `.html` page in the file, one after the other. The pages the contents list (the `.hhc` file) names come first, in its order, each under its contents title as a heading; pages it doesn't name follow in the order of the file's directory (by name), with a line between them. Each page is read like an [HTML book](#fb2-html-and-rtf-in-detail): headings, paragraphs, lists, tables, bold and italic. The book's title comes from the file.
- **What you don't get**: pictures, links between pages, the index, style sheets, scripts. A page's own heading and its contents title can both show, one under the other.
- **Encoding**: the one each page names (`<meta charset>`). With none: Windows-1253 when the file's language is Greek, Windows-1251 when it is Russian, Ukrainian, Belarusian or Bulgarian, otherwise UTF-8 with Windows-1252 for bytes that aren't UTF-8. Page names with Greek letters in the contents list are not matched, so those pages come at the end without a title.
- **Compression**: the LZX decompressor is VaultInk's own. It needs the file's window in memory: 64 KB in the files seen, 2 MB at most (a file asking for more is refused). On the device this memory, and the list of pages, comes from PSRAM only; when there is none free, or the reader has no PSRAM, the book is refused rather than taking the reader's working memory. A file that asks for the "E8 translation" (made for programs, not text) is refused.
- **Limits**: 4096 pages (the rest are left out), files up to 1 GB, contents titles up to 160 bytes, 4 MB of the contents list.
- **Tried with**: files made by the tests' own CHM writer and LZX compressor, in all three kinds of LZX block, with blocks that cross the 32 KB frames, resets, and windows from 32 KB to 2 MB. Files of the same kinds (windows of 64 KB, 128 KB and 2 MB) were given to 7-Zip, which unpacks them to the same bytes when the last frame is filled to 32 KB the way Microsoft's compiler fills it (when it is not, 7-Zip fails on the last file only; VaultInk reads both); and one real `.chm` made by Microsoft's compiler (a 10 KB test file from another project, one compressed frame) unpacks to the same bytes as with 7-Zip. **No real help file with real pages, a contents list and more than one compressed frame has been tried**, so the order and titles from a real contents list, and big real files, are unproven. How often the compression restarts is taken from the file the way 7-Zip takes it.

## DjVu, scanned PDFs and OCR

These are not read by the reader, and won't be in the firmware. A DjVu page or a scanned PDF page is a picture of a page: showing it on a 4.3" screen needs the text recognised first (OCR), and an OCR engine with its language data is bigger than the reader's whole free flash. Convert them on a computer or a phone first: to EPUB or text with OCR, or to XTC as page images with the [Banana Comic Converter](comics.md). A converter in the companion app is planned, not written.

## Comics

Comics go through the Banana Comic Converter, which cuts each page into panels for the small screen: see [comics.md](comics.md).

## Obsidian notes

`.md` notes in a vault open as book pages with their links, tasks, callouts, tables and embeds: see [editing.md](editing.md) and [livesync.md](livesync.md).
