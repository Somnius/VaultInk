# Sending highlights to Readwise and Memos

Not released yet. Tested on a computer only (the request bodies, the batches, the error messages and the list of what was sent); it has not been compiled for the device, and no request has gone to a real Readwise account or Memos server. The section [What is not verified](#what-is-not-verified) lists every part that still needs a real test.

A book's clippings (its highlights) can be sent to:

- **Readwise** (readwise.io), with your Readwise access token.
- **Memos**, on a server of your own, with its address and an access token.

Joplin is not supported: its API is the desktop app's local Web Clipper service on your computer, not a server the reader can reach from anywhere, so it was cut.

The export to a note in your vault ([Highlights to a vault note](reading-and-files.md)) is unchanged and needs no network.

## Set up

**Settings → Network → Highlight Exports** has four rows:

| Row | What to enter |
| --- | --- |
| Readwise Access Token | Your Readwise access token (Readwise shows it at readwise.io/access_token; that address is from memory, not checked). |
| Memos Server Address | The address of your Memos server, for example `https://memos.example.com`. With no `https://` or `http://` in front, `https://` is used. |
| Memos Access Token | An access token made in your Memos account settings. |
| Memos: Check Certificate | Off: the connection to your server is encrypted but its certificate is not checked. On: it is checked, against `/.crosspoint/ca.pem` on the card when that file is there, else against the root certificates built into the firmware. The same rule as CrossPoint Sync. |

A token is long and awkward to type on the reader. The web settings page (File Transfer, then Settings in the browser) has the same four fields under **Highlight Exports**, and you can paste there.

A token that is stored is never shown again: the row says **Set** or **Not Set**, the keyboard opens empty, and the web page gets only "a token is set", never the token. Entering a new one replaces it; saving an empty one removes it.

## Send

In a book that has clippings:

- **More → Highlights to a Vault Note** asks where to, once Readwise or Memos is set up: the vault note, **Send to Readwise** or **Send to Memos**. With neither set up it writes the vault note at once, as before.
- **More → View Clippings**, tap a clipping: the same **Send to Readwise** / **Send to Memos** choices are under Open and Go to Passage. They send the book's clippings, not only the one you tapped.

Then:

1. The reader asks what to send: **Send N highlights** (the ones not sent to that destination before) or **Send all N again**. Cancel or Back goes back to the book and Wi-Fi is never turned on.
2. Wi-Fi comes on (the usual join screen when no saved network is in range).
3. The highlights go out, in reading order.
4. Wi-Fi is turned off and a message says **Sent N highlights**, or what went wrong.
5. OK goes back to the same page of the book (through the same quick restart that CrossPoint Sync and the Wikipedia lookup use).

Wi-Fi is on only for the job you started. Nothing is sent on a timer, at sleep or at wake-up.

It cannot start from the lock screen or in USB Drive mode: it is reached only from an open book, the lock screen comes before any book opens, and USB Drive mode closes the book.

### What was already sent

The reader keeps a small file per book, `/.vaultink/exports/<number>.sent`, with one mark per clipping and destination (a hash, not the text). Sending again skips what is marked; **Send all again** sends everything.

- The file is found by the book's path. After you move or rename the book the marks are not found, and the next send offers every clipping again.
- A damaged file is read as empty, so the worst case is a highlight sent twice.
- If the job stops half way, what was confirmed by the server stays marked. For Memos the marks are saved every 10 memos, so a flat battery in the middle can repeat up to 10.
- Deleting a book leaves its small marker file on the card.

## What is sent

### Readwise

`POST https://readwise.io/api/v2/highlights/`, header `Authorization: Token <your token>`, at most 20 highlights in one request (fewer when they are long: a request is closed once it passes about 12 KB).

```json
{"highlights":[{"text":"...","title":"Book title","author":"Author",
  "source_type":"vaultink","category":"books",
  "location":3,"location_type":"order",
  "highlighted_at":"2026-11-11T09:30:00+00:00"}]}
```

| Field | What the reader puts there |
| --- | --- |
| `text` | The clipping, up to 8191 characters (a clipping is 4 KB at most). |
| `title`, `author` | From the book. Left out when the book has none. |
| `source_type` | Always `vaultink`. |
| `category` | Always `books`. |
| `location`, `location_type` | The clipping's place in reading order among the book's clippings (1, 2, 3...), as `order`. Not a page number: a page on this reader depends on the font and margins, and the reader has no book-wide page for a clipping. |
| `highlighted_at` | When the clipping was taken, in UTC. Left out when the reader had no date at that moment. |
| `note`, `highlight_url` | Never sent: clippings have no notes or links. (The code can write them; nothing fills them.) |

The chapter title is not sent to Readwise (it has no field for it).

### Memos

`POST <your server>/api/v1/memos`, header `Authorization: Bearer <your token>`, one request per highlight.

```json
{"content":"> The quote\n\nBook title, Author (Chapter)\n\n#highlights #vaultink #Book_title #Author","visibility":"PRIVATE"}
```

The memo is the quote as a block quote, a line with the book, the author and the chapter, and a tag line: `#highlights`, `#vaultink`, and a tag made from the title and one from the author (letters and digits kept, anything else as `_`, 60 bytes at most). Memos are always created as private.

## Messages

| Message | What it means |
| --- | --- |
| Sent N highlights | The server accepted them. |
| Not set up | No token (or, for Memos, no usable address) in Settings. |
| Not connected: the server did not answer | No Wi-Fi, no route to the server, a timeout, or the certificate check failed. |
| The server refused the access token (401) | The token is wrong or was revoked (HTTP 401 or 403). |
| Too many requests (429). Try again in N s | The server's rate limit. The reader stops and does not retry by itself. |
| Address not found (404) | For Memos: the address is wrong, or the server has no `/api/v1/memos`. |
| The server did not accept the request | Another refusal (HTTP 4xx), or the server answered with a redirect. Redirects are not followed, so the token only goes to the address you set. |
| Server error | HTTP 5xx. |
| Date not set: can't check the certificate | The clock has no date and it could not be set from the network. Nothing is sent. |
| ca.pem could not be read | Check certificate is on and `/.crosspoint/ca.pem` is there but unreadable. Nothing is sent. |

When an error comes after some highlights went out, the message also says how many were sent; those are marked and are not sent again.

## Security and privacy

- **Anyone holding the SD card and the reader can get the tokens.** They are stored in `/.crosspoint/exports.json` like the other credentials: mixed with the device's hardware address and base64-encoded. That stops a casual read and makes the file useless in another reader, but it is not encryption. If the reader or the card is lost, revoke the tokens in Readwise and Memos.
- The tokens are never written to the log and never sent to the web settings page.
- Readwise is always checked against the root certificates built into the firmware; there is no switch, and `ca.pem` does not replace them.
- With a Memos address that starts with `http://`, the token and the highlights travel unencrypted. With `https://` and Check Certificate off, they are encrypted but the server's identity is not checked.
- The text of your highlights, the book title and the author go to the service you picked, with the firmware's name and version as the User-Agent. Nothing else is sent, and nothing is sent to anyone else.

## Limits

- EPUB books only (clippings exist only there).
- One book at a time; there is no "send everything on the card".
- One way only: nothing is read back from Readwise or Memos, and deleting a clipping on the reader does not delete it there.
- No retry by the reader after an error or a rate limit; start it again.
- Up to 2048 marks per book file; older marks are dropped first.

## What is not verified

Written with no network access, from memory of the public API documentation. None of this has been tried against a real account or server.

Readwise:

- The address `https://readwise.io/api/v2/highlights/` and the `Authorization: Token ...` header.
- The field names `text`, `title`, `author`, `source_type`, `category`, `note`, `location`, `location_type`, `highlighted_at`, `highlight_url`, and that `order` and `books` are accepted values.
- The length limits (8191, 511, 1024, 4095 characters).
- That a highlight with no title is accepted, and where Readwise files it.
- That the answer is HTTP 200 (the reader takes any 2xx as success).
- That a rate limit is answered with 429 and a `Retry-After` header in seconds.
- Whether Readwise drops a highlight it already has (so whether **Send all again** makes duplicates there).
- Which root certificate readwise.io's certificate chains to, and so whether one of the ten built-in roots covers it. If none does, every send ends in "Not connected".
- The limit of 20 highlights per request is our own choice, not Readwise's.

Memos:

- The address `/api/v1/memos` and the body `{"content", "visibility"}`. Older Memos versions used other addresses; which versions accept this one is not known.
- That `Authorization: Bearer <access token>` is accepted for this call, and that the value `PRIVATE` is right.
- That the answer is HTTP 200.
- How Memos reads tags with Greek letters or underscores.

On the device:

- Nothing was compiled for the device or run in the simulator.
- Free memory during the job (the request body for 20 long highlights is built in memory twice for a moment).
- The popups (their width in Greek), the join screen, the return to the same page.
- A real TLS handshake with readwise.io and the time it takes.
- A send interrupted by sleep, a dropped connection or a flat battery.
