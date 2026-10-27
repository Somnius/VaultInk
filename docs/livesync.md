# LiveSync (read only)

VaultInk can pull an Obsidian vault from [Self-hosted LiveSync](https://github.com/vrtmrz/obsidian-livesync)
over Wi-Fi. It only reads: nothing you change on the device goes back to the server, and the
server is never written to.

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

- Pushing changes back, and conflict copies: this needs editing, which is Phase 4.
- Compression, the V1/V3 encryption formats, object storage and peer-to-peer.
- Setup URIs split over QR codes.
- Sync while the device is awake on a timer (only on wake, see above).
