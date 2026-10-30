// The FM-1's boot ROM (its chip's, JieLi WL82): getting there, running a program of ours in its RAM, and JieLi's
// loader for its flash. The protocol is fm1buddy's uboot.cpp (after FM-1-transporter and jl-uboot-tool), with the
// files from wherever lilfm1buddy keeps them (not an SD card).
#pragma once

#include <stdint.h>

#include <vector>

#define ROM_VID 0x4C4A
#define ROM_PID 0x8057
#define PICKER_AT 0x01C0A800u  // felucca-bsp's loader.ld
#define PICKER_MAX 0x14000u
#define LOADER_AT 0x01C02000u  // JieLi's wl82loader.bin
#define LOADER_SIZE 24064u
#define LOADER_SHA256 "d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376"
#define SECTOR 0x1000u
#define WRITE_FROM 0x4000u  // below: the flash's head (what the chip starts from), never written
#define WRITE_TO 0xFF000u   // above: key_mac
#define FM1_KEY 0x980F
#define FM1_FLASH 0x856014u

namespace rom {
struct Chip {
  uint16_t key;      // its chip key (0x980F)
  uint32_t flashId;  // its flash's JEDEC id (0x856014: 1 MiB)
  uint32_t size;     // the flash's size
  uint32_t io;       // the most a read moves at once
};

// a running FM-1 (MIDI device d) into its boot ROM, by SysEx (stock V15, Felucca-based firmwares, the picker); 0 or
// an error
const char* enter(int d);
// the boot ROM there and answering, within ms (after enter, or the key); 0 or an error
const char* attach(uint32_t ms);
// image (plain, as the build makes it) into RAM at addr, then jump there; 0 or an error
const char* run(uint32_t addr, const uint8_t* image, uint32_t n);
// JieLi's loader (wl82loader.bin as it comes: already ciphered) into RAM and started, and what it says of the chip;
// 0 or an error
const char* loader(const uint8_t* image, uint32_t n, Chip& chip);
// with the loader running: the whole flash; one sector, erased, written and read back; back to its firmware
const char* read(const Chip& chip, std::vector<uint8_t>& out, void (*progress)(uint32_t done, uint32_t total));
const char* writeSector(uint32_t addr, const uint8_t* data);
void leave();
}  // namespace rom
