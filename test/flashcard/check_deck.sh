#!/bin/bash
# Check real decks against the on-device layout. Usage: bash test/flashcard/check_deck.sh deck.csv ...
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$HERE/../.."
FC="$REPO/src/activities/flashcard"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
g++ -std=gnu++2a -Uunix -O1 -w -I"$HERE/stubs" -I"$FC" -I"$REPO/lib/Memory" -I"$REPO/src" -I"$REPO/lib/EpdFont" \
  -I"$REPO/lib/Utf8" "$HERE/deck_check.cpp" "$FC/FlashcardLayout.cpp" "$FC/FlashcardDeck.cpp" "$FC/FlashcardSrs.cpp" \
  "$REPO/lib/EpdFont/EpdFont.cpp" "$REPO/lib/EpdFont/EpdFontFamily.cpp" "$REPO/lib/Utf8/Utf8.cpp" -o "$OUT/deck_check"
ARGS=()
for f in "$@"; do ARGS+=("$(realpath "$f")"); done
TZ=UTC "$OUT/deck_check" "${ARGS[@]}"
