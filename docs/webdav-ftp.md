# WebDAV and FTP servers

Released in 0.74.0. The parts without a network are tested on a computer; nothing here has run on a device
or against a real server so far (see [What is not tested](#what-is-not-tested)).

VaultInk can copy files between the SD card and a file server:

* **WebDAV** (Nextcloud, ownCloud, Apache, a NAS), over https or http.
* **FTP**, plain text only. FTP is **not encrypted**; the device says so before every connection.
* **SFTP** (files over SSH), on the X4 Pro: the same browser, with its own sign-in and server-key rules.
  It has its own page: [SSH terminal and SFTP](ssh.md).

This is the other direction of [File Transfer](webserver.md), where the device is the server. Here the
device is the client and the files live on your server.

## Setting up a server

**Home → Transfer & Remotes → Remote servers → Add Server.** (Settings → Connectivity → WebDAV and FTP
servers still opens the same list, as a shortcut.) Up to 6 servers. Each field is saved as
soon as you leave the keyboard.

| Field | What to enter |
| --- | --- |
| Server Name | Any name for the list. |
| Protocol | WebDAV or FTP. A tap switches between them. |
| Address | WebDAV: the folder's address, for example `https://cloud.example.org/remote.php/dav/files/you` (Nextcloud) or `https://nas.local/dav`. FTP: `server`, `server:port` or `server/folder`. |
| Username, Password | Typed on the device keyboard. Leave the user name empty for an anonymous FTP sign-in. |
| Check certificate | WebDAV over https only. On for a new server. |

The same fields are on the web settings page (Transfer & Remotes → Join a Network, then Settings in the
browser), under
**WebDAV and FTP servers**. The page never receives a stored password: it shows `(set)`, and leaving the
field empty keeps the password.

Not accepted in an address: a user name or password inside it (`https://user:pass@server`), an IPv6
address in brackets, `ftps://` and `sftp://`.

## Browsing, downloading, uploading

Open a server and tap **Connect and browse**. Wi-Fi is switched on at that moment, and off again when you
leave the screen (the device then restarts to the home screen, as it does after the OPDS browser).

* **Tap a folder** to open it. Back goes up one folder; Back in the first folder leaves.
* **Tap a file** to download it. It goes to the folder shown in the first row, **Save downloads to**
  (`/books` when that folder exists, else the top of the card). Tap that row to choose another folder.
* **Upload a file here** opens the card's file list; the file you pick is sent into the folder on the
  screen.
* **New folder** makes a folder on the server.

A progress bar shows during a transfer; Back or Cancel stops it.

Things to know:

* A download is written to `name.part` and renamed only when all of it has arrived, so a broken or
  cancelled download never replaces a file you already have. A finished download **replaces** a file of
  the same name in that folder without asking.
* An upload **replaces** a file of the same name on the server without asking. Over FTP it is sent as
  `name.part` and renamed at the end, and the `.part` is deleted when the upload fails. Over WebDAV the
  file's length is sent first, so a server has no whole file to keep from an upload that stopped.
* A list shows at most 500 entries (and at most 16 KB of names). When a folder has more, a line above
  the list says that it is cut. There is no paging and no search.
* Names the card cannot take are left out of the list: names with `/ \ : * ? " < > |` or control
  characters, names ending in a dot or a space, and names over 255 bytes.
* A broken download starts again from the beginning; there is no resume.
* Files are not deleted, renamed or moved on the server from the device.
* The files that hold saved passwords (`/.crosspoint/wifi.json`, `opds.json`, `koreader.json`,
  `remotes.json`) cannot be uploaded, and a download cannot be written over them.
* FTP: a symbolic link is listed as a file. A list from an older server (`LIST`) that prints a time
  instead of a year has no date. Passive mode only.

## How safe is it

**The connection**

* WebDAV over **https** is encrypted. With **Check certificate** on, the server's certificate is checked
  against the root certificates built into the firmware, or against your own
  `/.crosspoint/ca.pem` when that file is on the card (for a self-signed server; see
  [Certificate checks](reading-and-files.md#certificate-checks)). With the switch off the connection is
  still encrypted, but the device cannot tell your server from someone pretending to be it.
* WebDAV over **http** and **FTP** are **not encrypted**. Anyone on the same network can read the user
  name, the password and the files. The device shows this and asks **every time** you connect; Cancel
  is the preselected answer, and nothing is sent, not even Wi-Fi switched on, before you say yes.
* The password is only ever sent to the server you set up. A redirect to another server, or from https
  to http, is not followed. A redirect from http to https on the same server is followed, and uploads
  then use https too.
* Only Basic sign-in is supported for WebDAV (user name and password in each request). Use https.
  For Nextcloud with two-factor sign-in, make an app password.
* FTP: the data connection always goes to the server you signed in to, whatever address the server
  names in its answer.

**The stored password**

The servers are kept in `/.crosspoint/remotes.json` on the SD card, the same way the OPDS and KOReader
sync passwords are kept: the password is scrambled with the device's hardware address and written as
base64. That stops someone who only glances at the file. It is **not** encryption: anyone who holds the
card and the device (or knows its hardware address) can read the password. Use a password made for this
device (an app password), not your main one, and remove the server on your account if the card is lost.

**When it can be used**

Only from Settings, by your own tap. It cannot be reached while the lock screen is up, and not in USB
Drive mode (the computer owns the card then, and the device restarts when that mode ends). Nothing runs
in the background: no timer, no wake-up, no sync while the device sleeps.

## When it fails

| Message | What it means |
| --- | --- |
| The server address cannot be used | The address is not one of the forms above. |
| The server did not answer | No Wi-Fi route, wrong host or port, a timeout, or the certificate check failed. |
| Wrong user name or password, or no permission | The server answered 401 or 403 (FTP: 530). |
| Not found on the server | The folder or file is gone (WebDAV 404 or 409, FTP 550). |
| The server refused | Read-only folder, disk full, a name the server does not take. |
| The server points to another address; not followed | A redirect to another server, or from https to http. |
| The answer is not from a WebDAV or FTP server | A web page (a sign-in page, a plain web server) or another service on that port. |
| Unknown date: the certificate cannot be checked | The clock could not be set over the network. |
| ca.pem could not be read | `/.crosspoint/ca.pem` is there but empty or unreadable. |
| Memory error | Not enough free memory for an encrypted connection. Leave the screen and try again. |

Tap the message (or press Confirm) to try again; Back goes up one folder or leaves.

## What is not tested

* Nothing has been built for or run on a device: the screens, Wi-Fi, the encrypted connection, free
  memory during a transfer, speed, and what a server does with an upload that stops midway.
* No real server has been tried. The WebDAV list reader is tested with hand-written answers in the
  shape Nextcloud, Apache mod_dav and a minimal server send, not with answers captured from them. The
  FTP conversation is tested against a scripted in-memory server, not a real one.
* In the simulator the server pages and the "not encrypted" question work, but connecting always
  fails: the simulator has no network layer these clients can use.

## For developers

* `lib/RemoteFiles`: an entry, the list caps and the name rule (`safeName`), shared by both clients.
* `lib/WebDav`: addresses, percent coding, the redirect rule, request heads, RFC 1123 dates and the
  streaming reader for a `207 Multi-Status` answer (expat, element prefixes ignored).
* `lib/Ftp`: addresses, commands, reply / PASV / EPSV / LIST (Unix and DOS) / MLSD parsers and the
  conversation (`ftp::Session`) over two abstract channels.
* Host tests: `test/webdav` (with the firmware's expat and its flags) and `test/ftp`.
* `src/network/RemoteFsClient.h` is the interface the screens use; `WebDavClient.cpp` and
  `FtpClient.cpp` are the network sides. `src/RemoteServerStore` keeps the servers.
* Screens: `src/activities/remote/` (the server list, one server's page, the browser).
* Web API: `GET /api/remotes`, `POST /api/remotes`, `POST /api/remotes/delete`
  ([webserver-endpoints.md](webserver-endpoints.md)).
