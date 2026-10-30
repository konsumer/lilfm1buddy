#!/bin/sh
# Fill data/ for the board's flash (then: pio run -t uploadfs): the picker (picker/build.sh first), and JieLi's loader
# (from jl-uboot-tool, checked by its hash). The firmwares go in data/fm1/ (.fwsc), as many as fit (about 13 MB).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
URL=https://raw.githubusercontent.com/kagaimiq/jl-uboot-tool/c32b28e7a04f873f526ba929cb1bae9f05442fe9/data/loaderblobs/usb/wl82loader.bin
SHA=d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376
mkdir -p "$ROOT/data/fm1"
cp "$ROOT/build/picker.bin" "$ROOT/data/picker.bin"
if [ "$(shasum -a 256 "$ROOT/data/wl82loader.bin" 2>/dev/null | cut -d' ' -f1)" != "$SHA" ]; then
  curl -fsSL "$URL" -o "$ROOT/data/wl82loader.bin"
  [ "$(shasum -a 256 "$ROOT/data/wl82loader.bin" | cut -d' ' -f1)" = "$SHA" ] || { echo "wl82loader.bin: wrong hash"; exit 1; }
fi
du -sh "$ROOT/data"
