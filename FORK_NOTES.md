# FORK_NOTES — LPIC flashcards on CrossPoint

Flashcards (linear review, deck chaining, daily new-card limit, legibility) ported
onto the official **CrossPoint Reader 1.6.0** (`54337e6`, the release installed on
the device). The first implementation was done on CrossPet (`~/xteink/crosspet`,
branch `lpic-flashcards`); it was moved here because the device has USB flashing
locked and Brick Club flags CrossPet as unsafe to flash on locked X4 units, while
CrossPoint 1.6.0 is rated *safe* and has SD firmware update + SD recovery.

## Device facts

- Xteink X4, USB flashing locked (never enumerates over USB, even running CrossPoint).
  No full-flash backup is possible; recovery is via SD only (see below).
- No RTC chip on X4 (`RtcType::None`), so CrossPoint never sets the date on it.
- UI theme: Lyra (`uiTheme: 1`), language EN.

## Backups

- SD card, file-level, 16 files / 94 MB: `~/xteink/backups/sd_2026-09-24/`
  (+ `sd_2026-09-24.sha256`, verified).
- Official firmware: `update.bin` = CrossPoint 1.6.0 (5,437,760 bytes), also kept in
  the SD backup.

## Toolchain

The official CI builds with `pioarduino/platformio-core v6.1.19`
(`.github/workflows/release.yml`). PlatformIO 6.2.0 fails on this tree
(`No module named 'SCons.Tool.FortranCommon'`), so builds here use
`~/xteink/.venv-cp` (pio 6.1.19) with `PLATFORMIO_CORE_DIR=~/xteink/.pio-core-cp`.

## Baseline (unmodified 1.6.0)

| env | firmware.bin | app0 used | free |
|---|---|---|---|
| `gh_release` | 5,440,480 B | 83.02% | 16.98% |
| `default` | 5,484,752 B | 83.69% | 16.31% |

(Official `update.bin`: 5,437,760 B.) Budget for ≥ 10% free: 5,898,240 B.

Test deck `test/decks/lpic_test.csv`:
`sha256 e90c3e067b66ac197ed51b894adfb01547aec0e8432ad444087e988395e659d5`

## Design decisions

- **Base and scope.** Flashcards live in `src/activities/flashcard/` and open from a
  new Home menu item (between File Transfer and Settings). Touched upstream files:
  `HomeActivity.{h,cpp}`, `ActivityManager.h` (`HomeMenuItem::FLASHCARDS`, appended),
  `main.cpp` + `fontIds.h` (font registration), `GfxRenderer` (`fontHasCodepoint()`),
  `fontconvert.py` (`--only-intervals`), the font scripts, and the two YAML files.
- **Settings file.** `/.crosspoint/flashcard.json` (`FlashcardStore`, a
  `PersistableStore`), loaded when the flashcard app opens, not at boot.
- **Date on X4 (no RTC).** CrossPoint never sets the date on X4. The mode screen has
  *Sync date (Wi-Fi)*: saved WiFi → NTP → system clock, like CrossPoint's clock
  sync (WiFi is torn down with a silent restart on exit). ESP-IDF is built with
  `CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC_HRT=y`, so the clock survives deep sleep and
  software restarts; only a power-on reset (battery empty) loses it.
  Clock states: *Unset* (before 2026-01-01 → Spaced **blocked**, never writes bogus
  dates), *Stale* (no sync for > 3 days → "sync recommended"), *Ok*.
  The local day boundary uses the flashcard *Time zone* setting (default UTC−3),
  because the X4 hides CrossPoint's clock offset.
  Deviation from spec step 2 (which only warned): agreed with the user.
- **No CSV import screen.** CrossPet's file picker depended on its `drawList`.
  Put decks in `/flashcard/` from a PC or CrossPoint's WiFi file transfer.
- **Upstream CrossPet bug fixed:** `saveToCsv()` wrote the header with length 63
  for a 62-byte string, so a NUL byte followed the header and the next load
  returned an **empty deck**. Fixed here (and on the CrossPet branch); stray NULs
  are dropped on load.
- **Save safety.** Writes go to `x.csv.tmp`, then remove + rename (SdFat rename
  does not overwrite). An orphaned `.tmp` is restored by the deck list and before
  the next save. A short SD read fails the load. A deck that failed to load is
  never saved.
- **Decks over 500 cards** load truncated; they are Linear only (Spaced blocked,
  skipped in spaced rounds, total shown as `500+`) because saving would drop the
  tail. The CSV must also be ≤ 256 KB.
- **Pending Spaced ratings are saved in `onExit()`**, which CrossPoint also calls
  when the device goes to sleep; CrossPet could lose up to 4 ratings.
- **Fonts with Latin-only subsets.** A size is skipped when it lacks a glyph that
  the full NotoSerif 14 has (e.g. Cyrillic, `ő`), so nothing drawable becomes
  invisible. **Arrows and math symbols (`→ ≥ ≠`) exist in no built-in serif font**
  (neither did they on CrossPet): write `->`, `>=`, `!=` in decks.
- **Back layout.** A very long question keeps at most half of the card area; the
  answer never overflows onto the button hints (extra lines are dropped).
- **Theme.** Host layout checks use Lyra (the device theme; tallest header).
  Lyra hint buttons are 80 px: labels like `Errei <10m` wrap to two lines via
  CrossPoint's own `drawHintLabel`; nothing overflows.
- **Home menu height.** Fits in Classic, Lyra and RoundedRaff even with an OPDS
  server configured; *Lyra 3 covers* + OPDS server would overflow by ~36 px.
- **Spec 5.1 vs 5.3:** JetBrains Mono also generated at 14/16/18 (5.3 needs 24→14).
- **`N` in "Next: deck (N cards)"** is capped by *Max reviews / deck* and resolved
  when the session ends (it depends on the quota the session consumed). Spaced
  rounds wrap around the list; Linear "next deck" does not wrap (as specified).

## Host tests (`bash test/flashcard/run.sh`)

g++ builds of the real `FlashcardDeck`, `FlashcardSrs`, `FlashcardClock`,
`FlashcardLinearSession` and `FlashcardLayout` with the real built-in font data;
SD card, logging and renderer are stubs (the SD stub closes files on destruction
and refuses to rename over an existing file, like SdFat).

| Acceptance | Host coverage |
|---|---|
| 1 Linear doesn't write | Code path: every CSV write is behind `mode == Spaced`. Device sha256 check still required. |
| 2 Spaced intact | `FlashcardSrs::review()` is byte-identical to CrossPet/upstream; save → reload round-trip checks `sr_due/interval/ease`. |
| 3 Shuffle | Yes. |
| 4 Summary | Yes. |
| 5 Daily limit | Yes (2 decks × 20 new, limit 10 → 10). |
| 6 Chaining | Yes (no repeated deck, wraps, ends in All caught up). |
| 7 Backslash | Yes (parser + the 7 test cards). |
| 8 Legibility | Yes, with Lyra geometry via the same `layoutFront/layoutBack` the screen uses. |
| 9 Flash | Yes (see Final build). |

Extra: date/day-boundary math, clock states, header NUL bug, NUL-tolerant load,
orphaned `.tmp` recovery (list and save), > 500 cards never written, glyph
coverage fallback, long question/answer bounds.

## Rescue kit and install (SD only — USB is locked)

On the SD card root:
- `update.bin` — **official CrossPoint 1.6.0** (unchanged). Rescue: power off,
  plug USB into power, hold **Power + Up** → the OEM bootloader reflashes it.
- `crosspoint-1.6.0-oficial.bin` — same file, for CrossPoint's own pickers.
- `crosspoint-flashcards.bin` — this build (`gh_release`).

Install: Settings → **SD Card Firmware Update** → pick `crosspoint-flashcards.bin`.
It validates the image and writes the other OTA slot.
Back to official: same menu → `crosspoint-1.6.0-oficial.bin`; or CrossPoint's
recovery (hold **Down + Power** at boot → picker); or the OEM route above.

## On-device checklist

Setup: `test/decks/lpic_test.csv` is copied to `/flashcard/` on the SD. Expected
`sha256` of the original: `e90c3e06…95e659d5`.

1. **Install** — Settings → SD Card Firmware Update → `crosspoint-flashcards.bin`.
   Device reboots into CrossPoint; Home shows **Flashcards** above Settings.
2. **Deck list** — Flashcards → `lpic test` with `New 7 Due 0 Total 7`, and
   Flashcard Settings. Back returns to Home with Flashcards selected.
3. **Clock gate** — pick the deck: *Spaced review* shows "Clock not set — connect
   Wi-Fi" and Confirm on it jumps to *Sync date (Wi-Fi)*. *Linear* works.
4. **Date sync** — *Sync date (Wi-Fi)* → pick/confirm your WiFi → "Clock synced" →
   Back → device restarts to Home. Reopen Flashcards → deck → Spaced no longer
   warns. **Sleep the device (power button) and wake it: Spaced still has no
   warning** (clock kept across deep sleep).
5. **Time zone** — Flashcard Settings → Time zone shows `UTC-03:00`.
6. **Linear doesn't write** [1] — Linear on `lpic test`, mark a mix of
   Missed/Unsure/Known through a full pass, *Repeat*, then *Exit*; also Back on a
   front → "Leave pass?" → Confirm. Put the SD in the PC: `sha256sum` unchanged.
7. **Linear buttons / header** — back side: Back=Missed, Prev=Unsure, Next=Known,
   Confirm does nothing. Header `Pass 1 · i/7`. A Missed card does not return in
   the same pass.
8. **Shuffle** [3] — ON: two full passes in different orders. OFF: CSV order.
9. **Summary** [4] — counts match; *Repeat missed + unsure* brings exactly those
   cards; gone after an all-Known pass; *Next deck* hidden on the last deck.
10. **Backslash / hint** [7] — card 1 `Contar as linhas de um arquivo` + hint
    `coreutils`; card 5 shows `grep '\.conf$'` with no hint; card 6 front
    `Regex com barra: grep '\.conf$'` + hint `escape do ponto`, back in mono with
    the backslash; card 7 front breaks after "Quebra", back shows two lines.
11. **Legibility** [8] — short fronts in large serif; `wc -l notas.txt` on one mono
    line; card 3 path broken after `/`; no word split; back: question small on
    top, separator, answer centred; no hint on the back.
12. **Spaced + save** [2] — on a copy of the deck, rate cards 1–4 Good, Easy, Hard,
    Again; Back. On the PC, `sr_due/sr_interval/sr_ease` of cards 1–3 are
    `D+1,1,250`, `D+4,4,265`, `D+1,1,235` (D = today); card 4 `D,0,230`.
13. **Sleep mid-session** — Spaced, rate 2 cards (fewer than 5), press Power to
    sleep, wake. On the PC the 2 ratings are in the CSV.
14. **Daily limit** [5] — two decks with 20 new cards each, *New cards / day* = 10:
    10 new cards in the first deck, then `All caught up` (the second deck is not
    offered); reopening the second deck shows no new cards today.
15. **Spaced chaining** [6] — with several decks having due cards: Done shows
    Again/Hard/Good/Easy counts and `Next: <deck> (N cards)`; Confirm chains; no
    deck repeats; ends in `All caught up`; Back returns to the list.
16. **Portuguese** — Settings → Language → Português (Brasil): flashcard screens
    and buttons in Portuguese; rating labels may wrap to two lines inside the
    button (expected), never overflow.
17. **Regression** — open a book, turn pages, sleep/wake; file transfer still
    works.
