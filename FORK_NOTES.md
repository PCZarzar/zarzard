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

