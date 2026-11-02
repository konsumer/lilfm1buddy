// What lilfm1buddy's own files share: the drive, the LED, the BOOT button, the log
#pragma once

#include <stdint.h>

#define FS "/fs"  // the drive (disk.cpp), for fopen and opendir
#define LOG_FILE FS "/log.txt"
#ifndef BOOT_PIN
#define BOOT_PIN 0  // every ESP32-S3 board's BOOT button (the board's pins_arduino.h has it too)
#endif

// what the LED shows (led.cpp has how)
enum Led { LED_OFF,
           LED_DISK,     // on a computer: its USB drive (flashes as it's read or written)
           LED_SERIAL,   // on a computer: its serial port (the drive ejected)
           LED_READY,    // on the FM-1, waiting for BOOT
           LED_ROUTING,  // the same, and routing MIDI (flashes as it goes through)
           LED_LOOKING,  // getting the FM-1 into its boot ROM
           LED_SWITCH,   // switch the FM-1 off, then on
           LED_PICK,     // pick one on the FM-1
           LED_READING,  // backing the FM-1 up
           LED_WRITING,  // writing the FM-1
           LED_DONE,
           LED_FAILED };
enum Flash { FLASH_READ,
             FLASH_WRITE,
             FLASH_MIDI };
void led(Led l);
void ledFlash(Flash f);
void ledBegin();
void ledEnd();  // off, and kept off through deep sleep

bool bootPressed();  // BOOT went down since the last call

void logSave();  // this run's log to log.txt
void logLoad();  // the last run's log carried on (a restart into key mode)

namespace disk {
bool mount();  // the drive at FS, formatted if it's new: false if it can't be
uint64_t freeBytes();
void usbBegin();  // the drive as a USB drive for the computer
// writes what's waiting, once nothing more has come for a moment; restarts once the computer's gone. True once
// it's ejected
bool loop();
}  // namespace disk
