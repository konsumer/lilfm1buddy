// The FM-1's boot ROM (its chip's, JieLi WL82): getting there, and running a program of ours in its RAM. The
// protocol is fm1buddy's uboot.cpp (after FM-1-transporter and jl-uboot-tool), without JieLi's loader.
#pragma once

#include <stdint.h>

#define ROM_VID 0x4C4A
#define ROM_PID 0x8057
#define PICKER_AT 0x01C0A800u  // felucca-bsp's loader.ld
#define PICKER_MAX 0x14000u

namespace rom {
// a running FM-1 (MIDI device d) into its boot ROM, by SysEx (stock V15, Felucca-based firmwares); 0 or an error
const char* enter(int d);
// the boot ROM there and answering, within ms (after enter, or the key); 0 or an error
const char* attach(uint32_t ms);
// image (plain, as the build makes it) into RAM at addr, then jump there; 0 or an error
const char* run(uint32_t addr, const uint8_t* image, uint32_t n);
}  // namespace rom
