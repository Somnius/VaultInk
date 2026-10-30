# Flashcards (next release)

Write cards in your own notes, review them on the reader. No account, no network.

## Writing cards

Tag the note `#flashcards` (in the frontmatter `tags:` or anywhere in the text). `#flashcards/greek/verbs`
puts its cards in the deck `greek/verbs`. Notes without the tag are never read, so lines such as
`key::value` (Dataview fields) don't become cards.

| You write | You get |
|---|---|
| `Front::Back` | one card |
| `Front:::Back` | two cards, both ways |
| a few lines, then a line with only `?`, then the answer lines | one card (use `??` for both ways) |
| `The capital is {{Athens}}.` | a card with `[...]` in the gap, one per `{{ }}` |
| `**Athens**` in a note with `sr-cloze: bold` in its frontmatter | the same, with bold as the gap |

Good to know:

- A multi-line card is the whole paragraph around the `?` line, so keep a blank line between it and
  other cards.
- `==highlight==` is never a gap: VaultInk shows it as a highlight, so most of your highlights would
  turn into cards.
- Lines inside code blocks, the frontmatter and the Spaced Repetition plugin's `<!--SR:...-->`
  comments are skipped. VaultInk never writes into your notes.
- Cards longer than 2,000 bytes are dropped, and a note gives at most 500 cards.

## Reviewing

**Vaults** on the home screen, then **Flashcards** at the top. With one vault that has cards you get its
decks straight away, each with "due · new · cards"; **All Cards** studies every deck. The clock has to
be set (Settings → System → Clock), because due dates are days.

A card shows its front. Tap the screen (or press Confirm) to show the back, then answer:

| Answer | Touch | Buttons |
|---|---|---|
| Again (shows again this session) | tap Again | Left |
| Hard | tap Hard | Up |
| Good | tap Good | Down or Confirm |
| Easy | tap Easy | Right |

Under each button is when the card comes back (`<1d`, `4d`, `2mo`). Back ends the session; every answer
is saved at once, so nothing is lost. The scheduler is SM-2 in whole days.

Inside a note, the reading menu has **Flashcards**: it reviews only that note's cards.

**Settings → Vaults**: New Cards per Session (20), Reviews per Session (100), Longest Interval in months (12).
The limits count per session, not per day.

## Where the state lives

Not in your notes. Each vault keeps `/.vaultink/<id>/cards.idx` (the cards found) and `cards.tsv` (one
line per answer, newest wins; rewritten past 1 MB, forgetting cards that left your notes over 90 days
ago) on the SD card, next to the search index. A card is known by its note path and text, so renaming a note or
editing a card makes it a new card, and retyping an accent or the spacing doesn't. If you also use the
Spaced Repetition plugin, the two keep separate schedules.

Not yet: pictures in cards, learning steps, opening the note at the card, a Flashcards row on the home
screen (it is full), importing Anki decks. The `flashcards` tag name is fixed for now.
