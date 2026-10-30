# lilfm1buddy

A tiny ESP32-S3 that plugs straight into the M-VAVE FM-1's USB port and lets you switch its firmware from the FM-1
itself: no screen of its own, no buttons, no computer. Plug it in, pick a firmware on the FM-1's screen with its knobs
and buttons, and it's written.

**Install it, and manage its firmwares, from the web page: [konsumer.js.org/lilfm1buddy](https://konsumer.js.org/lilfm1buddy/)**
(Chrome or Edge on a computer).

Working on hardware (2026-10-30): fomajam and baudgirl installed from the FM-1's own menu.

## You need

One of these ESP32-S3 boards (the web page installs each):

| Board | Size | Firmwares | Power | LED |
| --- | --- | --- | --- | --- |
| **Seeed XIAO ESP32S3** (suggested) | 21 × 18 mm | about 10 (8 MB flash) | LiPo charger on board, BAT pads on its back | one colour: blinks |
| Waveshare ESP32-S3-Zero | 24 × 18 mm | 3 or 4 (4 MB flash) | none: a LiPo and a charger board on its 5V pin (3.7 to 6 V) | RGB on GPIO 21 |
| any ESP32-S3 N16R8 (DevKitC-1, ...) | bigger | about 20 (16 MB flash) | its other USB port (a power bank) | RGB on GPIO 48 |

And a battery: the board is the FM-1's USB host, each on its own battery, one USB-C data cable between them, no
hub. It's only on for a minute or two at a time: a single-cell (1S, 3.7 V) LiPo **with a protection circuit**, 300 to
500 mAh, is plenty (the XIAO charges it at about 100 mA). Check its polarity with a meter before soldering it to the
XIAO's pads: the wires on these packs aren't always where their plug says.

## Use it

1. Plug the board into the FM-1 and switch both on.
2. With stock V15 or a Felucca-based firmware running, the board sends the FM-1 into its boot loader by itself. With
   any other (SLOOP, ...), or none, the LED goes yellow: switch the FM-1 off, then on.
3. The FM-1 shows the list (LED cyan). Turn any knob to move, press any button to pick (and again to be sure), or a
   piano key to jump to its row. The top row keeps what's on it.
4. The board writes it (magenta), and the FM-1 starts it (green). Red: it didn't; the page's Firmwares tab shows the
   board's log. (The XIAO's one LED blinks instead: the page's "Use it" tab has them.)

Until a firmware is picked, nothing is written. Only the firmware's own image is written (from `0x4000` to its end:
`0x93000` for most, baudgirl `0xBA000`, stock V16 `0xA9000`), never the flash's head (what the chip starts from), and
the chip's boot loader is in its ROM: nothing here can make the FM-1 unrecoverable.

## How it works

The FM-1's chip (JieLi WL82) has a boot loader in its ROM that no firmware can break. In it, the chip is a USB storage
device (`4C4A:8057`) that takes commands, among them "write this to RAM" and "jump there".

1. The ESP32 gets the FM-1 into its boot ROM: SysEx `F0 22 24 35 7D F7` from a firmware that takes it, or the USB key
   at power-on (fm1buddy's `key_s3.cpp`, after FM-1-transporter).
2. It writes the **picker** (`picker/`) to the FM-1's RAM and jumps to it. Nothing is written to flash.
3. The picker starts the FM-1's screen, keys and USB (as a USB-MIDI device), asks the ESP32 for its list over SysEx,
   and shows it. You pick one; the picker says which (`picker/picker.c` has the messages).
4. The ESP32 sends the FM-1 back into its boot ROM (the picker takes the same SysEx), runs JieLi's `wl82loader.bin`
   there, and writes the sectors of that firmware's flash image that differ, as fm1buddy's recovery does.

The picker is [felucca-bsp](https://github.com/hugelton/felucca-bsp)'s update loader (it runs from RAM) with a menu
in front, and its screen and key scan. felucca's loader can't write the flash itself here: its flash driver wants the
flash as the firmware's startup leaves it, and the boot ROM doesn't.

The ESP32 side (`esp32/`) builds on fm1buddy's board code (`third_party/fm1buddy`: the USB host, the key, packages),
with the boot ROM protocol in `esp32/rom.cpp`. The firmwares and JieLi's loader are files in the board's flash
(LittleFS: the rest of its flash). On a computer, the board is a serial port that the web page talks to
(`esp32/main.cpp` has the commands).

## Build

The picker needs JieLi's Linux toolchain (Felucca's `tools/get_toolchain.sh` fetches it) and, on macOS, Docker. The
board's program has the picker in it:

```sh
git submodule update --init
JIELI_TOOLCHAIN=~/.jieli/toolchain picker/build.sh   # build/picker.bin
pio run -e xiao -t upload                            # the board: xiao, zero or devkit
npm run fmt                                          # the web page's formatting
```

`pio run -e cyd` is the same on an fm1buddy board (the CYD-S3), with what it's doing on its screen too: handy while
working on it. CI (`.github/workflows/build.yml`) builds both, and on `main` puts the page on GitHub Pages with the
program next to it.

## Later

- Keep each firmware's data (projects, settings) when switching: the board reads the FM-1's flash anyway, it could
  keep what's past the image for each firmware and put it back.
- Firmwares from the FM-1 Firmware Hub on the page, not only from files.

## Credits

- [felucca-bsp](https://github.com/hugelton/felucca-bsp) (Leo Kuroshita, Charles Vestal, and others; MPL-2.0): the
  FM-1's chip and board code, and the update loader the picker is built from. Files from it stay MPL-2.0.
- [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) (kurogedelic, MIT) and
  [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (kagaimiq, MIT): the boot ROM protocol and the key.
  JieLi's `wl82loader.bin` isn't included: the page fetches it from jl-uboot-tool.
- [fm1buddy](https://github.com/konsumer/fm1buddy): the ESP32 side.

The rest is [GPL-3.0](LICENSE), like fm1buddy.
