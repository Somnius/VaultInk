# Comics and manga

The X4 Pro's screen is small for a comic page. The way to read comics on it is to convert them
first with **Banana Comic Converter** by u/curseofthebanana (GitHub: seethroughme):

**https://seethroughme.github.io/Banana-Comic-Converter/**

It's a free tool that runs in the browser, on a phone or a computer, and uses no AI. It cuts each
page into its panels and repacks them so they use as much of the screen as possible, instead of
shrinking the whole page. It has a manual panel editor for the pages it gets wrong, batch
conversion, metadata editing, and it can split big files. It writes XTC, XTCH, EPUB and CBZ.

It's an independent project, not part of VaultInk. Its author agreed to VaultInk linking to it.
Questions about the conversion go to its author (the page has instructions and troubleshooting).
VaultInk doesn't convert anything on the device.

## Which file to make

- **XTCH**: grayscale. The best choice for most manga and comics.
- **XTC**: black and white only. The fastest page turns, good for line art.
- **EPUB**: also opens on VaultInk, with the pictures on its pages.
- **CBZ**: VaultInk can't open CBZ yet (planned, see below). Pick XTCH instead.

For coloured comics, the converter's author suggests the **Light** setting for Darken Midtones.
Big series are easier to handle split into several files (the converter can split by the number
of panels per file).

## Getting the file onto the X4 Pro

The converter gives you the file in your browser's downloads. Then, any of these:

- **Companion app** (Android): on the X4 Pro open **File Transfer**, then in the app go to
  **Files**, open the folder you want (for example make a **Comics** folder), tap **Upload** and
  pick the file. A progress bar shows how far it got. See [companion.md](companion.md).
- **Web page**: open File Transfer on the X4 Pro and the address it shows in a browser on the same
  Wi-Fi, then upload there.
- **SD card**: copy the file to the card with a computer.

The file then opens from **Library** or **Browse Files** like a book, with its chapters, progress
and cover.

## What's planned

All in [Phase 1c of the roadmap](../ROADMAP.md):

1. A **Convert a comic** button in the companion app that opens the converter, and **Share to
   VaultInk** for converted files, uploading them straight to /Comics.
2. **Faster XTCH page turns**: pages are drawn pixel by pixel today, which is slow. The grayscale
   refresh will also be checked on the device.
3. A **CBZ reader** for comics copied without converting (JPEG and PNG pages; CBR and WebP won't
   open).
4. **Right-to-left** page order for manga, per book, and covers in the Library.
