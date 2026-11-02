// The drive: the flash past the program (the "fat" partition), a FAT volume of 512-byte sectors on it as it is (no
// wear levelling: it's written now and then, not all the time, and none of the flash goes to it). The board reads
// and writes it at FS; in disk mode a computer does, as a USB drive, and the board leaves it alone.
//
// The flash erases 4 KB at a time: one 4 KB block is kept here, changed, and written back when another one is wanted,
// or nothing more has come for a moment (disk::loop), or the file is closed (CTRL_SYNC)
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <diskio_impl.h>
#include <esp_partition.h>
#include <esp_vfs_fat.h>
#include <ff.h>
#include <soc/rtc_cntl_reg.h>

#include <mutex>

#include "hal.h"
#include "lil.h"

#define SS 512
#define BLOCK 4096
#define LABEL "LILFM1"

static const esp_partition_t* part;
static std::mutex lock;
static uint8_t cache[BLOCK];
static int32_t cached = -1;
static bool dirty;
static uint32_t lastWrite;

static bool flushLocked() {
  if (!dirty) {
    return true;
  }
  dirty = false;
  return !esp_partition_erase_range(part, cached * BLOCK, BLOCK) && !esp_partition_write(part, cached * BLOCK, cache, BLOCK);
}

static bool flush() {
  std::lock_guard<std::mutex> g(lock);
  return flushLocked();
}

static bool readSectors(uint8_t* out, uint32_t s, uint32_t n) {
  std::lock_guard<std::mutex> g(lock);
  for (; n--; s++, out += SS) {
    if ((int32_t)(s * SS / BLOCK) == cached) {
      memcpy(out, cache + s * SS % BLOCK, SS);
    } else if (esp_partition_read(part, s * SS, out, SS)) {
      return false;
    }
  }
  return true;
}

static bool writeSectors(const uint8_t* in, uint32_t s, uint32_t n) {
  std::lock_guard<std::mutex> g(lock);
  for (; n--; s++, in += SS) {
    int32_t b = s * SS / BLOCK;
    if (b != cached) {
      if (!flushLocked()) {
        return false;
      }
      cached = -1;
      if (esp_partition_read(part, b * BLOCK, cache, BLOCK)) {
        return false;
      }
      cached = b;
    }
    uint8_t* at = cache + s * SS % BLOCK;
    if (memcmp(at, in, SS)) {  // (what's already there isn't written again)
      memcpy(at, in, SS);
      dirty = true;
    }
  }
  lastWrite = millis();
  return true;
}

static bool open() {
  part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, 0);
  return part;
}

// ---- for the board: FatFs on it ----
static DSTATUS dOk(BYTE) {
  return 0;
}

static DRESULT dRead(BYTE, BYTE* b, uint32_t s, UINT n) {
  return readSectors(b, s, n) ? RES_OK : RES_ERROR;
}

static DRESULT dWrite(BYTE, const BYTE* b, uint32_t s, UINT n) {
  return writeSectors(b, s, n) ? RES_OK : RES_ERROR;
}

static DRESULT dIoctl(BYTE, BYTE cmd, void* buf) {
  switch (cmd) {
    case CTRL_SYNC:
      return flush() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
      *(LBA_t*)buf = part->size / SS;
      return RES_OK;
    case GET_SECTOR_SIZE:
      *(WORD*)buf = SS;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *(DWORD*)buf = BLOCK / SS;
      return RES_OK;
  }
  return RES_PARERR;
}

static char drive[3];
static FATFS* fs;

bool disk::mount() {
  static const ff_diskio_impl_t impl = {dOk, dOk, dRead, dWrite, dIoctl};
  BYTE pdrv;
  if (!open() || ff_diskio_get_drive(&pdrv)) {
    return false;
  }
  ff_diskio_register(pdrv, &impl);
  drive[0] = '0' + pdrv, drive[1] = ':';
  esp_vfs_fat_conf_t c = {FS, drive, 4};
  if (esp_vfs_fat_register_cfg(&c, &fs)) {
    return false;
  }
  FRESULT r = f_mount(fs, drive, 1);
  if (r == FR_NO_FILESYSTEM) {  // a new board: formatted, once
    hal::log("disk: formatting %u KB", (unsigned)(part->size / 1024));
    MKFS_PARM opt = {FM_ANY | FM_SFD, 1, 0, 0, 0};  // (one FAT: more room)
    static uint8_t work[BLOCK];
    r = f_mkfs(drive, &opt, work, sizeof work);
    if (!r && !(r = f_mount(fs, drive, 1))) {
      f_setlabel((std::string(drive) + LABEL).c_str());
      fclose(fopen(FS "/.metadata_never_index", "w"));  // (macOS: no Spotlight index on it)
    }
  }
  hal::log("disk: %s", r ? "can't mount it" : "mounted");
  return !r;
}

uint64_t disk::freeBytes() {
  DWORD clusters;
  FATFS* f;
  return f_getfree(drive, &clusters, &f) ? 0 : (uint64_t)clusters * f->csize * SS;
}

// ---- for a computer: a USB drive ----
static USBMSC msc;
static volatile bool ejected;

void disk::loop() {
  if (dirty && millis() - lastWrite > 500) {
    flush();
  }
  if (ejected) {
    delay(200);  // (its answer goes out first)
    esp_restart();
  }
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buf, uint32_t n) {
  ledFlash(FLASH_READ);
  return offset % SS || n % SS || !readSectors((uint8_t*)buf, lba + offset / SS, n / SS) ? -1 : n;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t n) {
  ledFlash(FLASH_WRITE);
  return offset % SS || n % SS || !writeSectors(buf, lba + offset / SS, n / SS) ? -1 : n;
}

// the computer ejected it: done with it, back to the serial port
static bool onStartStop(uint8_t, bool start, bool eject) {
  if (eject && !start) {
    hal::log("disk: ejected");
    ejected = true;
  }
  return true;
}

// a restart (esp_restart) keeps the USB port as it is: the serial port again, or a computer on it wouldn't find it
static void phyToSerial() {
  flush();
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL_M | RTC_CNTL_SW_USB_PHY_SEL_M);
}

void disk::usbBegin() {
  if (!open()) {
    return;
  }
  esp_register_shutdown_handler(phyToSerial);
  Serial.end();
  msc.vendorID("lilfm1");
  msc.productID("lilfm1buddy");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);
  msc.begin(part->size / SS, SS);
  USB.productName("lilfm1buddy");
  USB.begin();
}
