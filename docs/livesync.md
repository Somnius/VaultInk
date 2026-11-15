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

## More than one vault (not released yet)

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
passed since the last try, it joins your last Wi-Fi network by itself (10 seconds at most), pulls
the changes, and opens what you were reading. It doesn't sync when:

- the last try was less than 6 or 24 hours ago. A try counts even when it failed, so a missing
  network costs one 10-second wait per interval, not one on every wake;
- the battery is under 20%;
- there's no saved network, or it isn't in reach (Wi-Fi goes off again). An open network with no
  password isn't saved, so it can't be used for this;
- the clock isn't trusted yet.

A failed sync on wake doesn't stop you: the device goes back to what you were reading, and the
reason is on the LiveSync screen under Sync now.

## Not supported (yet)

- Compression, the V1/V3 encryption formats, object storage and peer-to-peer.
- Setup URIs split over QR codes.
- Sync while the device is awake on a timer (only on wake, see above).
