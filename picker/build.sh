#!/bin/sh
# Build the menus' RAM images (load and jump to 0x01C0A800): build/lilfm1.bin for the FM-1, build/lilsmk.bin for the
# SMK-37. Each is fomajam's platform (third_party/fomajam/platform: its update loader's crt0 and linker script, and
# its hardware layer for each, FOMA_SMK37) with the menu (picker.c, screen.c).
#   JIELI_TOOLCHAIN=~/.jieli/toolchain picker/build.sh
# The toolchain is Linux-only (felucca's tools/get_toolchain.sh fetches it). On macOS it runs in Docker.
set -e
TC="$(cd "${JIELI_TOOLCHAIN:?JIELI_TOOLCHAIN must point at the JieLi Linux toolchain}" && pwd -P)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
P=third_party/fomajam/platform
OUT=build
mkdir -p "$ROOT/$OUT"

run() {  # TOOL ARGS... (paths relative to ROOT)
  t="$1"
  shift
  if [ "$(uname)" = Linux ]; then
    (cd "$ROOT" && "$TC/$t" "$@")
  else
    docker run --rm --platform linux/amd64 -v "$ROOT:/work" -v "$TC:/opt/jieli:ro" -w /work \
      "${JIELI_DOCKER_IMAGE:-debian:bookworm-slim}" "/opt/jieli/$t" "$@"
  fi
}

CC="pi32v2/bin/clang"
FLAGS="-target pi32v2 -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function -Ipicker -I$P/hal -I$P/src -I$P/loader"
run $CC -target pi32v2 -c $P/loader/crt0_ldr.S -o $OUT/crt0_ldr.o
for m in fm1:0 smk:1; do
  name=lil${m%:*}
  run $CC $FLAGS -DFOMA_SMK37=${m#*:} -c picker/picker.c -o $OUT/$name.o
  run pi32v2/bin/ld --gc-sections -e _start -T $P/loader/loader.ld $OUT/crt0_ldr.o $OUT/$name.o -o $OUT/$name.elf
  run common/bin/objcopy -O binary -j .text $OUT/$name.elf $OUT/$name.bin
  echo "$name.bin: $(wc -c <"$ROOT/$OUT/$name.bin") bytes (at most 81920)"
done
