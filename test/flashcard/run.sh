#!/bin/bash
# Build and run host-side flashcard tests against the real flashcard sources
# and the real built-in font data. Usage: bash test/flashcard/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$HERE/../.."
FC="$REPO/src/activities/flashcard"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cp "$REPO/test/decks/lpic_test.csv" "$OUT/"
CXX="g++ -std=gnu++2a -Uunix -O1 -Wall -Wextra -I$HERE/stubs -I$FC -I$REPO/lib/Memory -I$REPO/.pio/libdeps/default/ArduinoJson/src"

$CXX "$HERE/test_flashcard.cpp" "$FC/FlashcardDeck.cpp" "$FC/FlashcardSrs.cpp" "$FC/FlashcardClock.cpp" \
  "$FC/FlashcardCatalog.cpp" "$FC/FlashcardCsv.cpp" "$FC/FlashcardFolderIndex.cpp" "$HERE/stubs/FlashcardMemoryStub.cpp" \
  "$FC/FlashcardPlanner.cpp" "$FC/FlashcardDeckOptions.cpp" "$FC/FlashcardHistory.cpp" "$FC/FlashcardPeriods.cpp" \
  "$FC/FlashcardStatsScan.cpp" "$FC/FlashcardCardActions.cpp" -o "$OUT/test_flashcard"
TZ=UTC "$OUT/test_flashcard" "$OUT"

# Layout check with real font metrics (acceptance 8)
$CXX -w -I"$REPO/src" -I"$REPO/lib/EpdFont" -I"$REPO/lib/Utf8" \
  "$HERE/test_layout.cpp" "$FC/FlashcardLayout.cpp" "$FC/FlashcardDeck.cpp" "$FC/FlashcardSrs.cpp" \
  "$FC/FlashcardCsv.cpp" "$HERE/stubs/FlashcardMemoryStub.cpp" \
  "$REPO/lib/EpdFont/EpdFont.cpp" "$REPO/lib/EpdFont/EpdFontFamily.cpp" "$REPO/lib/Utf8/Utf8.cpp" \
  -o "$OUT/test_layout"
TZ=UTC "$OUT/test_layout" "$OUT"
