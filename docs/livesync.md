# LiveSync

VaultInk keeps an Obsidian vault in step with [Self-hosted LiveSync](https://github.com/vrtmrz/obsidian-livesync)
over Wi-Fi. It pulls what changed in Obsidian, and from 0.28.0 it also sends back the notes you
changed on the device (ticked tasks, edits, new, renamed and deleted notes).

## What you need

- A CouchDB remote (not object storage or peer-to-peer).
- End-to-end encryption **on**, with the V2 algorithm (the plugin's default; the Setup URI it copies leaves the default out, and the device reads that as V2). Path obfuscation can be on or off.
- Compression **off**.
- A LiveSync **Setup URI** and its passphrase. Export the URI from LiveSync's settings in Obsidian.

The remote may be at most generation 13 (the `obsydian_livesync_version` document). A newer
remote is refused with a message, before anything is written.

## Set it up

1. On the device, open **Transfer & Remotes**, join a network and open the device's page in a browser.
2. Go to **Settings**, paste the Setup URI into the **LiveSync** box and press **Save Setup URI**.
   The URI is stored on the SD card still encrypted. The passphrase never goes through the web page.
   **Vault folder** is optional: the name of the folder under `/Vaults` (see "More than one vault").
3. On the device, open **Settings → Vaults → LiveSync → Finish setup** and type the Setup URI's
   passphrase.
4. The device joins Wi-Fi, reads the database's encryption salt and unlocks the vault key. Unlocking
   takes 310,000 rounds of PBKDF2, so give it a few seconds. Then it deletes the pending URI.

The vault goes into `/Vaults/<vault folder>/` when you gave a folder name, else into
`/Vaults/<database name>/`. A database name the SD card can't store becomes `/Vaults/LiveSync`.

## More than one vault

New in 0.77.0. A Sync all of two vaults ran on a real X4 Pro on 2026-11-16; the rest of this section
is checked with computer tests only.

The reader keeps up to **four LiveSync profiles**. Each profile is one CouchDB database synced into
its own folder under `/Vaults`, with its own Sync on wake and Check certificate choice. So a
database called `vtm` can be the folder `/Vaults/Constantinople By Night` and `vtm-private-lef` the
folder `/Vaults/Lefteris-RPG`.

- **Adding one.** Copy that vault's Setup URI in Obsidian, paste it on the web page as above, type
  the **Vault folder** name and save. It becomes the next free profile (or the one you pick under
  **Profile**; picking a profile that is already set up replaces it once you finish on the device,
  and until then the old setup keeps working). Then finish it on the device with its passphrase.
  Pasting the same URI twice doesn't use two profiles.
- **The folder name.** Up to 64 letters (Greek and other non-Latin letters too), digits, spaces,
  `-`, `_` and `.`; no `/`, and no dot or space at either end. With no name, the folder is the
  database's name, as before. Setting the same database up again in the same profile keeps the
  folder it had.
- **Capitals don't make a new folder.** To the SD card `Notes` and `NOTES` are one folder, and so
  are `Σημειώσεις` and `ΣΗΜΕΙΏΣΕΙΣ`, or `Заметки` and `ЗАМЕТКИ`. The reader compares folder names
  the same way for English letters, Latin letters with accents (Latin-1 and Latin Extended-A:
  Western and Central European), Greek and Cyrillic. When you set a profile up again and type its
  folder with other capitals, the profile keeps the spelling it had, so its sync state and the
  notes not sent yet stay with it. A new profile's folder that is already on the card with other
  capitals takes the card's spelling. **Limit:** letters of other scripts with capitals (Armenian,
  Georgian, the rarer Latin letters after U+017F) are compared as typed, and so is a letter typed
  as one character against the same letter typed as a letter plus an accent: such names are not
  seen as the same folder. Use one spelling for those.
- **Two profiles never share a folder.** The web page refuses a folder another profile uses (also
  when only the capitals differ, see above), and so does Finish setup, with a message, when the
  database's name is already another profile's folder. Give that one a folder name.
- **The same vault posted twice.** A Setup URI for a database a profile already syncs (same
  server, database and user name), pasted again without picking that profile, first waits as a new
  profile. At Finish setup, once the passphrase has opened it, the reader sees it is the same
  vault and updates the existing profile instead: that profile keeps its folder (unless you typed
  another valid folder name), its sync state, Check certificate and Sync on wake, and the waiting
  profile is removed. A waiting setup in a profile you picked yourself replaces that profile, as
  before.
- **A folder that already has files.** The sync writes the server's files into it, and a file at
  the same path that you didn't change on the device is replaced by the server's. Files only on the
  card stay. So before the first sync into such a folder the reader asks: after the passphrase,
  Finish setup shows the folder and "This folder already has files. Notes with the same path are
  replaced by the server's version; files only on the card stay." with Cancel and Confirm. Cancel
  changes nothing and the Setup URI keeps waiting. The web page says the same when you save the
  Setup URI with a folder name that is already there (`folderExists` in the answer). It is never a
  refusal. The question is skipped when the same vault is set up again in the folder it already
  syncs.
- **Another database into a folder that was synced before.** Each folder's sync state carries the
  name of the vault it belongs to (server, database, user name; `livesync/identity`). When a setup
  is finished for a **different** vault than the one the folder was synced with (another database,
  the same database name on another server, another user, also after **Forget this setup**), the
  old state is moved aside to `/.vaultink/<id>/livesync-old-<n>/` before anything is synced: the
  list of notes changed on the device and not sent, the revisions, the checkpoint, the id map and
  the trash. So the first vault's unsent notes are never sent into the second database. The notes
  themselves are not touched by this step, but they are then ordinary files of the folder: the
  first sync replaces the ones the new database also has at the same path, **including notes you
  changed on the device and had not sent to the first database** (the old list of them is in
  `livesync-old-<n>/changed.txt`). Sync before you point a folder at another database. Sync state
  whose vault is not known (a profile forgotten on firmware before this) counts as another
  vault's. When it is the **same** vault, everything stays: setting it up again, or forgetting it
  and setting it up again, loses no unsent notes; only the next pull starts from the beginning.
  If the old state can't be moved or removed (a card error), the setup stops with a message and
  the Setup URI keeps waiting.
- **On the device.** With two or more profiles, **Settings → Vaults → LiveSync** lists them by
  folder name with their last result (or "Enter the Setup URI passphrase"), and **Sync all** under
  them. Tap one for its page: Finish setup, Sync now, Vault, Server, Check certificate, Sync on
  wake and Forget this setup. With one profile the screen opens on that page, as before.
- **Sync all, the Sync tile, sync on wake and the companion's sync request** go through the
  profiles one after the other, never two at once: one profile's connection is closed before the
  next one opens. Each profile keeps its own result line, and one that fails doesn't stop the
  next. The screen then says how many synced ("1 of 2 vaults synced"). On wake only the profiles
  whose own interval has passed are synced; the Sync tile and a sync request take all of them.
  If the memory left after one vault is too broken up for another connection (under 20,000 bytes
  in one piece, the limit the downloads use), the next profile is not tried and its result line
  says to sync it on its own. A profile whose file can't be read counts as one that failed
  ("Profile 2 could not be read from the SD card"), and the automatic sync does not report
  success when no profile could be read.
- **Each vault's state is separate.** The checkpoint, the id map, the trash and the list of notes
  changed on the device live in `/.vaultink/<id>/`, where `<id>` comes from the folder's path, so
  two folders never share them. Removing a profile leaves its folder and that state on the card,
  marked with the vault it belongs to.
- **Coming from one profile.** The first time the new firmware reads the profiles it moves
  `/.vaultink/livesync/profile.json` to `profiles/1.json` (one rename, nothing rewritten) and a
  waiting `setup-uri.txt` to `profiles/1.setup.json`. Your vault, folder and settings stay as they
  were. A move that fails (a card error) is tried again the next time the profiles are read, and
  written to the log once. Going back to an older firmware afterwards, that firmware won't find the profile: set it
  up again there.

How it was checked: computer tests of the profile files (the move from one profile and its retry,
adding and removing, folder names and their capitals in Latin, Greek and Cyrillic, the
shared-folder refusals, four at most, old files, the same vault posted twice, and the sync state
kept for the same vault and moved aside for another one) and of the web page's request, and
builds for the X4 Pro and the simulator. **Not run on a reader or in the simulator yet**: the list
and profile pages, the question about a folder that has files, Sync all with two real databases,
how much memory is left for the second vault after the first, and whether a real FAT or exFAT
card agrees with the reader's comparison of capitals for every letter above.

### A Setup URI with a passphrase you can type

`scripts/livesync_setup_uri.py` (on a computer, Python 3, nothing to install) opens a Setup URI
with its passphrase and writes the same settings under a new passphrase, for example a short one
for the reader's keyboard:

```bash
python3 scripts/livesync_setup_uri.py --uri-file old-uri.txt --out new-uri.txt
```

`--out` makes a **new** file that only you can read (mode 600). A file or a symbolic link that is
already there is refused, so the URI is never written into a file others can read or through a
link; `--force` replaces it (the link itself, not what it points at). Input that isn't UTF-8 text
ends with a one-line error.

It asks for the two passphrases without showing them (or reads them from files or the
environment: `--help`); they are never taken from the command line or printed. The new URI is in
the same format the plugin writes, and a test opens one of its URIs with the firmware's own code.
A shorter passphrase is easier to guess: keep the new URI as private as the old one.

## Sync

**Settings → Vaults → LiveSync → Sync now** pulls everything that changed since the last sync:

- New and edited files are written. Each one goes to a temporary file first, so a cut-off sync
  never leaves half a note.
- Files deleted on the server, and the old name of a renamed file, are moved to
  `/.vaultink/<id>/livesync/trash/`. Nothing is deleted outright.
- The sync remembers where it got to, but only after each batch of changes is fully applied. If
  Wi-Fi drops, the next sync redoes that batch.
- The result is shown on the LiveSync screen, for example "Synced: 12 updated, 0 removed" or the
  reason it stopped.

When you leave the LiveSync screen after using Wi-Fi, the device restarts quietly to free memory,
like the other sync screens do.

## Big files, limits and skipped files

Written 2026-11-15, not in a release yet. Tested on a computer only (a fake server); **not run on
a reader yet**.

A file is stored on the server as a list of pieces (chunks). The device asks for them a few at a
time, writes them to the card and drops them before asking for more, so a file's size does not
decide how much memory the sync needs.

- **Pieces per request.** At most 8, and fewer for a file with big pieces: the device takes the
  file's average piece (its size divided by the number of pieces), adds the base64 and encryption
  overhead (about 1.8 times), doubles it because pieces are uneven, and asks for as many as fit
  in 1.5 MB.
- **One answer from the server** can be 2 MB at most when it holds several pieces. A bigger one
  is dropped while it arrives ("Response over ... bytes" in the log) and asked again in halves.
- **A request for one piece alone** may get a longer answer (0.77.0): up to three quarters of the largest free PSRAM
  block (half, in the first build of that day), 6 MB at most and never less than the 2 MB above.
  A picture that LiveSync stored as one piece is over 2 MB of JSON and was skipped before. Such
  an answer is not parsed into a JSON document (a second copy of it): the buffer is taken once at
  that cap (in the first build: at the size the file's record gives, which was too small three
  times on a reader, see below), the piece is found in it, and it is decrypted and decoded inside
  that same buffer, so the answer is in memory once.
  With a free PSRAM block of 6.5 MB that is an answer of about 3.2 MB, a piece of about 1.8 MB of file
  data. A piece still bigger than that is skipped as before. A one-piece answer over 64 KB takes
  this same in-place way even under 2 MB: parsing a 1.5 MB answer into a JSON document ran out of
  memory three times on a reader. Tried on Lef's X4 Pro (2026-11-16, both vaults, about 2 minutes,
  nothing skipped, internal heap never under 90 KB).
- **One file** can be 32 MB at most.
- **A file that can't be fetched or decoded is skipped, and the sync goes on.** That covers: over
  32 MB, a name the SD card can't store (`?`, `*`, `:` and the like), an answer that is too big,
  cut short or not JSON, a server error for that file's pieces after a second try, a piece that
  is missing on the server or doesn't decrypt. The result line then ends with
  "N files skipped, see /.vaultink/<id>/livesync/skipped.txt".
- **An answer that is too big is asked for again in smaller parts** before the file is skipped:
  when a request for several pieces comes back over the 2 MB, or leaves no memory to read it, the
  device asks for half as many, down to one piece a request. What was already written of the file
  stays. Only a single piece that is too big on its own skips the file.
- **`skipped.txt`** has one line per skipped file: the path, the reason and the file's id on the
  server, separated by tabs (paths are cut at 300 bytes). It holds 2000 files at most.
- **Skipped files are tried again** at the start of every sync. One that works leaves the list.
- **The sync still stops** when the server itself can't be used: no network, a certificate that
  isn't accepted, a refused user name or password, a server that doesn't answer a small request
  either, a database with compression on, or an SD card that can't be written. Nothing is
  recorded as skipped then, and the next sync starts from the same place.
- **It also stops when skipping would hide a bigger problem** (the place it got to is kept, so no
  file is lost):
  - *More than 20 requests in a row fail* with none answered in between, while the server still
    answers a small request: "21 document requests in a row failed (...). The sync stopped;
    nothing was left out." That is a server, or a proxy in front of it, refusing these requests;
    before, every file was skipped and the sync said it was done. Answers that are too big for
    the device don't count here.
  - *The list of skipped files is full*: "More than 2000 files could not be fetched (see
    skipped.txt). The sync stopped here so none of the others is lost." Before, files past the
    2000 were counted but not listed, and the sync moved on past them for good.

### Sending a request, and the screen staying on

Written 2026-11-15 after a first pull on an X4 Pro (a 0.76.1 development build, CouchDB behind
nginx): after about 150 files, requests for documents got no answer for 30 to 36 seconds each.
nginx logged them as closed by the client (499) and they never reached CouchDB: nginx waits for
the whole request body before it passes a request on, so the body had not arrived whole. The
reader then went to sleep by its 10-minute timer with the result unread.

- **A request body is now written until every byte is taken.** The network code used to hand the
  body to the socket once and treat anything but "all of it" as a failure; over TLS a socket that
  was busy for a moment left the part already sent on the wire and the rest unsent. Now the rest
  is offered again until it is all in, and a socket that takes nothing for 30 seconds fails the
  request with the connection closed. The same code sends every POST and PUT of the firmware
  (LiveSync push, KOReader sync, highlight exports, clippings, Translate, plugins, WebDAV), and
  WebDAV uploads got the same loop.
- **What the log says** when a request gets no answer (tag `LSY`, level ERR), next to the line
  described below: `POST request body: sent 1024 of 4096 bytes` (the body did not go out whole),
  `POST: request sent whole (body 700 of 700 bytes), no answer came back` (it did, and the server
  closed the connection or stayed silent for 30 seconds), or `POST: could not connect`.
- **Not confirmed on the reader.** The fault in the sending code is real and is covered by
  computer tests (a fake socket that takes 512 bytes a time, one that is busy now and then, one
  that stalls). Whether it is what the reader hit is not known: these request bodies are under
  1 KB. The next sync on the reader will say which of the lines above it is.
- **A connection is used again only after an answer that was read to its end.** When the device
  stops reading an answer (over the 2 MB, no memory for it, a body that stops, a timeout), the
  server may still be sending the rest, and a request written behind it would wait for all of
  that. The connection is closed (TLS session and socket) and the next request opens a new one;
  the log says `POST: closed after an unfinished answer (N bytes of it read)` once for each
  (`POST: closed after a request with no answer` when nothing came back at all), and every
  connection made is one line, `LSY connect #N (new TLS session)`, so a log shows which requests
  shared a connection and that a closed one was replaced (since 2026-11-16). The shared
  HTTP code already closed such a connection; the sync now also closes it itself, and computer
  tests count every connection opened and closed over a whole sync with cut answers (fake
  sockets; none is left open, none is used after a cut answer, and a batch asked again in halves
  goes out on a new connection).
- **The end of a chunked answer has to be read, not only its JSON.** A chunked body ends with a
  "0" line and a blank line. The shared HTTP code called the body whole when the "0" line had
  come and the blank line after it had not (it waited the 30 seconds, then went on): the
  connection was kept, and the late bytes were then read as the start of the next answer. Now
  the body is whole only when that blank line was read; otherwise the connection is closed like
  any other cut answer. The same for an answer whose headers stop before their blank line. A
  change list that was not read to its end is not applied even if its JSON parses ("The change
  list was cut short."). Computer tests hold back the last 1 to 7 bytes of an answer: when they
  come late the device waits and the same connection goes on; when they never come the next
  request goes out on a new one. Whether a reader ever met this is **not known**: it needs the
  last two bytes to be 30 seconds late.
- **The message after a failed sync follows the cause.** "If your server uses its own
  certificate..." is shown only when the TLS handshake failed while the certificate is checked
  and nothing had been answered yet in that sync. A server that could not be reached says "The
  server could not be reached."; one that was connected and went silent says "The server did not
  answer (the connection timed out)." (before, both got the certificate hint, and the text said
  "Server answered -1"; it now says "No answer to a document request."). English and Greek.
- **What a reader's log showed on 2026-11-15, and what is not known.** Two pictures were skipped
  with "the server's answer was too big", each with no "asking again" line: the request was for
  one piece already, and that one piece's answer was over 2 MB. Halving cannot help there; such a
  picture stays skipped until answers are read in a streaming way (the 2 MB cap is not raised:
  the answer and its JSON copy are both in memory). Later in the same sync, requests got no
  answer for 30 seconds and then connections could not be made, each try taking 60 seconds. The
  code does not reuse a connection after a cut answer (see above), so that is not the cause.
  The memory report after the sync showed the internal memory had been down to 536 bytes free at
  some point (PSRAM: 4.9 MB free at its lowest); the Wi-Fi needs internal memory. Whether that
  low point is when the requests failed is **not known**. A failed request now logs
  `POST failed with N bytes of internal memory free (lowest so far M)`, which will say.
- **A reader's log on 2026-11-16 (internal memory fine, about 149 KB free at the failures), and
  what was changed.** Three pictures of one piece each were skipped as too big; each "skipped"
  line came about 6 seconds after its "Response over" line. The engine writes "skipped" only after
  a small GET was answered (200 or 404), so after each of the three cut answers a request did get
  its answer, which it could not have on the old connection. After the third, every request was
  sent whole and got no answer for 30 seconds. **Why is not known.** The line "connection closed
  after an unfinished answer" was not in that log; in the code of that day it is written whenever
  a status line had come and the body was not read to its end, and the computer tests take the
  same path (the HTTP client's body callback saying stop), so no path that skips the close was
  found. What changed, for the next run to show more: the HTTP client now stops both of its
  sockets on every close whatever it believes about them and counts the connections it makes; the
  sync logs each one and each close (lines above); and a request that only reads (GET, `_all_docs`)
  and ends without a whole answer is sent **once more on a new connection** before it counts as
  failed (`POST: asking again on a new connection`). Not after an answer over the cap, not when
  no connection could be made, and never for a note going up (the first try may have reached the
  database). A silent server now takes longer to be given up on: up to three waits of 30 seconds
  for one request instead of two. Computer tests: a new connection that takes a request and never
  answers, and a kept connection that goes silent; in both the next try is answered on another
  connection. **Not run on a reader.**
- **A reader's log of a "Sync all" of two vaults, later on 2026-11-16, and what was changed**
  (computer tests and one x4pro-dev build; **not run on a reader**).
  - *Internal memory.* About 146 KB were free before Wi-Fi, 94 KB with Wi-Fi on. In the first
    vault, requests that got no answer were logged with 12 KB free (lowest 7.8 KB), later with
    4 KB, and then the TLS handshake failed and no connection could be made. The PSRAM was fine.
    The second vault, right after, worked.
    **Found in the code:** during a sync malloc() gave blocks *over* 64 bytes from the PSRAM, and
    its rule is "up to and including": a block of exactly 64 bytes stays internal. The sync keeps
    two tables for the whole vault (file id to path, file id to revision), and on this 32-bit
    board one entry of such a table is exactly 64 bytes, its path and its revision text are under
    64 too: about 220 bytes of internal memory for every file the vault has (measured with a
    32-bit build on a computer), taken when the sync starts and given back when it ends. The
    text of ids and revisions in the server's JSON answers is in blocks of that size as well.
    **Not known:** how many files that vault has, so whether this is all of the 80 KB that went
    missing. (640 files would be 140 KB.) **Changed:** during a sync every block comes from the
    PSRAM, whatever its size. The log now has a line with the free memory at every step
    (`LSY MEM state loaded, 612 files known, ...: internal N free, lowest N, largest N; PSRAM N
    free, largest N`, then for every page of the change list, every 10 file records, every
    connection made or closed and the end of the session), so the next run shows where it falls,
    if it still does.
  - *Requests without an answer.* **Still not explained.** They came when the internal memory
    was low this time, and the Wi-Fi needs it to receive; but a log earlier that day had them
    with the memory fine.
  - *Three files skipped with "not enough memory for the server's answer"* (3.6, 3.1 and 3.1 MB
    read). Each was a request for one piece, not a batch (a batch stops at 2 MB). The buffer had
    been taken at the size the file's record suggested, the piece was longer, and a buffer that
    holds 3 MB cannot grow: a second block of that size and a quarter more was not free. Why
    the pieces were longer than the record suggested is **not known** (a file in uneven pieces
    would do it; the log does not say how many pieces they had). **Changed:** the buffer for one
    piece is taken once at the cap, and the cap is three quarters of the largest free PSRAM
    block (6 MB at most) instead of half. An answer over that is still skipped ("too big").
  - *Waiting.* One request without an answer cost 168 seconds: 30 seconds each for the kept
    connection, a new one, and a third, the whole request once more by the sync, and connections
    that could not be made took 48 to 60 seconds a try. **Changed:** the wait for the start of
    an answer (the status line) and the wait for a connection are 10 seconds each (a body that
    has started may still pause for 30); a request is written twice at most, whoever sends it
    the second time; the sync itself no longer repeats a request that got no answer; after a
    request with no answer the next one gets a single try; and a server that cannot be reached
    is not tried a second time with TLS 1.2. A server that goes silent now stops the sync after
    30 seconds (three waits of 10), one that cannot be reached after 20.
  - *The screen.* The progress bar was drawn for every file, about once a second, each a refresh
    of the e-ink screen. It is now drawn every 2.5 seconds at most.
- **A sync keeps the reader awake.** The sync runs in one go, and the sleep timer was not fed
  while it ran, so it fired the moment a sync longer than the timeout ended. The end of any job
  that held the reader for 5 seconds or more (a sync, a PDF conversion, a download) now counts as
  a key press for the timer: the full timeout starts again from there.

Every answer the device could not use is written to the log (tag `LSY`, level ERR): the kind of
request (`version`, `parameters`, `changes`, `docs`, `chunks`), the HTTP status, the
Content-Type, the Content-Length or `chunked`, the bytes read, whether the body ended early, the
JSON parser's error by name (`NoMemory`, `IncompleteInput`, `InvalidInput`), the first 80 bytes
of the body when it isn't a memory problem, and the file being fetched. A long line is split
into pieces of 200 characters. The server address, the user name and the password are never
logged.

What was wrong before, as found on a computer: the JSON library kept the length of a string in 2
bytes, so any answer with a piece longer than 65,535 characters failed with "The server's answer
could not be read (too big or not JSON)" and the whole sync stopped there. A picture can have
such pieces. An X4 Pro stopped with that message on a picture, after an answer of 228 kB; that
this was the reason there is not confirmed until the new build runs on it. The device also kept
every piece of a file in memory until the file was written.

## What is stored on the device

`/.vaultink/livesync/profiles/1.json` to `4.json` (one per profile; before profiles it was
`/.vaultink/livesync/profile.json`) each hold the server address, the user name, the database name, the
vault folder, the encryption salt, and two secrets: the CouchDB password and the 32-byte vault key derived from your
passphrase. Both secrets are obfuscated with a key from the device's hardware (MAC) address. That
only stops a copy of the SD card from working on another device. **It is not encryption**: treat the
SD card like the key to that vault.

The passphrase itself is never stored. **Forget this setup** removes that profile (and a Setup URI
waiting for it), and the vault files stay on the card, with the folder's sync state (see "Another
database into a folder that was synced before"). A Setup URI that waits for its passphrase is
in `profiles/<n>.setup.json`, still encrypted, with the folder name you gave.

## Sending your changes (0.28.0)

Every **Sync now** (and the Sync tile, and sync on wake) first sends the notes you changed on the
device, then pulls. The result line says how many were sent ("Sent 2 notes from here").

- A note is sent **on top of the version the device last pulled**. If nobody else changed it,
  it simply becomes the new version in Obsidian.
- If the note also changed in Obsidian since the device last pulled, nothing is overwritten:
  the pull saves Obsidian's version next to yours as `Note (conflict 2026-10-28).md`, and then
  your version is sent as the note and the conflict copy is sent too. In Obsidian you end up
  with both texts, and you keep the one you want.
- A note deleted on the device is marked deleted on the server, like Obsidian does. A renamed
  note is sent as the old one deleted and the new one created.
- A note leaves the device's list of changes only once the server has it. If Wi-Fi drops, the
  next sync sends it again.
- Notes over 256 KB, and names with letters VaultInk can't lowercase the way Obsidian does
  (most Latin, Greek and Cyrillic names are fine), stay on the list and are not sent.

What the device needs for this: path obfuscation on, case-insensitive names, xxhash64, and one
of two id schemes. The original scheme (the default): the ids depend on the vault's passphrase,
which the device never keeps; at setup it keeps two values derived from it instead (obfuscated
like the password). Id scheme 1 (`idDerivationVersion` 1, with its 64-hex `idDerivationKey` in
the Setup URI): the device keeps that key (obfuscated the same way) and makes the ids as the
plugin does; path normalisation 0 or 1 (NFC) are both understood. The result line says "(ids v1)"
after "Sent N notes". Any other combination isn't sent. **A LiveSync setup made before 0.28.0 doesn't have them**: the
result line then says changes aren't sent. Paste the Setup URI on the device's web page again
and finish setup once more.

How it was checked: every chunk and note id in a database the official LiveSync CLI wrote
matches what the device computes, and notes the device sent (new, edited, deleted, a conflict)
read back byte for byte with the CLI. Not yet against a real Obsidian install.

## Certificates

From 0.21.0 the server's certificate is **checked by default**, and so is its name: the
certificate has to be for the host in your server address.

- **Built-in roots.** The firmware carries seven root certificates (from the Mozilla store):
  ISRG Root X1 and X2 (Let's Encrypt), DigiCert Global Root G2, GTS Root R1 and R4 (Google Trust
  Services, also behind Cloudflare), Amazon Root CA 1 and USERTrust RSA (Sectigo). The next
  release adds three (USERTrust ECC, Sectigo Root E46, GlobalSign Root R3), ten in all. A server
  with a certificate from one of them works with nothing to set up. `scripts/gen_ca_bundle.sh`
  rebuilds the list.
- **Your own CA.** For a self-signed server or a private CA, put its certificate (PEM) at
  `/.vaultink/livesync/ca.pem`. It replaces the built-in roots. If that file is there but can't
  be read, the sync stops; it never falls back to the built-in roots.
- **Turning the check off.** The LiveSync screen has a **Check certificate** row. Tap it to turn
  the check off: the connection stays encrypted, but the server isn't checked, and the Server
  row says "certificate not checked". Only do this on a network you trust.
- **The clock.** Certificates have dates, so the device needs the real time. If its clock isn't
  trusted yet (the X4 Pro keeps time in its RTC), it asks a time server first.
- **An IP address as the server.** Then the certificate must list that IP address. Most
  self-signed certificates for a LAN address don't, so use the host's name, or turn the check
  off. (I haven't been able to test an IP address on the device yet.)

If a sync fails before the server answers while the check is on, the result line says to try
`ca.pem` or turning the check off. The same message also shows when the network is simply down.

A setup made with 0.19.0 or 0.20.0 has no saved choice, and starts with the check on.

## Quick settings tile and sync on wake

Once LiveSync is set up, quick settings (swipe down from the top) shows a **Sync** tile. It runs
Sync now right away. When it's done, the device restarts quietly, as after every sync, and opens
the book or note you were reading again (or the home screen). If the sync fails, the LiveSync
screen stays open so you can read why.

**Sync on wake** is off until you turn it on, on the LiveSync screen. Tap the row to step through
Off, every 6 hours and every 24 hours. When the device wakes (or starts) and the chosen time has
passed since the last try, it scans once (5 seconds at most) and joins a saved Wi-Fi network that is in range by itself
(the strongest first, 10 seconds for each, 20 seconds in all with the scan; after a scan that
lists nothing, the last connected network and up to two more are tried anyway), pulls the changes, and opens what you
were reading. It never shows the Wi-Fi screen or asks anything: with no saved network in range it
skips the sync with one line in the log. (The scan and the choice among saved networks are
unreleased: computer tests and an x4pro-dev build, not run on a reader.) It doesn't sync when:

- the last try was less than 6 or 24 hours ago. A try counts even when it failed, so a missing
  network costs one scan per interval, not one on every wake;
- the battery is under 20%;
- there's no saved network, or it isn't in reach (Wi-Fi goes off again). An open network with no
  password isn't saved, so it can't be used for this;
- the clock isn't trusted yet.

A failed sync on wake doesn't stop you: the device goes back to what you were reading, and the
reason is on the LiveSync screen under Sync now.

Sync now, Sync all, Finish setup and the Sync tile join Wi-Fi through the usual Wi-Fi screen: a
saved network in range is joined without asking, and with none in range you get the network list
(see [the web server guide](webserver.md#join-network-mode)). If you go back from it without a
connection, the LiveSync screen says "Not synced: no Wi-Fi connection."

## Not supported (yet)

- Compression, the V1/V3 encryption formats, object storage and peer-to-peer.
- Setup URIs split over QR codes.
- Sync while the device is awake on a timer (only on wake, see above).
