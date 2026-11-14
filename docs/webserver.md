# Web Server Guide

This guide explains how to use CrossPoint Reader's built-in web server for file
transfer, device settings, Wi-Fi/OPDS management, and SD-card font management.

## Overview

The web server is available while the device is in **File Transfer** or
**Calibre Wireless** mode. It can:

- Upload, download, rename, move, and delete files on the SD card
- Create folders
- Edit many device settings from a browser
- Manage saved Wi-Fi networks, OPDS servers and (0.74.0) [WebDAV and FTP servers](webdav-ftp.md)
- Upload and delete `.cpfont` SD-card font families
- Accept WebDAV clients and Calibre wireless uploads

The server does not require authentication. Use it only on trusted private
networks or in hotspot mode when you control who is connected.

## Starting File Transfer

1. From the Home screen, select **Transfer & Remotes** (this Home row was called File Transfer
   before; the Wi-Fi screen with the address and QR codes still has the title File Transfer).
2. Choose one of the available modes:

| Mode | Use when |
|------|----------|
| **Join Network** | You want the reader to join an existing Wi-Fi network. |
| **Calibre Wireless** | You want to receive books from the CrossPoint Calibre plugin workflow. |
| **Create Hotspot** | You want the reader to create its own open Wi-Fi network. |
| **USB Drive** | You want to copy files over the USB-C cable, with no Wi-Fi. |
| **Remote servers** | You want to fetch or send files from the reader to your own WebDAV, FTP or SFTP server. No web server runs here: see [webdav-ftp.md](webdav-ftp.md). |
| **SSH terminal** | X4 Pro builds only: a shell on your own server. See [ssh.md](ssh.md). |

The last two rows open the lists of saved servers; Back from a list returns to this screen.
Settings → Connectivity → WebDAV and FTP servers and SSH servers open the same lists, as shortcuts.

## USB Drive

**Transfer & Remotes → USB Drive** ("Manage the SD card from your computer") makes the reader show up on your computer as a drive, over the USB-C cable. No Wi-Fi, router or password is involved, so it is a good way to copy large folders, a whole library or a vault. I have not timed it against Wi-Fi.

1. Open **Transfer & Remotes → USB Drive**. The reader says it is preparing the drive.
2. Plug the reader into your computer with the USB-C cable. The screen asks you to connect it. Connecting can take up to 30 seconds. If no computer connects within 5 minutes, the reader goes back Home.
3. The SD card appears as a drive. Copy files and folders as with any USB stick.
4. **Eject the drive on your computer** before you unplug, so nothing is cut off while it is writing. Ejecting, or disconnecting the cable, brings the reader back Home. If something goes wrong the screen says so and tells you to disconnect the cable.

While it is on, the reader does not use the SD card: the computer has it. Do not unplug the cable while files are copying. The web page (Wi-Fi) and USB Drive are two ways to the same SD card.

## Join Network Mode

1. Select **Join Network**.
2. If you have saved Wi-Fi credentials, CrossPoint first tries the last
   connected network, then other visible saved networks in signal-strength
   order. Press **Back** to cancel or **Confirm** to stop auto-connect and show
   the network list.
3. If the network list is shown, pick a 2.4 GHz Wi-Fi network from the scan
   results.
4. Enter the password if prompted.
5. Save credentials if you want the reader to reconnect automatically next time.

After connection, the reader shows:

- The connected SSID
- A QR code for the web URL
- The direct IP URL, for example `http://192.168.1.102/`
- The mDNS fallback URL, usually `http://vaultink.local/`

Use either URL from a phone, tablet, or computer on the same network.

## Create Hotspot Mode

1. Select **Create Hotspot**.
2. Connect your phone or computer to the open Wi-Fi network:

```text
CrossPoint-Reader
```

3. Open the URL shown on the reader. `http://vaultink.local/` is preferred
   when supported; the fallback IP is typically `http://192.168.4.1/`.

The reader displays one QR code for joining the hotspot and another QR code for
opening the web interface.

## Calibre Wireless Mode

Calibre Wireless starts the same web server in station mode, then displays setup
instructions and upload progress on the reader. Use this mode with the
CrossPoint Calibre plugin or other clients that speak the documented WebSocket
upload protocol.

For Calibre OPDS browsing, add `/opds` to the catalog URL when configuring an
OPDS server.

## Web Interface

The browser UI has four primary pages.

### Home

The Home page shows firmware status, network mode, IP address, device type,
uptime, and free heap.

### File Manager

The File Manager page can:

- Browse SD-card folders
- Upload files, using WebSocket upload when available and HTTP upload as a fallback
- Create folders
- Download files
- Rename files
- Move files into existing folders
- Delete one or more selected files or empty folders, hidden ones included
  (shown when **Show Hidden Files** is on)
- Import an Obsidian vault from a `.zip` (**Import Vault (.zip)**, see below)

An upload refuses a name that already exists in that folder ("File already
exists"); delete or rename the old file first. When EPUB files are moved,
renamed, or deleted through the web server, the matching book cache is cleared
so stale metadata is not reused.

#### Import Vault (.zip)

Zip a vault on the computer (in Obsidian's folder, or any `.zip` of it) and pick
it with **Import Vault (.zip)**. The browser unpacks it (JSZip, already on the
page) and sends the files one by one with the normal folder and upload calls,
into `/Vaults/<name>/`:

- `<name>` is the zip's single top folder (`My Vault/…`), or else the zip's file
  name. If `/Vaults/<name>` already exists, the import goes to `<name> (2)`,
  `<name> (3)`… and never mixes into the old one.
- Dot files and folders (`.obsidian`, `.trash`, `.DS_Store`), `__MACOSX`, and
  entries with `..` or characters the SD card can't hold are skipped. A folder
  right under `/Vaults` counts as a vault without `.obsidian`, so links, embeds
  and search work as usual.
- A line at the bottom of the page counts the files; keep the page open until it
  says "Imported". Large vaults take a while: every file is its own upload.

The demo vault (`demo-vault/VaultInk Demo` in the repository, also attached to
the releases as `VaultInk-Demo.zip`) is a good first test.

### Settings

The Settings page exposes many firmware settings in the browser. It also has
cards for:

- Saved Wi-Fi networks
- OPDS servers
- WebDAV and FTP servers (0.74.0)

Passwords are accepted when adding or editing entries, but saved passwords are
not returned by the API.

### Fonts

The Fonts page lists installed SD-card font families and lets you upload
`.cpfont` files. Upload files from one font family at a time. The server validates
the font family name, filename, and `.cpfont` magic bytes before accepting the
upload.

Installed fonts appear in **Settings > Reader > Font Family** after the font
registry refreshes.

On devices with external RAM enabled in CrossPoint, you can use TTF/OTF/TTC
fonts directly.
Copy them into `/fonts/` or `/.fonts/` on the SD card. The Fonts page and
`/api/fonts/upload` accept `.cpfont` files only. See the
[SD card font guide](./sd-card-fonts.md) for the direct font folder layout.
To remove a loose direct font, delete its file from the SD card. The Fonts page
can delete a family stored in a subfolder.

## Command Line Use

Power users can use `curl`, WebDAV clients, or WebSocket clients while the web
server is running.

Endpoint details are documented in [webserver-endpoints.md](./webserver-endpoints.md).

## Security Notes

- The HTTP server runs on port 80.
- The WebSocket upload server runs on port 81.
- There is no authentication.
- Anyone on the same network can access the web interface while it is running.
- The server stops when you exit File Transfer or Calibre Wireless mode.
- Hotspot mode creates an open network for connectivity fallback; disconnect when done.

## Tips

1. Use **Create Hotspot** when no trusted network is available.
2. Prefer `vaultink.local` when available, but keep the displayed IP address as a fallback.
3. Move closer to the router if upload progress stalls in Join Network mode.
4. Upload custom fonts through the Fonts page or copy them to `/.fonts/` or `/fonts/` on the SD card.
5. Exit File Transfer mode when finished to conserve battery.

## Related Documentation

- [README](../README.md)
- [Webserver Endpoints](./webserver-endpoints.md)
- [SD Card Fonts](./sd-card-fonts.md)
- [Troubleshooting](./troubleshooting.md)
