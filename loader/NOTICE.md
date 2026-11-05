# loader

lilfm1buddy's flash loader: it runs in the FM-1's RAM where the chip's boot ROM hands over its USB storage commands,
the same place, and the same commands, as JieLi's `wl82loader.bin`. The board sends it there, runs it, and writes or
reads the flash through it (`esp32/rom.cpp`), as fm1buddy's recovery does.

It's [m-bootloader](https://github.com/jvitkauskas/m-bootloader)'s flash loader (MIT), from commit
`d131391a398dc9a757b54ea91ab3cc71ef939d14` (release `2.0-c43a32f`): `src/` and `include/` are its `loader`,
`loader_target`, `display`, `platform`, `core` and `runtime`, and `tools/target.ld.S` its link script. `LICENSE` is its
(MIT). Its specification, `docs/SPEC.md` there, is the protocol.

What lilfm1buddy adds: `LMAP` — the flash as 16 × 16 sectors of 4 KiB, a byte a sector, drawn as it's read or written
(`FC47`: `display_map` in `src/display.c`, `src/loader.c`), so the FM-1 shows what's going on instead of a still
screen. Colours, one a state: dark, not touched; amber, to write; green, written; dim green, already the same; cyan,
read; red, failed. The board draws the two rows above it (`FC44`), with the percentage.

Build it (the toolchain is [pi32v2-llvm](https://github.com/jvitkauskas/pi32v2-llvm), `pi32v2-23.1.5-2`, not JieLi's):

```sh
PI32V2_TOOLCHAIN=~/pi32v2-llvm loader/build.sh   # build/mflr-loader.bin, and esp32/mflr_loader.h (in the board's program)
```

The board has the loader in its program, so nothing has to be put on its drive (and no page, no Wi-Fi). A
`mflr-loader.bin` on the drive is used instead, if there is one: a newer build, to try without flashing the board.
JieLi's `wl82loader.bin`, if it's on the drive, is the second thing tried, if ours doesn't start.
