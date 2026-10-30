#!/bin/sh
# Build the picker's RAM image: build/picker.bin (load and jump to 0x01C0A800). It's felucca-bsp's update loader
# (its crt0 and linker script, ldr_core.c) with a menu in front (picker.c, screen.c).
#   JIELI_TOOLCHAIN=~/.jieli/toolchain picker/build.sh
# The toolchain is Linux-only (felucca's tools/get_toolchain.sh fetches it). On macOS it runs in Docker.
set -e
TC="$(cd "${JIELI_TOOLCHAIN:?JIELI_TOOLCHAIN must point at the JieLi Linux toolchain}" && pwd -P)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BSP=third_party/felucca-bsp
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
FLAGS="-target pi32v2 -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function -Ipicker -I$BSP/chip -I$BSP/boards/fm1 -I$BSP/loader"
run $CC -target pi32v2 -c $BSP/loader/crt0_ldr.S -o $OUT/crt0_ldr.o
run $CC $FLAGS -c picker/picker.c -o $OUT/picker.o
run pi32v2/bin/ld --gc-sections -e _start -T $BSP/loader/loader.ld $OUT/crt0_ldr.o $OUT/picker.o -o $OUT/picker.elf
run common/bin/objcopy -O binary -j .text $OUT/picker.elf $OUT/picker.bin
echo "picker.bin: $(wc -c <"$ROOT/$OUT/picker.bin") bytes (at most 81920)"
