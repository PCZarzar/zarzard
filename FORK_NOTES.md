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
- **Literal `/n` in paths (`\/`).** `/n` is the format's line break (spec card 7),
  which would split `/etc/nsswitch.conf` or `/sbin/nologin`. In the front, the hint
  and plain answers write **`\/`** for a literal slash: `/etc\/nsswitch.conf`.
  Answers wrapped in backticks are always literal (no escaping needed there).
- **Independent review (high)** — fixed: `/n` in paths (above); front and hint are
  clipped to the card area (a huge front/hint no longer overlaps the buttons); decks
  that fail to load are reported on the list instead of vanishing; `goHome()` from
  any flashcard screen returns to the Flashcards row; dead `importCsv()` (512-byte
  stack buffer) removed; vectors pre-reserved.
  Accepted as limitations: the deck load needs one heap block the size of the CSV
  (≤ 256 KB; real LPIC decks are a few KB); the deck list reloads all CSVs after a
  session (slow only with many large decks); Home menu overflows in Classic or
  Lyra 3 Covers when an OPDS server is also configured; SM-2 is unchanged by spec
  (a hand-edited absurd `sr_interval` can wrap); the NTP loop mirrors
  `HalClock::syncFromNTP`, which cannot be reused because it returns early on
  boards without an RTC (the X4).
- **Linear progress is saved (user request, overrides spec 3.5/3.6).** Back on a
  card saves and leaves (no "progress will be lost" prompt). The pass (number,
  position, shuffled order, Missed/Unsure/Known piles) is written to
  `/.crosspoint/fc_linear_<hash>.json` every 5 cards, on Back/Exit and when the
  device sleeps; **the deck CSV is still never written in Linear**. Opening the deck
  in Linear offers *Continue (Pass N · i/n)* or *Restart from the beginning*.
  The file stores a fingerprint of the cards' ids/fronts/backs: if the CSV's cards
  changed, or the file is inconsistent, the pass restarts instead of pointing at
  wrong cards. Chained "Next deck" resumes that deck's saved pass if it has one.
  Waking from sleep still opens Home (CrossPoint behaviour); reopen the deck →
  Continue.
- **E-ink refresh.** Card screens use CrossPoint's fast refresh only (no periodic
  flashing). A periodic half refresh was tried and removed at the user's request:
  in flashcard use it flashed every ~7 cards and left the unchanged header a shade
  lighter. To clear ghosting on demand use CrossPoint's *Short Power Button →
  Refresh*.
- **Folders and two entry points (user request, 2026-09-26).** Flashcards opens on
  *Spaced review* / *Linear review* / *Statistics* / *Settings* (the per-deck mode screen is gone).
  Decks may sit in `/flashcard/*.csv` or one folder level `/flashcard/<folder>/*.csv`
  (folders are created from a PC or CrossPoint's WiFi file transfer; hidden files
  and folders such as macOS `._x.csv` are ignored).
  *Spaced* is one flat list of every deck, ordered by `FlashcardCatalog::sortForSpaced`:
  decks with something to review first (most due, then most new, then name), then an
  *Up to date* section. *Review all* chains through pending decks, always taking the
  most-due unvisited one. Date sync lives on that list (when the date is unknown or
  stale) and in Settings. *Linear* browses folders → decks; a deck with a saved pass
  offers Continue / Restart; *Next deck* stays inside the folder. Shuffle moved to
  Settings. Moving a deck to another folder changes its path, so its saved linear
  pass is not found any more (the CSV itself is unaffected).
- **Whole folder, shuffled (Linear).** Inside a folder the first row studies every
  card of its decks as one pass, always fully shuffled (each new pass reshuffles,
  whatever the Shuffle setting). `FlashcardFolderIndex` keeps only each card's file
  offset (6 bytes per card, up to 3000 cards) and reads the card on screen from the
  SD card; the CSVs are never written. The folder pass has its own saved progress
  (Continue / Restart), keyed by the folder path and a fingerprint of the cards.
  No "Next deck" after a whole-folder pass.
- **Streaming CSV reader (fix for large decks, 2026-09-27).** Decks of ~300 cards
  (50 KB) aborted in `operator new`: every deck was read whole into RAM to build the
  lists. `FlashcardCsvReader` parses from the SD card in 512-byte chunks with the
  original parser's rules (host tests compare both on 1500 random files); lists only
  count cards (`FlashcardDeck::scanStats`); a deck is loaded only when the free heap
  holds it with 24 KB to spare, otherwise "could not be loaded" instead of a crash.
- **Free days (Anki "Easy Days" idea, user request).** *Settings → Free days* marks
  weekdays with no study (`FlashcardStore::freeDaysMask`, bit 0 = Sunday; all seven is
  refused). After SM-2 computes a card's next state (unchanged), a due date on a free
  day moves to the nearest study day (`FlashcardClock::avoidFreeDays`): up to
  interval/3 days earlier when possible (e.g. interval ≥ 3 → Saturday instead of
  Sunday), otherwise later (Monday). Again (due today) is never moved. Cards that were
  already due on a free day before the setting existed show up the next study day.
- **Spaced review planning (user request, Anki-like).** *Settings → Spaced review*
  holds the daily new-card **total** (5–100), max reviews per deck session, free days
  and **Decks**. Each deck has a state — *Active* (new + due), *Reviews only* (due),
  *Stand-by* (out of the Spaced list; Linear unaffected; the CSV keeps its SRS dates)
  — and a **share** (1–50, default 5). A deck without a saved choice is Active when
  any card was studied in Spaced review, Stand-by otherwise, so new decks never
  flood the list. Once a day `FlashcardPlanner::split` divides the total between
  Active decks with new cards in proportion to their shares (Sainte-Laguë, ties by a
  random seed; a deck never gets more than its new cards; the rest goes to the
  others). A changed choice or total redoes the split the same day, keeping what each
  deck already used. Choices, the split and the last Linear pass result per deck are
  in `/.crosspoint/flashcard_decks.json` (`FlashcardDeckOptions`), keyed by path.
- **Statistics (user request).** A daily log `/.crosspoint/flashcard_history.bin`
  (`FlashcardHistory`, 28-byte checksummed records, rewritten through a .tmp copy)
  counts Spaced ratings, new cards, *true retention* (ratings of cards already in
  review, interval ≥ 1, not Again — new cards and relearning are left out, as in
  Anki), mature retention, Linear cards and study time (≤ 60 s per card). Linear
  only adds volume and time, never accuracy. The screen: a panel per Today / Week
  (Monday first) / Month with total cards, time, Spaced/Linear split, streak (free
  days without study do not break it), new cards, retention and a small chart;
  History (bars by days / weeks / months, paging back), Forecast (30 days), Decks
  (phases new / learning / mature and the last full Linear pass "% known") and
  Difficult cards (ease ≤ 150%), which can be studied as one Linear session
  (`FLASHCARD_HARD_KEY`, also the first row of Linear review). History, streak and
  Linear results start at install; forecast, phases and difficult cards come from the
  CSVs. Linear cards are only logged while the date is known (synced clock).

## Final build

| env | firmware.bin | app0 used | free |
|---|---|---|---|
| `gh_release` (installed) | 5,596,848 B | 85.40% | **14.60%** |
| `default` | 5,641,536 B | 86.08% | 13.92% |

Baseline `gh_release` was 5,440,480 B (16.98% free): +156,368 B. Requirement ≥ 10%: OK.
No new compiler warnings; cppcheck: 0 high / 0 medium (2 low style notes).
Installed image: `crosspoint-flashcards.bin` =
`sha256 04341d65c5cfe9133f36bf86c51533646b8bf38731e17209c159ad5ba7a67438`
(commit `e3a05bf2`; copy in `~/xteink/releases/`). `esptool image-info`: ESP32-C3,
7 segments, checksum and SHA-256 valid; board tag `CROSSPOINT-BOARD-V1:x4;`
(same as the official image, so CrossPoint's SD updater accepts it).
The official rescue image `update.bin` is byte-identical to the GitHub release
asset `crosspoint-1.6.0-x3-x4.bin` (`sha256 832f3651…a1ad436`).

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
   Missed/Unsure/Known through a full pass, *Repeat*, then *Exit*. Put the SD in
   the PC: `sha256sum` of the CSV unchanged (progress lives in `/.crosspoint/`).
6b. **Linear progress** — stop at card 30 with Back → reopen the deck → Linear →
   *Continue (Pass 1 · 30/…)* shows the same card, same order, same pile counts at
   the end. Stop again, put the device to sleep, wake → reopen → Continue works.
   *Restart* starts at 1 and the old pass is gone.
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
