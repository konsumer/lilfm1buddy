// The FM-1's boot ROM (its chip's, JieLi WL82): getting there, running a program of ours in its RAM, and JieLi's
// loader for its flash. The protocol is fm1buddy's uboot.cpp (after FM-1-transporter and jl-uboot-tool), with the
// files from wherever lilfm1buddy keeps them (not an SD card).
#pragma once

#include <stdint.h>

#include <vector>

#define ROM_VID 0x4C4A
#define ROM_PID 0x8057
#define PICKER_AT 0x01C0A800u  // the menus', fomajam's platform/loader/loader.ld
#define PICKER_MAX 0x14000u
#define LOADER_AT 0x01C02000u  // JieLi's wl82loader.bin
#define LOADER_SIZE 24064u
#define LOADER_SHA256 "d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376"
#define SECTOR 0x1000u
#define WRITE_FROM 0x4000u  // below: the flash's head (what the chip starts from), never written
#define WRITE_TO 0xFF000u   // above: key_mac
#define FM1_KEY 0x980F
// its flash: 1 MiB SPI NOR, one of two makers' parts (the same type and size, written the same way)
#define FM1_FLASH 0x856014u    // the FM-1's up to 2026
#define FM1_FLASH_2 0xB36014u  // in newer FM-1s
static inline bool fm1Flash(uint32_t id) {
  return id == FM1_FLASH || id == FM1_FLASH_2;
}

namespace rom {
struct Chip {
  uint16_t key;      // its chip key (0x980F)
  uint32_t flashId;  // its flash's JEDEC id (0x856014 or 0xB36014: 1 MiB)
  uint32_t size;     // the flash's size
  uint32_t io;       // the most a read moves at once
};

// A program that runs in the ROM's place, where the ROM hands its USB storage commands to it: the FM-1's flash, and
// its boot ROM's jobs. JieLi's (wl82loader.bin), or ours (mflr-loader.bin: m-bootloader's, with the FM-1's screen)
struct Loader {
  const char* name;  // its file on the drive
  uint32_t at;       // where it links, and is run
  bool plain;        // a plain image (the ROM ciphers it), or as it comes (already ciphered)
  bool screen;       // it can draw on the FM-1's (or SMK-37's) screen itself: the map, and what's going on
  uint32_t max;      // the most it may be
};
extern const Loader JIELI;   // JieLi's: what its firmware's flashers use
extern const Loader MFLR;    // ours: the same commands, and the screen (loader/)

// the map's states, one for each of its 256 sectors (screenMap)
enum MapState : uint8_t {
  LMAP_NEW,      // not touched
  LMAP_DIFFERS,  // to write
  LMAP_WRITTEN,
  LMAP_SAME,   // already what it should be
  LMAP_READ,   // read (a backup)
  LMAP_FAILED,
};

// a running FM-1 (MIDI device d) into its boot ROM, by SysEx (stock V15, Felucca-based firmwares, the menu); 0 or
// an error
const char* enter(int d);
// the boot ROM there and answering, within ms (after enter, or the key); 0 or an error
const char* attach(uint32_t ms);
// image (plain, as the build makes it) into RAM at addr, then jump there; 0 or an error
const char* run(uint32_t addr, const uint8_t* image, uint32_t n);
// a loader into RAM and started, and what it says of the chip; 0 or an error
const char* loader(const Loader& l, const uint8_t* image, uint32_t n, Chip& chip);
// the loader's own screen (Loader::screen, after loader()): the panel (1: the FM-1, 2: the SMK-37), a row of 20
// cells (row 0..14), and the flash's 256 sectors; false if it didn't take it
bool screenInit(uint32_t board);
bool screenRow(uint32_t row, uint16_t fg, uint16_t bg, const char* text);
bool screenMap(const uint8_t* states);
// with the loader running: n bytes of its flash from addr; one sector, erased, written and read back; back to its
// firmware
const char* read(const Chip& chip, uint32_t addr, uint8_t* out, uint32_t n);
const char* writeSector(uint32_t addr, const uint8_t* data);
void leave();
}  // namespace rom
