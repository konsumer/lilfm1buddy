# lilfm1buddy

A tiny ESP32-S3 that plugs straight into the FM-1's USB port and lets you switch its firmware from the FM-1 itself:
no screen, no buttons, no MIDI. Plug it in, pick a firmware on the FM-1's screen with its keys, and it's installed.

Early: the picker and the install are written, not yet tried on hardware together.

## How it works

The FM-1's chip (JieLi WL82) has a boot loader in its ROM that no firmware can break. In it, the chip is a USB
storage device (`4C4A:8057`) that takes commands, among them "write this to RAM" and "jump there". fm1buddy already
uses that to run JieLi's `wl82loader.bin` for backups and recovery.

lilfm1buddy runs its own program there instead, the **picker**:

1. The ESP32 gets the FM-1 into its boot ROM: SysEx `F0 22 24 35 7D F7` from a firmware that takes it (stock V15,
   Felucca-based ones), or the USB key at power-on (any firmware, or none).
2. It writes the picker to the FM-1's RAM and jumps to it. Nothing is written to flash.
3. The picker starts the FM-1's screen, keys and USB, and shows the firmwares the ESP32 has (it asks over USB).
4. You pick one with the keys. The picker tells the ESP32.
5. The ESP32 sends the FM-1 back into its boot ROM (the picker takes the same SysEx), runs JieLi's loader there, and
   writes that firmware's flash image (from `0x4000` to its end), as fm1buddy's recovery does. (felucca's loader can write it itself, but its
   flash driver wants the flash as the firmware's startup leaves it: started by the boot ROM, it isn't.)

So: one install per switch, and no change to any firmware. Later, the ESP32 can also keep each firmware's projects
and settings: read the flash before switching away, and put them back when switching back.

## Why it can be small

[felucca-bsp](https://github.com/hugelton/felucca-bsp) (MPL-2.0, in `third_party/`) has most of the FM-1 side:

- `loader/`: Felucca's update loader. It already **runs from RAM** (at `0x01C0A800`, up to 80 KB), starts the screen
  (`ldr_screen.c`), is a USB-MIDI device and answers M-UPGRADE as `ota-FM-1_900`. The picker is this plus a menu.
- `boards/fm1/`: the LCD, key scan, knobs and LEDs.

The ESP32 side is fm1buddy's board code (the USB host, the key at power-on: `key_s3.cpp`, packages: `fm1.cpp`), and
its boot ROM protocol (`uboot.cpp`), in `esp32/rom.cpp` with its files from the board's flash.

## Plan

1. **Does a RAM image run from the boot ROM?** Yes (2026-10-30): felucca-bsp's loader, written to RAM and started
   by the boot ROM, drew its screen and came up on USB as `1209:0002`.
2. **The picker:** a menu on the screen, the keys, and a small SysEx exchange with the ESP32 (the list of firmwares,
   the choice). Then the install it already does.
3. **lilfm1buddy:** a PlatformIO build for a small ESP32-S3 (a Super Mini, battery) with fm1buddy's boot ROM, USB
   host and installer code, the firmwares in its flash (or an SD card), and a web page to add them.
4. **Keep each firmware's data** when switching (whole-flash backup and restore through the boot ROM).

## Open questions

- The loader is normally started by the SPL, after its clock and flash setup. From the boot ROM those may differ:
  the clock the ROM leaves running, the flash's state, and whether `0x01C0A800` is clear of what the ROM uses while
  it takes the image.
- Which ranges the picker may write: felucca's loader only writes `[0x4000, 0x97000)` and `[0xE0000, 0xFC000)`
  (Felucca's own layout). Other firmwares (stock, SLOOP) need what their packages hold.
- Power: the ESP32 on its own battery, and data and ground only to the FM-1 (no VBUS), as fm1-cartridge does. That
  may also be what fm1buddy's recovery needs (VBUS kept the FM-1's chip on when "off").
- The USB key at power-on is still unreliable in fm1buddy (konsumer/fm1buddy#41, #43). Firmwares that take the
  SysEx don't need it.

## Build

The picker needs JieLi's Linux toolchain (Felucca's `tools/get_toolchain.sh` fetches it) and, on macOS, Docker:

```sh
git submodule update --init
JIELI_TOOLCHAIN=~/.jieli/toolchain picker/build.sh   # build/picker.bin
```

## Use it

On an fm1buddy board (the CYD-S3) for now. It builds on fm1buddy's code (`third_party/fm1buddy`) with its own main
(`esp32/`). Everything it needs is in its own flash, no SD card:

```sh
JIELI_TOOLCHAIN=~/.jieli/toolchain picker/build.sh   # build/picker.bin
tools/data.sh                                        # data/: picker.bin, and JieLi's wl82loader.bin (checked)
cp ~/somewhere/*.fwsc data/fm1/                      # the firmwares (about 13 MB fit)
pio run -e cyd -t uploadfs                           # data/ into the board's flash
pio run -e cyd -t upload                             # the program
```

Then plug the board into the FM-1, both on battery, and reset the board:

- with a running stock V15 or Felucca-based firmware, it sends the FM-1 into its boot ROM by SysEx;
- otherwise (SLOOP, or no FM-1 found) it restarts into key mode: switch the FM-1 off, then on.

On the FM-1: turn any knob to move, press any button to pick (and again to be sure), or press a piano key to jump to
that row. The first row keeps what's on it (the FM-1 restarts, nothing written). The board then writes the one you
picked through the boot ROM and JieLi's loader: the sectors of its flash image that differ, from `0x4000` (the head,
what the chip starts from, is never written) to the image's end (`0x93000` for most; baudgirl `0xBA000`, stock V16
`0xA9000`). The flash past the image stays. Until a firmware is picked, nothing is written to the FM-1's flash.

On a computer, the board is a serial console: `third_party/fm1buddy/tools/s3.py log` prints what happened.

## Credits

- [felucca-bsp](https://github.com/hugelton/felucca-bsp) (Leo Kuroshita, Charles Vestal, and others; MPL-2.0): the
  FM-1's chip and board code, and the update loader the picker is built from. Files from it stay MPL-2.0.
- [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) (kurogedelic, MIT) and
  [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (kagaimiq, MIT): the boot ROM protocol and the key.
- [fm1buddy](https://github.com/konsumer/fm1buddy): the ESP32 side.

The rest is [GPL-3.0](LICENSE), like fm1buddy.
