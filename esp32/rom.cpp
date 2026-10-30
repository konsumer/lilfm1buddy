#include "rom.h"

#include <string.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include "hal.h"
#include "midi.h"

#define BLOCK 512  // what WRITE_MEMORY takes at once, and what the ROM deciphers by
#define IO 512
// the ROM's
#define WRITE_MEMORY 0xFB06
#define JUMP 0xFB08
// JieLi's loader's
#define READ_FLASH 0xFD05
#define WRITE_FLASH 0xFB04
#define ERASE_SECTOR 0xFB01
#define READ_KEY 0xFC09
#define GET_ONLINE_DEVICE 0xFC0A
#define RUN_APP 0xFC0C
#define GET_USB_BUFFER 0xFC14

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

// one that answers 16 bytes, its command echoed first; its answer in resp. Up to tries times
static bool exec(uint16_t cmd, const uint8_t* args, int n, uint8_t resp[16], int tries = 3, uint32_t ms = 2000) {
  uint8_t cdb[16];
  cdbOf(cdb, cmd, args, n);
  for (int t = 0; t < tries; t++) {
    uint32_t got = 0;
    if (hal::storageCommand(cdb, 16, true, resp, 16, &got, ms) && got == 16 && resp[0] == (cmd >> 8) &&
        resp[1] == (cmd & 0xFF)) {
      return true;
    }
  }
  return false;
}

// image into RAM at addr, a block at a time: ciphered here (plain), or as it is (already)
static bool writeMemory(uint32_t addr, const uint8_t* image, uint32_t n, bool plain) {
  std::vector<uint8_t> b(BLOCK);
  for (uint32_t off = 0; off < n; off += BLOCK) {
    uint32_t len = std::min<uint32_t>(BLOCK, n - off);
    memcpy(b.data(), image + off, len);
    if (plain) {
      cipher(b.data(), len);
    }
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
      return false;
    }
  }
  hal::log("rom: %u bytes at %08X", (unsigned)n, (unsigned)addr);
  return true;
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
  if (!writeMemory(addr, image, n, true)) {
    return "Its boot ROM didn't take the picker.";
  }
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

const char* rom::loader(const uint8_t* image, uint32_t n, Chip& chip) {
  if (!writeMemory(LOADER_AT, image, n, false)) {
    return "Its boot ROM didn't take JieLi's loader.";
  }
  // run it; it's running when it answers READ_KEY (the jump's own answer may be lost, and the loader doesn't know
  // JUMP: so it's asked only until the loader answers)
  uint8_t args[6], key[4], resp[16];
  be32(args, LOADER_AT);
  args[4] = 0, args[5] = 1;
  be32(key, 0x00AC6900);
  bool running = false;
  for (int t = 0; t < 3 && !running; t++) {
    exec(JUMP, args, 6, resp, 1);
    running = exec(READ_KEY, key, 4, resp);
  }
  if (!running) {
    return "JieLi's loader didn't start.";
  }
  uint8_t k[2] = {resp[7], resp[6]};  // its answer's 4th and 5th, the other way round, deciphered
  cipher(k, 2);
  chip.key = k[0] | k[1] << 8;
  if (!exec(GET_ONLINE_DEVICE, 0, 0, resp)) {
    leave();
    return "JieLi's loader didn't say what flash it has.";
  }
  chip.flashId = resp[4] | resp[5] << 8 | resp[6] << 16 | (uint32_t)resp[7] << 24;
  uint8_t cap = chip.flashId & 0xFF;  // JEDEC: its size, as a power of 2
  chip.size = cap >= 0x10 && cap <= 0x18 ? 1u << cap : 1024 * 1024;
  // reads as big as it takes (what it says isn't taken on trust: one that big first, smaller if it doesn't come back)
  chip.io = IO;
  if (exec(GET_USB_BUFFER, 0, 0, resp, 2, 1000)) {
    uint32_t b = (uint32_t)resp[2] << 24 | resp[3] << 16 | resp[4] << 8 | resp[5];
    for (uint32_t io = std::min<uint32_t>(b, STORAGE_IO_MAX) / IO * IO; io > IO; io /= 2) {
      std::vector<uint8_t> test(io);
      uint8_t a[6], cdb[16];
      be32(a, 0);
      a[4] = io >> 8, a[5] = io & 0xFF;
      cdbOf(cdb, READ_FLASH, a, 6);
      uint32_t got = 0;
      if (hal::storageCommand(cdb, 16, true, test.data(), io, &got, 2000) && got == io) {
        chip.io = io;
        break;
      }
    }
  }
  hal::log("rom: chip key %04X, flash %06X (%u KiB), reads of %u", chip.key, (unsigned)chip.flashId,
           (unsigned)chip.size / 1024, (unsigned)chip.io);
  return 0;
}

const char* rom::read(const Chip& chip, uint32_t addr, uint8_t* out, uint32_t n) {
  uint32_t io = chip.io >= IO ? chip.io : IO;
  for (uint32_t at = 0; at < n; at += io) {
    uint32_t k = std::min(io, n - at);
    uint8_t args[6], cdb[16];
    be32(args, addr + at);
    args[4] = k >> 8, args[5] = k & 0xFF;
    cdbOf(cdb, READ_FLASH, args, 6);
    bool ok = false;
    for (int t = 0; t < 3 && !ok; t++) {
      uint32_t got = 0;
      ok = hal::storageCommand(cdb, 16, true, out + at, k, &got, 2000) && got == k;
    }
    if (!ok) {
      return "It stopped answering.";
    }
  }
  return 0;
}

const char* rom::writeSector(uint32_t addr, const uint8_t* data) {
  if (addr < WRITE_FROM || addr >= WRITE_TO || addr % SECTOR) {
    return "That isn't somewhere it writes.";
  }
  uint8_t args[9], cdb[16], resp[16], back[IO];
  be32(args, addr);
  if (!exec(ERASE_SECTOR, args, 4, resp, 3, 5000)) {
    return "It didn't erase.";
  }
  for (uint32_t off = 0; off < SECTOR; off += IO) {
    be32(args, addr + off);
    args[4] = IO >> 8, args[5] = IO & 0xFF, args[6] = 0;
    uint16_t c = crc16(data + off, IO, 0);
    args[7] = c & 0xFF, args[8] = c >> 8;
    cdbOf(cdb, WRITE_FLASH, args, sizeof args);
    bool ok = false;
    for (int t = 0; t < 3 && !ok; t++) {  // (the same bytes again: NOR flash only clears bits, so it's the same)
      ok = hal::storageCommand(cdb, 16, false, (uint8_t*)data + off, IO, 0, 2000);
    }
    if (!ok) {
      return "It didn't take what was written.";
    }
  }
  for (uint32_t off = 0; off < SECTOR; off += IO) {
    be32(args, addr + off);
    args[4] = IO >> 8, args[5] = IO & 0xFF;
    cdbOf(cdb, READ_FLASH, args, 6);
    bool ok = false;
    for (int t = 0; t < 3 && !ok; t++) {
      uint32_t got = 0;
      ok = hal::storageCommand(cdb, 16, true, back, IO, &got, 2000) && got == IO;
    }
    if (!ok) {
      return "It stopped answering.";
    }
    if (memcmp(back, data + off, IO)) {
      hal::log("rom: %06X reads back differently", (unsigned)(addr + off));
      return "It read back differently.";
    }
  }
  return 0;
}

void rom::leave() {
  if (!hal::storageThere(ROM_VID, ROM_PID)) {
    return;
  }
  uint8_t args[4], resp[16];
  be32(args, 1);
  exec(RUN_APP, args, 4, resp, 2, 500);  // (it may restart before it answers)
  hal::log("rom: back to its firmware");
}
