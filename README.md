# lilfm1buddy

A tiny ESP32-S3 that plugs straight into the M-VAVE FM-1's USB port and lets you switch its firmware from the FM-1
itself: no screen of its own, no computer. Plug it in, press its BOOT button, pick a firmware (or a backup) on the
FM-1's screen with its knobs and buttons, and it's written. On a powered hub with other MIDI devices, it routes MIDI
between them too.

**Install it, and manage its files, from the web page: [konsumer.js.org/lilfm1buddy](https://konsumer.js.org/lilfm1buddy/)**
(Chrome or Edge on a computer).

Working on hardware (2026-10-30): fomajam and baudgirl installed from the FM-1's own menu.

## You need

- a [Seeed XIAO ESP32S3 Plus](https://www.aliexpress.us/item/3256808143430495.html): 21 × 18 mm, room for about 20
  firmwares, a battery charger on board, and it reads its battery (the FM-1 shows its level)
- a [3.7 V 180 mAh LiPo with bare wires](https://www.aliexpress.us/item/3256802077349750.html), soldered to the BAT+
  and BAT− pads on the XIAO's back. Check the polarity with a meter first: the wire colours on these aren't always
  right. A firmware switch takes a few mAh; the XIAO charges it from its USB-C (its red LED flashes while it does)

No power switch: when it's done, or nothing happens for a while (two minutes with nothing to do, ten for a pick), the
board goes to deep sleep, months on that battery. **Its BOOT button (or reset) wakes it.**

The board is the FM-1's USB host: each on its own battery, one USB-C data cable between them (or a powered hub).

## Use it

On a computer, the board is a USB drive, `LILBUDDY` (its LED cyan): put firmwares (`.fwsc`) and JieLi's `wl82loader.bin`
on it (the web page's Drive tab does both). Eject it and it's a serial port (blue) until it's unplugged: for installing
its program, and its log. Its program installs from its download mode too (hold BOOT, tap reset), which nothing can
break. Dot files a computer leaves on the drive (macOS's `._` copies, trash, indexes) are cleared away by the board,
and macOS is told not to make most of them. On the drive:

- `*.fwsc`: firmwares; `*.bin`: backups, `fm1-N.bin` (or `smk-N.bin`, an SMK-37's: the FM-1's chip in a keyboard,
  which it installs and backs up too). Each only goes onto its own kind: never an FM-1's onto an SMK-37, nor the other
  way round, and an SMK-37's own calibration is never written
- `wl82loader.bin`: JieLi's loader, for writing the FM-1's flash
- `lilfm1.bin`, `lilsmk.bin`: the menus on the FM-1's and the SMK-37's screens (`picker/`, run from their RAM), for
  the devices you have: the web page puts the latest there, so the board itself rarely needs updating. The board asks
  the running firmware which it is, and loads its menu
- `theme.txt`: the menus' colors, one of [fomajam](https://github.com/konsumer/fomajam)'s themes by name: `BLACK` (the
  default), `GREY`, `BLUE`, `GREEN`, `FOREST`, `PURPLE`, `RED`, `ORANGE`, `TEAL`, `WHITE` or `NEON`
- `midimap.txt`: MIDI routing on the FM-1, with [fm1buddy](https://github.com/konsumer/fm1buddy)'s rules (none: no
  routing). A line `bluetooth` turns on Bluetooth MIDI (phones and computers connect to it, as `fm1buddy`), and
  `bluetooth NAME` connects to a Bluetooth MIDI device whose name has NAME in it
- `log.txt`: what happened last time on the FM-1

On the FM-1:

1. Plug the board into the FM-1, switched on (or a powered hub, with the FM-1 and other MIDI devices). The LED goes
   green: dim, or bright while it routes MIDI (`midimap.txt`), flashing as it goes through.
2. Press BOOT. With stock V15 or a Felucca-based firmware running, the board sends the FM-1 into its boot loader by
   itself. With any other (SLOOP, ...), or none, the LED blinks yellow: switch the FM-1 off, then on (straight on the
   board for this: no hub).
3. The FM-1 shows the list, and both batteries at the top: "Back up this FM-1", then its firmwares and backups. Turn
   SEL to move, press OCT+ to pick, and OCT+ again to be sure (anything else: back). To keep what's on it, switch the
   FM-1 off and on.
4. The board writes it (magenta), or backs it up to `fm1-N.bin` (white), and the FM-1 starts: the LED green. Three
   red blips and a pause: it didn't (`log.txt` says why). Then the board goes to sleep.

Until something is picked, nothing is written. Only the image is written (from `0x4000` to its end: `0x93000` for most
firmwares, baudgirl `0xBA000`, stock V16 `0xA9000`, a backup `0xFF000`), never the flash's head (what the chip starts
from), and the chip's boot loader is in its ROM: nothing here can make the FM-1 unrecoverable.

## How it works

The FM-1's chip (JieLi WL82) has a boot loader in its ROM that no firmware can break. In it, the chip is a USB storage
device (`4C4A:8057`) that takes commands, among them "write this to RAM" and "jump there".

1. The ESP32 gets the FM-1 into its boot ROM: SysEx `F0 22 24 35 7D F7` from a firmware that takes it, or the USB key
   at power-on (fm1buddy's `key_s3.cpp`, after FM-1-transporter).
2. It asks the running firmware which it is (an FM-1 or an SMK-37), and writes that one's **menu** (`lilfm1.bin` or
   `lilsmk.bin` on its drive: `picker/`) to its RAM and jumps to it. Nothing is written to
   flash.
3. The menu starts the FM-1's screen, keys and USB (as a USB-MIDI device), and asks the ESP32 for what it needs with
   its commands, a line of text in each SysEx (`esp32/main.cpp` lists them: `info`, `ls`, `read`, `install`,
   `backup`, `run`, ...). The same commands work on the ESP32's serial port, and from any FM-1 firmware: one could
   switch firmwares from its own menu.
4. For `install`, the ESP32 sends the FM-1 back into its boot ROM (the menu takes the same SysEx), runs JieLi's
   `wl82loader.bin` there, and writes the sectors of that firmware's flash image that differ, as fm1buddy's recovery
   does. A backup is the same, read instead.

The menu is [fomajam](https://github.com/konsumer/fomajam)'s update loader (it runs from RAM; from
[felucca-bsp](https://github.com/hugelton/felucca-bsp)'s) with a menu in front, built on fomajam's platform for each
device (`third_party/fomajam/platform`: the FM-1's, and the SMK-37's screen, keys and knobs). The loader can't write
the flash itself here: its flash driver wants the
flash as the firmware's startup leaves it, and the boot ROM doesn't. If it learns to, the menu could write firmwares
itself (reading them with `read`), with no change to the ESP32.

The ESP32 side (`esp32/`) builds on the parts of fm1buddy's code it needs (`third_party/fm1buddy`: the USB host, the
key, MIDI routing, Bluetooth MIDI, packages), with the boot ROM protocol in `esp32/rom.cpp`. All of its flash past the
program is the drive (`esp32/disk.cpp`): FAT, 512-byte sectors, straight on the flash (no wear levelling: it's
written now and then, so none of the flash goes to that).

## Build

The menus need JieLi's Linux toolchain (Felucca's `tools/get_toolchain.sh` fetches it) and, on macOS, Docker. The
board's program has them in it:

```sh
git submodule update --init
JIELI_TOOLCHAIN=~/.jieli/toolchain picker/build.sh   # build/lilfm1.bin, build/lilsmk.bin
pio run -e xiaoplus -t upload                        # the board
npm run fmt                                          # the web page's formatting
npm start                                            # the web page at localhost:8080 (reloads on changes), with what's built
```

`tools/lil.py` drives a board on this computer, with no buttons (for a script, or an agent working on it): `disk`
(mounts its drive, prints where), `eject` (its serial port), `flash ENV` (installs the program, then it's the drive
again), and any of its commands (`info`, `ls`, `log`, `read OFF LEN NAME`, `rm NAME`). A new menu: build it
(`picker/build.sh`), and copy `build/lilfm1.bin` (or `lilsmk.bin`) to the drive. Run it with PlatformIO's Python, which has pyserial:
`~/.platformio/penv/bin/python tools/lil.py flash zero`.

Other ESP32-S3 boards with PSRAM build too (`platformio.ini`: `xiao`, the XIAO ESP32S3, 8 MB; `zero`, the Waveshare
ESP32-S3-Zero, 4 MB, RGB LED; `devkit`, any N16R8), with `pio run -e NAME -t upload`. The drive is the flash past the
program: about 3 MB on a 4 MB board (5 firmwares, or 3 backups), 7 MB on 8 MB, 15 MB on 16 MB. CI
(`.github/workflows/build.yml`) builds them all, and on `main` puts the page on GitHub Pages with the program next to
it.

## Later

- Keep each firmware's data (projects, settings) when switching: the board reads the FM-1's flash anyway, it could
  keep what's past the image for each firmware and put it back.

## Credits

- [felucca-bsp](https://github.com/hugelton/felucca-bsp) (Leo Kuroshita, Charles Vestal, and others; MPL-2.0): the
  FM-1's chip and board code, and the update loader the menus are built from (by way of fomajam). Files from it stay
  MPL-2.0.
- [fomajam](https://github.com/konsumer/fomajam) (GPL-3.0): the platform the menus are built on, with the SMK-37's
  layer by M. Geisler ([genkigenki/Felucca](https://github.com/genkigenki/Felucca), branch smk37; hardware notes:
  [genkigenki/fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37)).
- [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) (kurogedelic, MIT) and
  [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (kagaimiq, MIT): the boot ROM protocol and the key.
  JieLi's `wl82loader.bin` isn't included: the page fetches it from jl-uboot-tool.
- [fm1buddy](https://github.com/konsumer/fm1buddy): the ESP32 side.

The rest is [GPL-3.0](LICENSE), like fm1buddy.
