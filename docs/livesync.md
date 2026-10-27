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

The firmware ships no certificate authority bundle, so by default the HTTPS connection is
encrypted but the server's certificate is **not checked**. The Server row then says "certificate
not checked". To check it, put your server's CA certificate (PEM) at
`/.vaultink/livesync/ca.pem`. A self-signed server works the same way.

Your notes stay end-to-end encrypted either way. An unchecked connection only puts the CouchDB
password at risk on a hostile network.

## Not supported (yet)

- Pushing changes back, and conflict copies: this needs editing, which is Phase 4.
- Compression, the V1/V3 encryption formats, object storage and peer-to-peer.
- Setup URIs split over QR codes.
- Background or automatic sync.
