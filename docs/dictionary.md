# Dictionary

Look up words and phrases while reading an EPUB using an offline StarDict dictionary stored on the SD card.

## Supported Format

The reader supports **StarDict** dictionaries. When searching for dictionaries online, look for "StarDict format" or files with `.dict`, `.idx`, and `.ifo` extensions.

A dictionary folder must contain:

- `.idx` — word index (required, **must be uncompressed** — a `.idx.gz` will not work; decompress it on your computer with `gzip -d` first)
- `.dict` or `.dict.dz` — definition data (`.dict.dz` is supported as-is; entries are decompressed on the fly during lookup)
- `.syn` — synonym index (optional; maps alternate spellings and irregular forms to their headword)
- `.ifo` — metadata (optional)

Not supported: dictionaries with 64-bit index offsets (`idxoffsetbits=64` in the `.ifo` — rare, and rejected with an error).

> [!NOTE]  
> If you use .dict.dz and Crosspoint failed to load your dictionary with error message "Not enough memory", try unzipping it.

## Setting Up a Dictionary

1. Copy your dictionary folder(s) to `/dictionaries/` on the SD card — one dictionary per folder, e.g. `/dictionaries/webster/webster.idx` + `webster.dict.dz`. A hidden `/.dictionaries/` folder (dot-prefixed) works the same way, for keeping it out of the file browser.
2. Open **Settings → Reader → Dictionary** on the device.
3. Select a dictionary from the list, or **None** to disable lookups.

The Dictionary setting only appears when at least one usable dictionary folder exists. Folders containing more than one dictionary (multiple `.idx` stems) are skipped as ambiguous.

## Looking Up Words and Phrases

Two ways to start a lookup while reading:

- Open the reader menu (**Confirm**) and choose **Look Up**.
- Or set **Settings → Controls → Long-press Menu** to "Dictionary", then hold **Confirm** (~0.4s) on the reading page.

### Selecting Text

**On button-driven devices:**

1. A selection cursor appears on a word. Use **Left/Right** to navigate between words in reading order, and the side **Up/Down** buttons to jump between lines or flip across nearby pages.
2. Press **Confirm** (**Select**) to anchor the start of the selection.
3. **Single word:** Press **Confirm** again immediately (**Look Up**) to look up the highlighted word.
4. **Phrase / multiple words:** Use **Left/Right** or **Up/Down** to extend the highlighted selection across words, lines, or pages. Then press **Confirm** (**Look Up**).
5. Press **Back** at any time to cancel and return to reading.

**On touchscreen devices:**

- Tap or drag the selection handles across words to highlight a word or phrase, then tap **Look Up**.

On the very first lookup with a dictionary (and again whenever the `.idx` or `.syn` source file changes), the reader shows *"Indexing dictionary…"* while it builds small sidecar files next to them — a `.qidx` for the word index, and a `.sidx` when a `.syn` synonym file is present. Each sidecar is rebuilt independently, only when its own source changes. This takes a few seconds for large dictionaries and makes all subsequent lookups fast. The sidecars can be deleted safely at any time — they will simply be rebuilt.

### How Lookup Works

1. **Direct match** — the word or phrase is searched as-is (case-insensitive) in the dictionary index. Leading and trailing punctuation and whitespace are stripped automatically. Multi-word phrases match dictionary entries containing spaces (such as idioms, phrasal verbs, or compound terms). An index may list the same headword more than once (one entry per part of speech, for example); every entry it lists is shown, each labelled with its headword.
2. **Synonyms** — on a miss, if the dictionary ships a `.syn` file, alternate spellings and irregular forms recorded there are resolved to their headword (e.g. `oxen` → `ox`, `colour` → `color`). All the headwords a synonym points at are shown. This step is skipped if the `.sidx` sidecar could not be built (e.g. transient low memory during indexing); the dictionary otherwise stays usable, and the build is retried the next time it is opened.
3. **Stemming** — still no match: common English word forms are retried automatically: possessives and plurals (`dogs` → `dog`, `stories` → `story`) and verb endings (`walked` → `walk`, `running` → `run`, `making` → `make`).
4. **Not found** — a short popup appears and you return to word selection.

## The Definition Screen

When a word is found, the definition screen shows the matched headword at the top and the definition text below, with a page counter for long definitions. A word that maps to several definitions shows all of them one after another, each labelled with its headword; if they do not all fit, the ones that fit are shown. If the first definition fills the size limit, it is shown alone without an extra label.

HTML dictionaries (`sametypesequence=h`) and XDXF dictionaries (`sametypesequence=x`) use the EPUB text-layout engine for supported formatting such as headings, bold, italics, lists, and line breaks. Unrecognized tags keep their text without special formatting. Images and CSS styling are ignored. Definitions that are too large or cannot be parsed or laid out within the available memory fall back to plain text.

Definitions are drawn in your reading font. Dictionaries may use IPA symbols (e.g. `ˈ ː ɹ ɪ ʊ`…) as pronunciation guide, which built-in reader fonts do not include, so they are shown as `�`. To see them, install an [SD card font](./sd-card-fonts.md) that covers IPA and select it as the reading font.

- **Left/Right** or side **Up/Down** — previous / next page
- **Confirm** — select words or a phrase inside the definition, then choose **Look Up**
- **Long press on text** — select the touched word on touchscreen devices when touch reader controls are enabled
- **Back during selection** — cancel selection
- **Back while reading a definition** — return to the previous definition, or to book word selection at the first definition
- **Long Back while reading a definition** — hold **Back** for at least one second, then release to close all definitions and return directly to reading
- **Long press the header Back arrow** — close all definitions and return directly to reading on touchscreen devices, even when touch reader controls are disabled

During definition word selection, press **Back** to cancel selection first, then hold **Back** or long-press the header Back arrow to leave the lookup chain.

Nested lookups use the same dictionary. Both formatted and plain-text definitions support selection.
Returning to a previous definition restores its page and remembers its selection for the next press of **Confirm**.

The reader keeps up to 16 previous definitions. At this limit, another lookup shows a brief message and leaves the current definition open.
A failed lookup reloads the current definition and keeps its page and selection.

The current definition is released before loading another entry, so two definitions do not need to fit in memory together.
Returning to a previous definition reads it from the SD card again.
If a previous definition cannot be read or its position restored, the definition screen closes and returns to book word selection.
The screen also closes if a failed forward lookup cannot reload the current definition.


## Wikipedia when the dictionary has no entry

Not released yet: tested on a computer only, not on a reader.

When the dictionary has no entry for what you picked, the **Not found** message asks
**Look up on Wikipedia?** and stays for about eight seconds.

- Press **Confirm**, or tap the message, to look it up. **Back**, a tap anywhere else, or waiting
  dismisses it.
- Wi-Fi turns on only after that press. If no network is saved yet, the usual Wi-Fi screen opens
  so you can pick one; backing out of it returns to the book.
- The answer is the short summary at the top of the article, as plain text, on the same screen as
  a dictionary definition: the title and the text, no pictures and no links. Wi-Fi is off again
  before you read it.
- **Back** returns to the book. The reader restarts quietly into the book at your page, as it does
  after KOReader sync; this clears the memory the Wi-Fi session used.

Which Wikipedia: the book's own language (its `dc:language`, so a Greek book asks
el.wikipedia.org), else the language of the menus, else English.

What you may see instead of an article:

| Message | What happened |
| --- | --- |
| No Wikipedia article | There is no article with exactly that title. Titles are case-sensitive after the first letter, so a name typed in lower case in the book may not match. |
| Several articles match | The title is a list of meanings (a disambiguation page). Select more words, for example the full name. |
| Couldn't reach Wikipedia | No connection, or the certificate check failed. |
| Date not set: can't check Wikipedia | The clock has no date and it could not be set from the network, so the certificate cannot be checked. Nothing is sent. |
| Couldn't read the answer | The answer was not an article summary (a sign-in page of a public network, for example). |
| Not Enough Memory | Too little free memory for a secure connection. Try again after the book reopens. |

Good to know:

- The connection to Wikipedia is always checked against the certificates built into the firmware.
  There is no switch for it.
- At most 8 KB of an answer is read. A very long summary is cut there and ends with "…".
- The words you looked up go to Wikipedia (Wikimedia), with the firmware's name and version as the
  client. Nothing else is sent, and nothing is sent until you press Confirm.
- It needs a dictionary to be set: the offer is part of the dictionary's Not found message. It is
  in EPUB books only, like the dictionary.
- It can't be reached from the lock screen or in USB Drive mode: both come before a book is open.
- With the translation lookup below, the same message offers both: it reads **Not found. Wikipedia or
  Translate?**, Confirm or a tap on its left half is Wikipedia.


## Translate when the dictionary has no entry

Not released yet: tested on a computer only (the addresses, the request, reading the answers and the error
messages). It has not been compiled for the reader or tried on one, and the reader's own connection to the
service has never been made.

The same **Not found** message offers a translation next to Wikipedia: it reads
**Not found. Wikipedia or Translate?**

- **Confirm**, or a tap on the left half of the message, is Wikipedia. The **Next** button (Right), or a tap on
  the right half, is Translate. **Back**, a tap anywhere else, or waiting about eight seconds dismisses it.
- The first time, a question says what will be sent and where. **Cancel** goes back to the book: nothing is
  sent and Wi-Fi is not turned on. It is asked once for MyMemory and once for a server of your own (again if
  you change the server's address).
- Then it works like the Wikipedia lookup: Wi-Fi comes on, the translation shows on the definition screen with
  the name of the service and the two languages under it, for example `MyMemory (en > el)`, Wi-Fi is off again
  before you read it, and **Back** returns to the book through the same quiet restart.

**What is sent: only the word or phrase you selected and the two language codes.** Not the book's title, not
the sentence around the selection, no account, no e-mail address. The request also carries the firmware's name
and version as the client, like the Wikipedia lookup. The service sees your network address, as any website
does.

Which languages: from the book's own language (its `dc:language`) into the language of the menus. A book in
the menu language is translated into English instead. An English book under English menus has nothing to
translate into, and the reader says so without connecting.

### Where it goes

**MyMemory** (translated.net), with no key and no setup, unless you set a server of your own:

- The address is `https://api.mymemory.translated.net/get?q=<selection>&langpair=<from>|<to>`.
- Its published limits, read on 2026-11-13 at <https://mymemory.translated.net/doc/usagelimits.php> and
  <https://mymemory.translated.net/doc/spec.php>: "Free, anonymous usage is limited to 5000 chars/day", and one
  request is "Max 500 bytes". A selection over 500 bytes is refused on the reader before anything is sent (500
  bytes is about 500 Latin letters or 250 Greek ones). The optional e-mail parameter that raises the daily
  limit is not sent.
- Its terms (<https://mymemory.translated.net/terms-and-conditions>) allow "Automated and systematic API access
  by machines" and forbid, among other things, "translating more than one paragraph at once".
- MyMemory is a translation memory with machine translation behind it. A single word often comes back as
  another meaning than the one in your book, or unchanged: the test query "book" from English to Greek came back
  as "book".
- It needs the book's language. A book that does not say its language cannot be sent to MyMemory, because its
  documentation describes no way to detect the language.
- The connection is always checked against the certificates built into the firmware (its chain ends at Amazon
  Root CA 1, which is one of them). There is no switch for it.

**A LibreTranslate server of your own**, in **Settings → Network → Translation**:

| Row | What it is |
| --- | --- |
| LibreTranslate Server Address | For example `https://translate.example.org` or `http://192.168.1.20:5000`. Empty means MyMemory, and the row then shows "MyMemory". |
| LibreTranslate API Key | Only if your server asks for one. It is stored like the other tokens (mixed with the device's hardware address, not encryption), never shown again on the reader and never sent to the web settings page, which only says whether one is set. |
| LibreTranslate: Check Certificate | Off by default, like every server of your own: the connection is encrypted but the certificate is not checked. On: checked against `/.crosspoint/ca.pem` when that file is there, else against the built-in roots. |

- With an address set, only your server is asked; MyMemory is never used as a fallback.
- The request is `POST <address>/translate` with a JSON body `{"q", "source", "target", "format": "text"}` and
  `"api_key"` when set, as in <https://docs.libretranslate.com/>. A book with no language is sent with
  `"source": "auto"`, so the server detects it.
- An `http://` address sends the selection and the key unencrypted. Use it on your own network only.
- The hosted libretranslate.com asks for an API key (see its site); a server you run yourself needs none unless you set one.

### Messages

| Message | What happened |
| --- | --- |
| No translation found | The service answered with no text. |
| Selection too long to translate | Over 500 bytes. Nothing was sent. |
| Book language unknown or same as the menus | No language to translate from (MyMemory) or into. Nothing was sent. |
| This language pair is not supported | The service does not know one of the two languages. |
| Translation limit reached. Try later | MyMemory's daily allowance is used up, or your server says slow down (429). |
| Server refused: check the API key | Your server answered 403, or asked for a key. |
| Check the server address in Settings | The address is not an `http://` or `https://` one. |
| Couldn't reach the translation service | No connection, or the certificate check failed. |
| Unknown date: the certificate cannot be checked | The clock has no date and it could not be set from the network. Nothing is sent. |
| Server error. Try again later | The service failed (5xx). |
| ca.pem could not be read | Check Certificate is on and `/.crosspoint/ca.pem` is there but empty or unreadable. Nothing is sent. |
| Not Enough Memory | Too little free memory for a secure connection. Try again after the book reopens. |
| Couldn't read the answer | Not a translation (a sign-in page of a public network, for example). |

### Not verified

- Anything on a reader or in the simulator: the popup with two choices and its width in Greek, the question
  before the first use, the result screen, free memory during the request, the return to the page.
- A TLS handshake from the reader with api.mymemory.translated.net. The chain was read and checked on a
  computer with OpenSSL against the firmware's root list on 2026-11-13.
- What MyMemory sends when the daily limit is reached. The reader takes HTTP 429, `"responseStatus": 429`,
  `"quotaFinished": true` or a text starting with "MYMEMORY WARNING" as that; this shape is from memory, the
  limit itself was never hit. The normal answer and the two language errors were captured from the live service.
- Whether the daily limit is counted per network address.
- A real LibreTranslate server: its answers are tested from the examples in its documentation, and the wording
  of its error messages (used to tell an unknown language from a missing key on a 400) is from memory.
- It needs a dictionary to be set, like the Wikipedia offer: both are part of the dictionary's Not found
  message. EPUB books only.


## Where to find dictionaries

> credit to https://github.com/koreader/koreader/wiki/Dictionary-support for the list.

- The [reader.dict](https://www.reader-dict.com) (ex "BoboTiG/ebook-reader-dict") project provides StarDict version of daily dumps of [Wiktionary](https://www.wiktionary.org/) monolingual dictionaries for a variety of languages. It also provides [non-free multilingual](https://www.reader-dict.com) dictionaries.
- The [WikDict](https://www.wikdict.com) project provides free bilingual dictionaries based on [Wiktionary](https://www.wiktionary.org/) for a lot of language pairs. StarDict versions can be [downloaded from here](https://download.wikdict.com/dictionaries/stardict/).
- The [`Vuizur/Wiktionary-Dictionaries`](https://github.com/Vuizur/Wiktionary-Dictionaries) repository contains dictionaries based on [Wiktionary](https://www.wiktionary.org/) from many languages to English, including English-English.
- The [DictInfo](https://www.dictinfo.com/) website provides outdated monolingual dictionaries based on [Wiktionary](https://www.wiktionary.org/).
- The [Firedict site](https://tuxor1337.frama.io/firedict/dictionaries.html) contains a list of freely available dictionaries.
- [wiktionary_stardict](https://xxyzz.github.io/wiktionary_stardict/): update monthly.
- [Fictionaries](https://fictionary.gumroad.com/) provides dictionaries for various speculative fiction books and series.
- [World Factbooks Archive](https://github.com/MilkMp/CIA-World-Factbooks-Archive-1990-2025) provides 36 years of CIA's World Factbook dictionaries in StarDict format.
- [StarDict-Hebrew](https://github.com/Uri-Tauber/StarDict-Hebrew) Hebrew-English StarDict versions of Babylon dictionaries.
