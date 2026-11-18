#!/bin/sh
# The menu in a browser (tools/emu/emu.c): site/fw/menu.wasm, for the web page's "Try it". It needs a clang that
# builds for WebAssembly, with wasm-ld: LLVM's (not Apple's), or emscripten's ($WASM_CC, else found).
#   tools/emu.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P="$ROOT/third_party/fomajam/platform"
CC="${WASM_CC:-}"
if [ -z "$CC" ]; then
  for c in "$(em-config LLVM_ROOT 2>/dev/null)/clang" /opt/homebrew/opt/llvm/bin/clang clang; do
    if echo 'int x;' | "$c" --target=wasm32 -nostdlib -Wl,--no-entry -x c - -o /dev/null 2>/dev/null; then
      CC="$c"
      break
    fi
  done
fi
[ -n "$CC" ] || { echo "emu.sh: no clang that builds for WebAssembly (LLVM's, or emscripten's)"; exit 1; }
mkdir -p "$ROOT/site/fw"
"$CC" --target=wasm32 -O2 -nostdlib -fno-builtin -Wall -Wno-unused-function -Wno-incompatible-library-redeclaration \
  -I"$ROOT/tools/preview" -I"$ROOT/picker" -I"$P/src" -I"$P/gen" -I"$ROOT/third_party/fomajam/src" \
  -Wl,--no-entry -Wl,--export=emu_init -Wl,--export=emu_boot -Wl,--export=emu_fb -Wl,--export=emu_frame \
  "$ROOT/tools/emu/emu.c" -o "$ROOT/site/fw/menu.wasm"
echo "site/fw/menu.wasm: $(wc -c <"$ROOT/site/fw/menu.wasm") bytes"
