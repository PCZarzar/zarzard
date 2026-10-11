# zarzard

**戦え — fight.** A study firmware for the Xteink X4 and X3 e-readers: spaced-repetition
flashcards, drill, study statistics and summaries, on top of a full
EPUB reader.

Zarzard is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
1.6.0. Everything CrossPoint does is still here; Zarzard adds the study tools and
its own look.

## What it adds

- **Flashcards** from CSV files in `/flashcard` on the SD card (one level of
  folders).
  - **Spaced review** (SM-2): Again / Hard / Good / Easy, a daily new-card limit
    shared across decks, free days, decks in stand-by or reviews-only.
  - **Drill**: go through a deck (or a whole folder) in order or
    shuffled; sort cards into Missed / Unsure / Known, then repeat the ones you
    missed. The pass is saved and survives sleep.
  - While reviewing: mark, suspend, skip, reset or delete a card.
- **Statistics**: cards and study time per day, week and month, streak,
  retention, forecast, per-deck progress and difficult cards.
- **Summaries**: EPUBs kept in `/summaries`, apart from books.
- **Zarzard theme**: Manrope type, icons, a Home that shows today's study at a
  glance.

## Install

The X4 and X3 can be updated from the SD card, no USB flashing needed:

1. Download `firmware.bin` from the [latest release](https://github.com/PCZarzar/zarzard/releases/latest)
   and copy it to the root of the SD card.
2. On the device: **Settings → System → SD Card Firmware Update**, pick the file,
   confirm, and do not power off until it restarts.

Later versions arrive through **Settings → System → Check for updates** (Wi-Fi),
which reads this repository's releases.

**Recovery.** Keep the official CrossPoint `firmware.bin` on the card. If
something goes wrong, the same SD update menu (or the boot firmware picker)
reinstalls it.

## Flashcard file format

```csv
card_id,front_content,back_content,sr_due,sr_interval,sr_ease,flags
1,What does /etc/fstab hold?,Filesystems mounted at boot.,,,,
```

Only `front_content` and `back_content` are needed for new cards; the device
fills in the rest. Wrap an answer in backticks to show it in a monospace font.

## Build

```sh
pio run -e gh_release
```

The release binary is `.pio/build/gh_release/firmware.bin`. Host tests for the
flashcard code: `bash test/flashcard/run.sh`.

## Credits and licenses

Zarzard is © Pedro Zarzar and released under the MIT License (see `LICENSE`),
like the CrossPoint Reader it builds on. See `NOTICE` for the third-party
software, fonts and icons it includes, and their licenses. The firmware
binaries include wolfSSL, licensed under GPLv2 or later; this repository is
their corresponding source.
