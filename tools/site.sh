#!/bin/sh
# The page's program (each board's) and menus next to it in site/fw, as CI publishes them. After picker/build.sh and
# pio run (the boards built so far: the others are left out).
set -e
cd "$(dirname "$0")/.."
version="$(git describe --tags --always --dirty)"
mkdir -p site/fw
cp build/lilfm1.bin build/lilsmk.bin site/fw/
for b in xiaoplus xiao zero devkit; do
  [ -f .pio/build/$b/firmware.factory.bin ] || continue
  mkdir -p site/fw/$b
  cp .pio/build/$b/firmware.factory.bin site/fw/$b/lilfm1buddy.bin
  printf '{"name": "lilfm1buddy", "version": "%s", "new_install_prompt_erase": true, "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": "lilfm1buddy.bin", "offset": 0}]}]}\n' "$version" >site/fw/$b/manifest.json
done
echo "site/fw: $(ls site/fw | tr '\n' ' ')"
