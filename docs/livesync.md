# LiveSync

VaultInk keeps an Obsidian vault in step with [Self-hosted LiveSync](https://github.com/vrtmrz/obsidian-livesync)
over Wi-Fi. It pulls what changed in Obsidian, and from 0.28.0 it also sends back the notes you
changed on the device (ticked tasks, edits, new, renamed and deleted notes).

## What you need

- A CouchDB remote (not object storage or peer-to-peer).
- End-to-end encryption **on**, with the V2 algorithm. Path obfuscation can be on or off.
- Compression **off**.
- A LiveSync **Setup URI** and its passphrase. Export the URI from LiveSync's settings in Obsidian.

The remote may be at most generation 13 (the `obsydian_livesync_version` document). A newer
remote is refused with a message, before anything is written.

## Set it up

1. On the device, start **File Transfer** and open the device's page in a browser.
2. Go to **Settings**, paste the Setup URI into the **LiveSync** box and press **Save Setup URI**.
   The URI is stored on the SD card still encrypted. The passphrase never goes through the web page.
3. On the device, open **Settings → Vaults → LiveSync → Finish setup** and type the Setup URI's
   passphrase.
4. The device joins Wi-Fi, reads the database's encryption salt and unlocks the vault key. Unlocking
   takes 310,000 rounds of PBKDF2, so give it a few seconds. Then it deletes the pending URI.

The vault goes into `/Vaults/<database name>/`. A database name the SD card can't store becomes
`/Vaults/LiveSync`.

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

`/.vaultink/livesync/profile.json` holds the server address, the user name, the database name, the
encryption salt, and two secrets: the CouchDB password and the 32-byte vault key derived from your
passphrase. Both secrets are obfuscated with a key from the device's hardware (MAC) address. That
only stops a copy of the SD card from working on another device. **It is not encryption**: treat the
SD card like the key to that vault.

The passphrase itself is never stored. **Forget this setup** removes the profile, and the vault
files stay on the card.

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

What the device needs for this: the vault's id settings must be the defaults (path obfuscation
on, the original id scheme, case-insensitive names, xxhash64). The ids depend on the vault's
passphrase, which the device never keeps; at setup it keeps two values derived from it instead
(obfuscated like the password). **A LiveSync setup made before 0.28.0 doesn't have them**: the
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
  Services, also behind Cloudflare), Amazon Root CA 1 and USERTrust RSA (Sectigo). A server with
  a certificate from one of them works with nothing to set up. `scripts/gen_ca_bundle.sh`
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
