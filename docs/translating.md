# Translating VaultInk

VaultInk's menus come from one file per language in
[`lib/I18n/translations/`](../lib/I18n/translations). English (`english.yaml`) is the source.
Any text a language doesn't have yet shows up in English, so a half-finished translation still
works.

English and Greek are complete. The other languages came with CrossPoint Reader and miss the texts
VaultInk added. To see where each one stands:

```sh
python3 scripts/i18n_status.py
```

## Asking for a language

Open a [translation request](https://github.com/Somnius/VaultInk/issues/new?template=translation_request.yml).
Say which language, and whether you can translate it yourself. If you can't, the request still
helps: it shows which languages people want.

## Translating

You don't need to build anything to translate. A text editor is enough.

1. **Start from the right file.**
   - If your language already has a file, run `python3 scripts/i18n_status.py <file name>`, for
     example `python3 scripts/i18n_status.py italian`. It prints every English text that file is
     missing, in a form you can paste into the file and translate.
   - If it doesn't, copy `english.yaml` to a new file named after the language in English, for
     example `welsh.yaml`.
2. **Set the header** at the top of a new file:
   ```yaml
   _language_name: "Cymraeg"   # the language's name in itself
   _language_code: "CY"        # short code, upper case
   _order: "40"                # position in the language list
   _bcp47: "cy"                # standard language tag
   ```
3. **Translate the text between the quotes.** Leave the names on the left (`STR_...`) as they are.
   Keep `%d`, `%s` and `%%` in place: the device fills them in with numbers or names. The status
   script warns when they don't match the English.
4. **Keep it short.** The screen is 480 pixels wide. If a phrase is much longer than the English,
   look for a shorter way to say it.
5. **Brand and product names stay as they are:** VaultInk, Obsidian, CrossPoint, Calibre, OPDS,
   KOReader, Wi-Fi. In Greek we also keep "Vault" in English, since that's the word Obsidian users
   know; other languages may choose the same.
6. **Send it in.** Attach the file to your translation request, or open a pull request with it.
   Either is fine.

## Checking it on a computer

If you can build the firmware (see the [README](../README.md#building)), the simulator shows your
translation on a screen the same size as the device's:

```sh
python3 scripts/gen_i18n.py lib/I18n/translations lib/I18n/
pio run -e simulator_x4_pro
```

Then pick your language under **Settings → System → Language**.

## Languages that need their own fonts

The built-in fonts cover Latin, Greek, Cyrillic, Hebrew, Arabic and Vietnamese. Languages in other
scripts (for example Indic scripts or Burmese) need fonts and text shaping the firmware doesn't
have yet. Requests for them are still welcome; they help decide what comes next.
