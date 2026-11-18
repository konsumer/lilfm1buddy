#!/bin/sh
# The menu's screens (picker/ui.c) as PNGs, drawn on this computer as the FM-1 would show them: build/preview/*.png
#   tools/preview.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P="$ROOT/third_party/fomajam/platform"
OUT="$ROOT/build/preview"
mkdir -p "$OUT"
${CC:-cc} -std=c99 -O1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -Wall -Wno-unused-function -I"$ROOT/tools/preview" -I"$ROOT/picker" -I"$P/src" -I"$P/gen" \
  -I"$ROOT/third_party/fomajam/src" "$ROOT/tools/preview/preview.c" -o "$OUT/preview"
"$OUT/preview" "$OUT"
for f in "$OUT"/*.ppm; do
  if command -v sips >/dev/null; then
    sips -s format png "$f" --out "${f%.ppm}.png" >/dev/null
  else
    convert "$f" "${f%.ppm}.png"
  fi
  rm "$f"
done
echo "$OUT"
