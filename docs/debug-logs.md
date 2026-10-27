# Debug logs

When something goes wrong on your device, like a crash, a screen that turns on by itself, or a
sync that fails, the device's own log shows what happened. VaultInk can save that log to the SD
card so you can send it to me.

**It's off by default, and nothing is ever sent anywhere by the device.** You choose which files
to send, and you send them yourself.

## Turn it on

1. **Settings → System → Debug Logs (to /debug on the SD card)**: turn it on.
2. Use the device as usual until the problem happens again.
3. Take the files from the **`debug`** folder on the SD card (with a card reader, USB, or the
   device's web page under File Transfer → Files).
4. Email them to **me@lefteros.com**. Please don't attach them to a GitHub issue, which is
   public. A short note helps: what happened, roughly when, and what was on the screen.
5. Turn the setting off again if you like. **Settings → System → Clear Debug Logs** deletes the
   folder.

## What's in the folder

- `log-0.txt` is the newest log. When it reaches 256 KB it becomes `log-1.txt`, and so on up to
  `log-3.txt`. The oldest one is then deleted, so the logs never use more than about 1 MB.
- `crash-0.txt`, `crash-1.txt`, ... are written when the device restarts after a crash: what
  crashed, where, and the last log lines before it. (`/crash_report.txt` on the card always
  holds the latest crash, even with Debug Logs off.)

Each line starts with the time since the device started, in milliseconds. Every boot logs why it
started (power key, USB, waking up), which helps with "it turned on by itself".

## What it can show about you

The log holds what the device does: the names of the books, notes and folders you open, the
names of Wi-Fi networks, and the addresses of servers it talks to (LiveSync, OPDS, KOReader
sync). I checked that it doesn't write passwords, the LiveSync passphrase or the vault key. It
isn't meant to hold the text of your notes either. The files are plain text, so have a look
before you send them.

## Στα ελληνικά

1. **Ρυθμίσεις → Σύστημα → Αρχεία καταγραφής (στο /debug της κάρτας SD)**: άνοιξέ το.
2. Χρησιμοποίησε τη συσκευή κανονικά μέχρι να ξανασυμβεί το πρόβλημα.
3. Πάρε τα αρχεία από τον φάκελο **`debug`** της κάρτας SD.
4. Στείλ' τα με email στο **me@lefteros.com**, όχι στο GitHub (εκεί τα βλέπουν όλοι). Γράψε και
   δυο λόγια: τι έγινε, περίπου πότε, και τι είχε η οθόνη.
5. Η **Διαγραφή αρχείων καταγραφής** σβήνει τον φάκελο.

Τα αρχεία έχουν ονόματα βιβλίων, σημειώσεων και φακέλων, ονόματα δικτύων Wi-Fi και διευθύνσεις
διακομιστών, όχι κωδικούς. Είναι απλό κείμενο: ρίξε μια ματιά πριν τα στείλεις.
