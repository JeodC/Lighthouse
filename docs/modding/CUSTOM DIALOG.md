# Custom Dialog

[LANGUAGE_PACKS.md](../../docs/modding/LANGUAGE%20PACKS.md) covers translating dialog, but *custom dialog* is more advanced. Say you want to modify a character's portrait sprite, or trigger code from inside a dialog. This is how it's done.

---

## 1. Anatomy of a dialog

A dialog asset is two lists of boxes - `Bottom` and `Top`, for the two screen positions - and each box is a `[code, "text"]` pair:

```yaml
ASSET_A0D_BLUBBER_COMPLETE:
  Bottom:
    - [0x87, "ME TREASURE! THANK YE ME HEARTIES, TAKE THIS REWARD!"]
    - [0x7, '\x01']
    - [0x87, "I'M OFF TER SPEND, SPEND, SPEND!"]
    - [0x4, ""]
  Top:
    - [0x4, ""]
```

There are **two separate code layers**:

- the **box code** - the leading byte of each row, interpreted by the dialog sequencer ([`src/core2/gc/dialog.c`](../../src/core2/gc/dialog.c));
- **inline codes** - bytes *inside* the quoted text, interpreted by the text renderer ([`src/core2/font/print.c`](../../src/core2/font/print.c)).

`0x04` as a box code closes the box; `0x04` inside a string is nothing.

---

## 2. Box codes

### Portraits: `0x80` and up

A box code of `0x80`+ means "text box, spoken by portrait `code - 0x80`". The portrait picks everything about the presentation: the talk sprite, the voice, the mouth animation - there is no separate "play voice" code; casting the speaker *is* the sound design. The full cast is the `GcZoomboxSprite` table in [`include/core2/gc/zoombox.h`](../../include/core2/gc/zoombox.h) (a dialog byte maps to that table at `code - 0x80 + 0x0C`). Some regulars:

| Code | Speaker | | Code | Speaker |
|---|---|---|---|---|
| `0x80` | Banjo | | `0x87` | Blubber |
| `0x81`/`0x82` | Kazooie | | `0xB5` | Gruntilda |
| `0x83` | Bottles | | `0xCB` | Brentilda |

Valid codes run `0x80`-`0xDD`; the retail game tops out at `0xDB`. Consecutive boxes with the *same* portrait code are grouped into one on-screen box as successive lines (up to 8), so a new code is also what forces a new box.

### Commands: `0x01`-`0x09`

Codes below `0x20` are commands, not text. The game implements nine:

| Code | Name | What it does |
|---|---|---|
| `0x01` | **Choice** | A/B prompt. The result (A = 1, B = 0) is handed to the actor's completion callback, which decides what happens - the dialog itself doesn't branch. |
| `0x02` | Yield, keep alive | Minimize this box but let it survive into the next dialog (used to chain conversations). |
| `0x03` | Choice, closing | Like `0x01`, but both boxes close after the answer. |
| `0x04` | **Close** | Shut this box and hand over to the other list. The standard terminator, every `Bottom` and `Top` ends with one. |
| `0x05` | Minimize, yield | Implemented but unused by the retail game. |
| `0x06` | **Hand over** | The back-and-forth marker: minimize, pre-load the next speaker's portrait, and switch to the other box. This is what bounces a conversation between `Bottom` and `Top`. |
| `0x07` | **Trigger** | Fire a gameplay event - see [§4](#4-scripting-gameplay-the-trigger-box). |
| `0x08` | Text variant | One replacement candidate for a `~` in the preceding text box - see [§3](#3-live-substitution). |
| `0x09` | Number slot | Replace the `~` in the preceding text box with a live number - see [§3](#3-live-substitution). |

Codes `0x0A`-`0x1F` fall through to garbage - don't use them. And a yaml hygiene reminder from the language guide: the box counts are recomputed on import, so adding and removing rows is always safe.

---

## 3. Live substitution

A `~` in a text box is a placeholder, filled in when the box is shown. What fills it depends on the rows that follow:

- **`0x08` rows** offer fixed alternatives; a callback from the actor showing the dialog picks one. Brentilda's gossip works this way - the right answer is baked into the save:

  ```yaml
  - [0xcb, "GRUNTY BRUSHES HER ROTTEN TEETH WITH ~ FLAVORED TOOTHPASTE!"]
  - [0x8,  "TUNA ICE CREAM"]
  - [0x8,  "SALTED SLUG"]
  - [0x8,  "MOULDY CHEESE"]
  - [0x4,  ""]
  ```

- **A `0x09` row** (empty string) makes the game print a number instead - the actor's callback supplies the value.

Since the *actor's code* selects the variant, you can reword the candidates or the surrounding text freely, but the number of `0x08` rows should stay what the actor expects.

---

## 4. Scripting gameplay: the trigger box

The `0x07` box is the interesting one. When the conversation reaches it, nothing is displayed - instead the dialog system calls the **trigger callback** the actor registered when it opened the dialog, passing the box's first text byte as an **event id** ([dialog.c:434](../../src/core2/gc/dialog.c#L434)). The box then skips itself and the conversation continues.

Blubber end to end:

1. His dialog `ASSET_A0D_BLUBBER_COMPLETE` (shown in [§1](#1-anatomy-of-a-dialog)) carries `[0x7, '\x01']` between "TAKE THIS REWARD!" and "I'M OFF TER SPEND…".
2. When Blubber opens it, he passes two callbacks ([blubber.c:96](../../src/TTC/ch/blubber.c#L96)) - one for completion, one for triggers.
3. The trigger callback ([blubber.c:77](../../src/TTC/ch/blubber.c#L77)) calls `jiggy_spawn(JIGGY_14_TTC_BLUBBER, …)` and puffs steam at the spawn point.

So the jiggy pops **mid-sentence**, exactly where the writer placed the box. Move the `0x07` row and the spawn moves with it; delete it and the jiggy never appears; add a second and the spawn fires twice. (The variant dialog for when you've already collected the jiggy, `ASSET_A2A`, simply has no trigger box.)

The **event id byte** is the actor's to interpret. Blubber ignores it - any trigger spawns his jiggy - but Bottles' molehills switch on it ([mole.c:201](../../src/core2/ch/mole.c#L201)): id `\x05` grants 50 eggs mid-lesson, `\x06` grants red feathers, `\x08` refills health, `\x01`-`\x04` move the tutorial camera.

Two rules keep expectations straight:

- **A trigger is a request, not an opcode.** It does whatever the showing actor's callback does - there is no generic "spawn object" code, and a `0x07` box in a dialog whose actor registered no trigger callback does nothing.
- **Skipping doesn't lose events.** If the player skips out of a dialog, every not-yet-reached trigger box fires anyway ([dialog.c:275](../../src/core2/gc/dialog.c#L275)) - so rewards can't be lost to the B button, and you can't hide "secret" triggers behind reading to the end.

---

## 5. Inline format codes

Inside the text, any byte that isn't a glyph in the current font falls into a command switch, and `0xFD` explicitly escapes the *next* byte into it. The useful ones:

| Sequence | Effect |
|---|---|
| `\xFDh` … `\xFDl` | **Shaky text** on … off (per-glyph wobble) |
| `\xFDf` | Toggle dialog <-> bold font |
| `\xFDb` | Toggle a black panel behind the text |
| `\xFDp` | Toggle monospaced spacing |
| `\xFDj` … `\xFDe` | Enter/leave the extended (JP) glyph sheet, where bytes are raw glyph indices |

Type them in **single-quoted** yaml (`'I FEEL \xFDhALL\xFDl FUNNY'`) - the importer decodes `\xNN` escapes there, while double quotes hand them to the yaml parser instead (see the [language guide's encoding notes](LANGUAGE%20PACKS.md#how-the-text-is-encoded)). Pauses, scroll speed, and line wrapping are *not* inline codes: punctuation pauses automatically, the player controls speed, and wrapping is automatic at 24 printable characters.

Because the switch catches every non-glyph byte, a **bare lowercase** `b d e f h j l p q v` in dialog text executes its command without any `\xFD` - stock text is all-caps, so vanilla never trips this, but your edits can. Stick to uppercase (or a font that actually has lowercase glyphs, which makes those bytes ordinary letters again). The `\xFD`-prefixed form is always safe.

---

## 6. Build and test

The quiz and Grunty-question assets (`quizq/`, `gruntyq/`) share this text encoding but use a 4-element row - `[cmd, 0xfd, 0x6c, "text"]` - whose two middle bytes must be kept; the importer rejects an option row without them (details in the [language guide](LANGUAGE%20PACKS.md#quiz-and-grunty-questions)).

Otherwise, everything is the standard flow: edit the yaml, `torch modding import o2r`, pull the changed `assets/dialog/…` entries into a mod with `torch pack`, and drop it in `mods/`. A two-minute test for the trigger mechanics: move Blubber's `[0x7, '\x01']` row *above* his first line, deliver his gold, and the jiggy pops before he says a word.
