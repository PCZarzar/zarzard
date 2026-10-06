#!/bin/bash
# Host checks of the Bible reader with real .zbib files and the real fonts.
# Usage: bash test/bible/run.sh <dir with ARA.zbib, KJV.zbib, NVI.zbib>
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$HERE/../.."
STUBS="$REPO/test/flashcard/stubs"
ZBIB="${1:?usage: run.sh <dir with .zbib files>}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
g++ -std=gnu++2a -Uunix -O1 -Wall -Wextra -w -I"$STUBS" -I"$REPO/src" -I"$REPO/src/activities/bible" \
  -I"$REPO/lib/EpdFont" -I"$REPO/lib/Utf8" -I"$REPO/.pio/libdeps/default/ArduinoJson/src" \
  "$HERE/test_bible.cpp" "$REPO/src/activities/bible/BibleFile.cpp" "$REPO/src/activities/bible/BibleLayout.cpp" \
  "$REPO/lib/EpdFont/EpdFont.cpp" "$REPO/lib/EpdFont/EpdFontFamily.cpp" "$REPO/lib/Utf8/Utf8.cpp" \
  -o "$OUT/test_bible"
mkdir -p "$OUT/work"
"$OUT/test_bible" "$OUT/work" "$ZBIB"
