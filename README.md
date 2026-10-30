# lilfm1buddy

A tiny ESP32-S3 that plugs straight into the M-VAVE FM-1's USB port and lets you switch its firmware from the FM-1
itself: no screen of its own, no buttons, no computer. Plug it in, pick a firmware on the FM-1's screen with its knobs
and buttons, and it's written.

**Install it, and manage its firmwares, from the web page: [konsumer.js.org/lilfm1buddy](https://konsumer.js.org/lilfm1buddy/)**
(Chrome or Edge on a computer).

Working on hardware (2026-10-30): fomajam and baudgirl installed from the FM-1's own menu.

## You need

- a [Seeed XIAO ESP32S3 Plus](https://www.aliexpress.us/item/3256808143430495.html): 21 × 18 mm, room for about 20
  firmwares, a battery charger on board, and it reads its battery (the FM-1 shows its level)
- a [3.7 V 180 mAh LiPo with bare wires](https://www.aliexpress.us/item/3256802077349750.html), soldered to the BAT+
  and BAT− pads on the XIAO's back. Check the polarity with a meter first: the wire colours on these aren't always
  right. A firmware switch takes a few mAh; the XIAO charges it from its USB-C (its red LED flashes while it does)

No power switch: when it's done, or nothing happens for a while (two minutes waiting for the FM-1 to be switched on,
ten for a pick), the board goes to deep sleep, months on that battery. **Its reset button wakes it.**

The board is the FM-1's USB host: each on its own battery, one USB-C data cable between them, no hub.

## Use it

1. Wake the board (its reset button) and plug it into the FM-1, both switched on. Its LED blips once a second while
   it looks for the FM-1.
2. With stock V15 or a Felucca-based firmware running, the board sends the FM-1 into its boot loader by itself. With
   any other (SLOOP, ...), or none, the LED blinks slowly: switch the FM-1 off, then on.
3. The FM-1 shows the list, and both batteries at the top (the LED on, with a short gap each second). Turn any knob
   to move, press any button to pick (and again to be sure), or a piano key to jump to its row. To keep what's on
   it, switch the FM-1 off and on.
4. The board writes it (a fast blink), and the FM-1 starts it: the LED on. Three blips and a pause: it didn't (the
   page's Firmwares tab shows the board's log). Then the board goes to sleep.

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
pio run -e xiaoplus -t upload                        # the board
npm run fmt                                          # the web page's formatting
```

Other ESP32-S3 boards with PSRAM build too (`platformio.ini`: `xiao`, the XIAO ESP32S3, 8 MB; `zero`, the Waveshare
ESP32-S3-Zero, 4 MB, RGB LED; `devkit`, any N16R8), with `pio run -e NAME -t upload`. `pio run -e cyd` is the same on
an fm1buddy board (the CYD-S3), with what it's doing on its screen too: handy while
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
