#include "rom.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "hal.h"
#include "midi.h"

#define BLOCK 512  // what WRITE_MEMORY takes at once, and what the ROM deciphers by
#define WRITE_MEMORY 0xFB06
#define JUMP 0xFB08

static void wait(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static void be32(uint8_t* p, uint32_t v) {
  p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = v;
}

// CRC-16/XMODEM (poly 0x1021), from init
static uint16_t crc16(const uint8_t* p, uint32_t n, uint16_t crc) {
  while (n--) {
    crc ^= (uint16_t)*p++ << 8;
    for (int b = 0; b < 8; b++) {
      crc = crc & 0x8000 ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

// JieLi's jl_crc_cipher, key 0xFFFFFFFF: the WL82's ROM deciphers what WRITE_MEMORY brings with it, a block at a
// time (jl-uboot-tool: memory-rw-mengli-crypt)
static void cipher(uint8_t* p, uint32_t n) {
  static const uint8_t MAGIC[16] = {0xC3, 0xCF, 0xC0, 0xE8, 0xCE, 0xD2, 0xB0, 0xAE,
                                    0xC4, 0xE3, 0xA3, 0xAC, 0xD3, 0xF1, 0xC1, 0xD6};
  static const uint8_t KEY[2] = {0xFF, 0xFF};
  uint16_t crc = crc16(KEY, 2, 0xFFFF);
  for (uint32_t i = 0; i < n; i++) {
    crc = crc16(&MAGIC[i % 16], 1, crc);
    p[i] ^= (uint8_t)crc;
  }
}

static void cdbOf(uint8_t cdb[16], uint16_t cmd, const uint8_t* args, int n) {
  memset(cdb, 0xFF, 16);
  cdb[0] = cmd >> 8, cdb[1] = cmd & 0xFF;
  memcpy(cdb + 2, args, n);
}

const char* rom::enter(int d) {
  static const uint8_t KEY[] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
  midi::thru = false;
  bool sent = midi::sendWait(d, KEY, sizeof KEY, 1000);
  midi::thru = true;
  return sent ? 0 : "It didn't take MIDI.";
}

const char* rom::attach(uint32_t ms) {
  for (uint32_t start = hal::millis(); !hal::storageThere(ROM_VID, ROM_PID);) {
    if (hal::millis() - start > ms) {
      return hal::storageSeen(ROM_VID, ROM_PID) ? "Its boot ROM came, but couldn't be opened."
                                                : "Its boot ROM didn't show up.";
    }
    wait(50);
  }
  // it answers some time after it's there: INQUIRY until it does
  static const uint8_t INQUIRY[6] = {0x12, 0, 0, 0, 36, 0};
  uint8_t inq[36];
  wait(1000);
  for (int i = 0; i < 30; i++) {
    uint32_t n = 0;
    if (hal::storageCommand(INQUIRY, sizeof INQUIRY, true, inq, sizeof inq, &n, 1000) && n == sizeof inq) {
      hal::log("rom: %.8s %.16s %.4s", (const char*)inq + 8, (const char*)inq + 16, (const char*)inq + 32);
      return 0;
    }
    wait(300);
  }
  return "Its boot ROM didn't answer.";
}

const char* rom::run(uint32_t addr, const uint8_t* image, uint32_t n) {
  std::vector<uint8_t> b(BLOCK);
  for (uint32_t off = 0; off < n; off += BLOCK) {
    uint32_t len = std::min<uint32_t>(BLOCK, n - off);
    memcpy(b.data(), image + off, len);
    cipher(b.data(), len);
    uint8_t args[9], cdb[16];
    be32(args, addr + off);
    args[4] = len >> 8, args[5] = len & 0xFF, args[6] = 0;
    uint16_t c = crc16(b.data(), len, 0);
    args[7] = c & 0xFF, args[8] = c >> 8;
    cdbOf(cdb, WRITE_MEMORY, args, sizeof args);
    bool ok = false;
    for (int t = 0; t < 3 && !ok; t++) {  // (the same block again: the same RAM)
      ok = hal::storageCommand(cdb, 16, false, b.data(), len, 0, 2000);
    }
    if (!ok) {
      hal::log("rom: WRITE_MEMORY %08X failed", (unsigned)(addr + off));
      return "Its boot ROM didn't take the picker.";
    }
  }
  hal::log("rom: %u bytes at %08X", (unsigned)n, (unsigned)addr);
  // the jump: its answer may be lost (the program has the USB port by then). Gone from USB within a few seconds:
  // it jumped
  uint8_t args[6], cdb[16], resp[16];
  be32(args, addr);
  args[4] = 0, args[5] = 1;
  cdbOf(cdb, JUMP, args, sizeof args);
  for (int t = 0; t < 3; t++) {
    uint32_t got = 0;
    hal::storageCommand(cdb, 16, true, resp, 16, &got, 1000);
    for (uint32_t start = hal::millis(); hal::millis() - start < 3000; wait(50)) {
      if (!hal::storageSeen(ROM_VID, ROM_PID)) {
        hal::log("rom: jumped to %08X (try %d)", (unsigned)addr, t + 1);
        return 0;
      }
    }
  }
  return "Its boot ROM is still there: the jump didn't happen.";
}
