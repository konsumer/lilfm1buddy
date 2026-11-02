// lilfm1buddy: an ESP32-S3 on the FM-1's USB port. What it does depends on what's on that port when it starts:
//
// A computer: a USB drive ("LILFM1") with what it keeps on it:
//
//   *.fwsc          firmwares (FM-1 packages)
//   *.bin           backups of an FM-1's flash (made from the FM-1's menu)
//   wl82loader.bin  JieLi's loader, for writing the FM-1's flash (the web page gets it)
//   midimap.txt     MIDI routing on the FM-1 (fm1buddy's rules: midimap.h). A line "bluetooth" turns on Bluetooth
//                   MIDI too, "bluetooth NAME" connects to a Bluetooth MIDI device of that name as well
//   log.txt         what happened last time on the FM-1
//
// Ejected, it's the computer's serial port until it's unplugged (for installing this program, and its console: below,
// and tools/lil.py, to drive it from a script). Unplugged,
// it restarts (for the FM-1). Installing it any time: the chip's own download mode (hold BOOT, tap reset), which
// nothing here can break.
//
// The FM-1, straight or on a powered hub with other MIDI devices: their USB host. With a midimap.txt it routes MIDI
// between them (and Bluetooth). Press BOOT (it wakes it too) for the menu on the FM-1's screen:
//
//   1. the FM-1 into its boot ROM: SysEx to its firmware (stock V15, Felucca-based ones). If that doesn't work, the
//      board restarts into key mode (fm1buddy's key_s3.cpp), and the key does it as the FM-1 is switched on. The key
//      needs the FM-1 straight on the port: no hub
//   2. the picker (picker/, in this program) into its RAM, and a jump to it. Nothing is written to its flash yet:
//      switching the FM-1 off and on brings its firmware back
//   3. the picker asks for the list (picker.c says how), shows it, and says which one
//   4. a firmware or a backup: the FM-1 back into its boot ROM (the picker takes the SysEx too), JieLi's loader, and
//      the sectors from 0x4000 to the image's end that differ. Its head (what the chip starts from) and key_mac stay.
//      "Back up this FM-1": its whole flash to backup-N.bin
//
// Then it sleeps (and when nothing's happening for a while): BOOT or its reset button wakes it.
#include <Arduino.h>
#include <dirent.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/rtc_cntl_reg.h>
#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <vector>

#include "board.h"
#include "fm1.h"
#include "hal.h"
#include "lil.h"
#include "midi.h"
#include "rom.h"

#define LOADER FS "/wl82loader.bin"
#define MAP FS "/midimap.txt"
#define PICKER_VID 0x1209
#define PICKER_PID 0x0002
#define LIST_MAX 24   // picker.c's PK_MAX
#define PICK_NAME 20  // and PK_NAME
#define BACKUP_NAME "Back up this FM-1"
#define FLASH_SIZE 0x100000u
#define IDLE_MS 120000  // nothing to do for this long: sleep
#define VERSION "0.2.0"

// the picker (picker/build.sh: build/picker.bin, in this program)
extern const uint8_t pickerStart[] asm("_binary_build_picker_bin_start");
extern const uint8_t pickerEnd[] asm("_binary_build_picker_bin_end");

#define SERIAL_MODE 0x53455249
static RTC_NOINIT_ATTR uint32_t serialFlag;  // a restart into the serial port (the drive ejected)
static bool computer, serial, fsOk;

#define say(...) hal::log("lil: " __VA_ARGS__)

// the USB port back to the serial port, for a restart or a wake: they keep it as it is, and a computer on it then
// wouldn't find the board
static void phyToSerial() {
  CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL_M | RTC_CNTL_SW_USB_PHY_SEL_M);
}

static void fail(const char* e) {
  led(LED_FAILED);
  say("%s", e);
}

// deep sleep, until BOOT or the reset button (no power switch needed: tens of uA)
static void sleepNow(uint32_t ms = 10000) {
  delay(ms);
  say("Sleeping: BOOT or reset wakes it.");
  if (fsOk) {
    logSave();
  }
  ledEnd();
  phyToSerial();
  rtc_gpio_pullup_en((gpio_num_t)BOOT_PIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BOOT_PIN, 0);
  esp_deep_sleep_start();
}

// its own battery, in bars (0..3), or -1 if it can't tell (BAT_ADC: the pin that reads it, through BAT_DIV)
static int batteryBars() {
#ifdef BAT_ADC
  uint32_t v = 0;
  for (int i = 0; i < 8; i++) {
    v += analogReadMilliVolts(BAT_ADC);
  }
  v = v / 8 * BAT_DIV;
  say("Its battery: %u mV", (unsigned)v);
  return v >= 3900 ? 3 : v >= 3700 ? 2
                     : v >= 3500   ? 1
                                   : 0;
#else
  return -1;
#endif
}

static bool endsWith(const std::string& s, const char* e) {
  size_t n = strlen(e);
  return s.size() > n && !strcasecmp(s.c_str() + s.size() - n, e);
}

// the menu: backing up first, then the firmwares and backups on the drive, by name
static std::vector<std::string> menu() {
  std::vector<std::string> l;
  if (DIR* d = opendir(FS)) {
    while (dirent* e = readdir(d)) {
      std::string f = e->d_name;
      if (f[0] != '.' && f != "wl82loader.bin" && (endsWith(f, ".fwsc") || endsWith(f, ".bin"))) {
        l.push_back(f);
      }
    }
    closedir(d);
  }
  std::sort(l.begin(), l.end(), [](const std::string& a, const std::string& b) { return strcasecmp(a.c_str(), b.c_str()) < 0; });
  l.insert(l.begin(), BACKUP_NAME);
  l.resize(std::min<size_t>(l.size(), LIST_MAX));
  return l;
}

// its name for the FM-1's screen: no extension, 7-bit, PICK_NAME at most
static std::string shortName(const std::string& f) {
  std::string out;
  for (char c : f.substr(0, f.rfind('.'))) {
    if (out.size() < PICK_NAME) {
      out += c >= 0x20 && c < 0x7F ? c : '?';
    }
  }
  return out;
}

// a USB MIDI device within ms: a running FM-1, or the picker (vid:pid); its device, or -1
static int find(uint32_t ms, uint16_t vid = 0, uint16_t pid = 0) {
  for (uint32_t start = millis(); millis() - start < ms; delay(100)) {
    for (int d = 0; d < MIDI_USB; d++) {
      midi::Dev v;
      if (vid ? midi::dev(d, v) && v.on && v.vid == vid && v.pid == pid : fm1::candidate(d)) {
        return d;
      }
    }
  }
  return -1;
}

// the FM-1 into its boot ROM: 0 there, or an error (after it's restarted into key mode: never returns then)
static const char* toRom() {
  if (hal::recoveryMode()) {
    led(LED_SWITCH);
    say("Switch the FM-1 OFF, then ON");
    hal::keyStart();
    for (uint32_t start = millis(); hal::keyState() != hal::KEY_HOST; delay(50)) {
      if (millis() - start > 120000) {
        return "The FM-1 wasn't switched on.";
      }
    }
    return rom::attach(15000);
  }
  led(LED_LOOKING);
  say("Looking for the FM-1...");
  int d = find(15000);
  const char* e = "No FM-1 found.";
  if (d >= 0 && !(e = rom::enter(d))) {
    e = rom::attach(10000);
  }
  if (e) {  // (SLOOP, say: it doesn't take the SysEx) the key, from key mode
    say("%s Into key mode.", e);
    hal::recoveryModeSet(true);
  }
  return e;
}

// the picker's side: the list when it asks, until it says which one; that one, -1 (it went) or -2 (nothing was
// picked in 10 minutes)
static int listen(int d, const std::vector<std::string>& list) {
  static const uint8_t HELLO[] = {0xF0, 0x7D, 0x4C, 0x42, 0x10, 0xF7};
  for (uint32_t start = millis(); millis() - start < 600000;) {
    midi::Dev v;
    if (!midi::dev(d, v) || !v.on) {
      return -1;
    }
    uint8_t m[SYSEX_MAX];
    uint32_t n = midi::sysex(d, m);
    if (!n) {
      delay(20);
    } else if (n == sizeof HELLO && !memcmp(m, HELLO, n)) {
      for (size_t i = 0; i < list.size(); i++) {
        std::string name = shortName(list[i]);
        uint8_t out[8 + PICK_NAME] = {0xF0, 0x7D, 0x4C, 0x42, 0x01, (uint8_t)i, (uint8_t)list.size()};
        memcpy(out + 7, name.data(), name.size());
        out[7 + name.size()] = 0xF7;
        midi::sendWait(d, out, 8 + name.size(), 1000);
        delay(30);  // (it takes one SysEx at a time: one that comes before it took the last is lost)
      }
      int bars = batteryBars();
      if (bars >= 0) {  // (its battery, for the picker's header)
        uint8_t out[] = {0xF0, 0x7D, 0x4C, 0x42, 0x02, (uint8_t)bars, 0xF7};
        midi::sendWait(d, out, sizeof out, 1000);
      }
      midi::drain(d);  // (the hellos that came meanwhile)
    } else if (n == 7 && !memcmp(m, HELLO, 4) && m[4] == 0x12 && m[5] < list.size()) {
      return m[5];
    }
  }
  return -2;
}

// from the picker (device d) to JieLi's loader, on the FM-1's chip: 0, or an error
static const char* toLoader(int d, const std::vector<uint8_t>& loader, rom::Chip& chip) {
  const char* e;
  if ((e = rom::enter(d)) || (e = rom::attach(10000)) || (e = rom::loader(loader.data(), loader.size(), chip))) {
    return e;
  }
  if (chip.key != FM1_KEY || chip.flashId != FM1_FLASH || chip.size != FLASH_SIZE) {
    rom::leave();
    return "That isn't the FM-1's chip: nothing was done.";
  }
  return 0;
}

// the FM-1's whole flash to backup-N.bin: 0, or an error
static const char* backup(int d, const std::vector<uint8_t>& loader) {
  if (disk::freeBytes() < FLASH_SIZE + 65536) {
    return "No room on the drive for a backup.";
  }
  char path[40];
  struct stat st;
  for (int n = 1; snprintf(path, sizeof path, FS "/backup-%d.bin", n), !stat(path, &st); n++) {
  }
  led(LED_READING);
  rom::Chip chip;
  const char* e = toLoader(d, loader, chip);
  FILE* f = e ? 0 : fopen(path, "wb");
  if (!e && !f) {
    e = "Can't write to the drive.";
  }
  static uint8_t buf[SECTOR];
  for (uint32_t a = 0; !e && f && a < FLASH_SIZE; a += SECTOR) {
    if (!(e = rom::read(chip, a, buf, SECTOR)) && fwrite(buf, 1, SECTOR, f) != SECTOR) {
      e = "Can't write to the drive (full?).";
    }
  }
  rom::leave();  // (it restarts, into its firmware)
  if (f) {
    fclose(f);
    if (e) {
      remove(path);
    } else {
      say("Saved %s", path + sizeof FS);
    }
  }
  return e;
}

// a firmware (.fwsc) or a backup (.bin) onto the FM-1: 0, or an error
static const char* install(int d, const std::string& file, const std::vector<uint8_t>& loader) {
  static Pkg pkg;
  std::vector<uint8_t> raw;
  std::string path = FS "/" + file;
  const uint8_t* want;  // what the flash should have, from 0
  uint32_t end;         // up to here
  const char* e;
  if (endsWith(file, ".bin")) {
    FILE* f = fopen(path.c_str(), "rb");
    raw.resize(FLASH_SIZE + 1);
    size_t n = f ? fread(raw.data(), 1, raw.size(), f) : 0;
    if (f) {
      fclose(f);
    }
    if (n != FLASH_SIZE) {
      return "That isn't a backup of an FM-1 (1 MB).";
    }
    want = raw.data();
    end = WRITE_TO;
  } else {
    uint32_t at;
    if ((e = pkg.open(path.c_str()))) {
      return e;
    }
    if (!pkg.flash(at, end) || strncmp(pkg.product, "FM-1_", 5)) {
      return "That package has no FM-1 flash image in it.";
    }
    if (end <= WRITE_FROM || end > WRITE_TO) {
      return "Its flash image is a size the FM-1's isn't.";
    }
    want = pkg.image + at;
  }
  led(LED_WRITING);
  rom::Chip chip;
  if ((e = toLoader(d, loader, chip))) {
    return e;
  }
  // a sector at a time: read, and written only if it differs
  uint32_t total = (end - WRITE_FROM + SECTOR - 1) / SECTOR, written = 0;
  static uint8_t now[SECTOR], sec[SECTOR];
  for (uint32_t a = WRITE_FROM, k = 1; !e && a < end; a += SECTOR, k++) {
    uint32_t n = std::min<uint32_t>(SECTOR, end - a);
    if ((e = rom::read(chip, a, now, SECTOR))) {
      break;
    }
    if (memcmp(want + a, now, n)) {
      memcpy(sec, now, SECTOR);  // (the end of a short image: as it is)
      memcpy(sec, want + a, n);
      for (int t = 0; t < 3; t++) {
        if (!(e = rom::writeSector(a, sec))) {
          break;
        }
        say("sector %06X: %s", (unsigned)a, e);
      }
      written++;
    }
    if (k % 32 == 0 || k == total) {
      say("  %u of %u sectors, %u written", (unsigned)k, (unsigned)total, (unsigned)written);
    }
  }
  rom::leave();  // (it restarts: into what was written)
  pkg.close();
  if (e && written) {
    static std::string err;
    err = std::string(e) + " Press BOOT to try again before switching the FM-1 off.";
    return err.c_str();
  }
  return e;
}

// the menu on the FM-1, and what's picked there. Never returns: it sleeps after
static void picker() {
  std::vector<uint8_t> loader(LOADER_SIZE + 1);
  char sha[65];
  const char* e = 0;
  FILE* f = fsOk ? fopen(LOADER, "rb") : 0;
  loader.resize(f ? fread(loader.data(), 1, loader.size(), f) : 0);
  if (f) {
    fclose(f);
  }
  if (!fsOk) {
    e = "Its drive didn't mount.";
  } else if (!hal::sha256(loader.data(), loader.size(), sha) || strcmp(sha, LOADER_SHA256)) {
    e = "No wl82loader.bin on its drive (or not JieLi's).";
  }
  if (e || (e = toRom()) || (e = rom::run(PICKER_AT, pickerStart, pickerEnd - pickerStart))) {
    fail(e);
    sleepNow();
  }
  int d = find(10000, PICKER_VID, PICKER_PID);
  if (d < 0) {
    fail("The picker didn't show up on USB.");
    sleepNow();
  }
  std::vector<std::string> list = menu();
  led(LED_PICK);
  say("Pick one on the FM-1 (%u).", (unsigned)list.size());
  int i = listen(d, list);
  if (i < 0) {
    fail(i == -1 ? "The picker went (the FM-1 restarted?)." : "Nothing was picked: switch the FM-1 off and on.");
    sleepNow();
  }
  say("%s", list[i].c_str());
  if ((e = i == 0 ? backup(d, loader) : install(d, list[i], loader))) {
    fail(e);
  } else {
    led(LED_DONE);
    say("Done.");
  }
  sleepNow();
}

// MIDI routing, with a midimap.txt (and Bluetooth MIDI, if it says so): on or not
static bool routing() {
  FILE* f = fsOk ? fopen(MAP, "r") : 0;
  if (!f) {
    return false;
  }
  std::vector<std::string> names;
  bool ble = false;
  char line[160], name[40];
  while (fgets(line, sizeof line, f)) {
    if (!strncasecmp(line + strspn(line, " \t"), "bluetooth", 9)) {
      ble = true;
      if (sscanf(line, " %*s %39[^\r\n#]", name) == 1) {
        names.push_back(name);
      }
    }
  }
  fclose(f);
  if (ble) {
    hal::bleOn(true);
    hal::bleSearch(!names.empty());
  }
  midi::route(true);
  return true;
}

// Bluetooth MIDI devices midimap.txt names, connected (and remembered) once they're found
static void bleConnect() {
  static uint32_t at;
  if (!hal::bleIsOn() || millis() - at < 2000) {
    return;
  }
  at = millis();
  FILE* f = fopen(MAP, "r");
  char line[160], name[40];
  hal::BlePeer p[8];
  int n = hal::blePeers(p, 8);
  while (f && fgets(line, sizeof line, f)) {
    if (!strncasecmp(line + strspn(line, " \t"), "bluetooth", 9) && sscanf(line, " %*s %39[^\r\n#]", name) == 1) {
      for (int i = 0; i < n; i++) {
        if (!p[i].saved && strcasestr(p[i].name, name)) {
          hal::bleToggle(i);
        }
      }
    }
  }
  if (f) {
    fclose(f);
  }
}

// on the FM-1 (or a hub): routing until BOOT, then the menu
static void host(void*) {
  bool woke = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
  if (!hal::recoveryMode() && !woke) {
    bool routes = routing();
    led(routes ? LED_ROUTING : LED_READY);
    say("%sBOOT: the menu on the FM-1.", routes ? "Routing MIDI. " : "");
    uint32_t idle = millis(), count = 0;
    while (!bootPressed()) {
      delay(20);
      uint32_t c = 0;
      bool any = false;
      for (int d = 0; d < MIDI_TOUCH; d++) {
        midi::Dev v;
        if (midi::dev(d, v) && v.on) {
          c += v.count;
          any = true;
        }
      }
      if (c != count) {
        ledFlash(FLASH_MIDI);
        count = c;
      }
      if (routes && any) {
        idle = millis();
      }
      if (millis() - idle > IDLE_MS) {
        sleepNow(0);
      }
      if (routes) {
        bleConnect();
      }
    }
    midi::route(false);
  }
  picker();
}

void setup() {
  Serial.begin(115200);
  ledBegin();
  // a computer on the port (it sends start-of-frames): its USB drive. Else: the USB host
  for (uint32_t t = millis(); !hal::recoveryMode() && millis() - t < 1500 && !computer; delay(10)) {
    computer = HWCDC::isPlugged();
  }
  serial = computer && serialFlag == SERIAL_MODE && esp_reset_reason() == ESP_RST_SW;
  serialFlag = 0;
  if (serial) {
    led(LED_SERIAL);
    fsOk = disk::mount();  // (the computer hasn't it now)
    say("lilfm1buddy " VERSION ", on a computer: its serial port, until it's unplugged.");
    return;
  }
  if (computer) {
    led(LED_DISK);
    say("lilfm1buddy " VERSION ", on a computer: its USB drive.");
    disk::mount();  // (a new board's: formatted first, as the computer would ask to)
    disk::usbBegin();
    return;
  }
  fsOk = disk::mount();
  if (hal::recoveryMode() && fsOk) {
    logLoad();
  }
  say("lilfm1buddy " VERSION ", on the FM-1%s", hal::recoveryMode() ? " (key mode)" : "");
  // the serial port off first: in key mode, Serial.end() drives D+ and D- low, and the key is on them
  esp_register_shutdown_handler(phyToSerial);
  Serial.end();
  if (!hal::recoveryMode()) {
    board::usbHostBegin();  // (in key mode: once the key has worked, keyStart)
  }
  xTaskCreatePinnedToCore(host, "lil", 16384, 0, 1, 0, 1);
}

// the serial port's commands, a line each. Each answer ends with a line "OK" or "ERR why" (the log's lines may come
// between):
//
//   info   lilfm1buddy VERSION free=BYTES loader=0|1
//   ls     NAME BYTES, a line for each file on the drive
//   log    log.txt (what happened last time on the FM-1)
//   disk   OK, then it restarts as the USB drive
static void console() {
  static char line[80];
  static uint8_t n;
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (n < sizeof line - 1) {
        line[n++] = c;
      }
      continue;
    }
    line[n] = 0;
    n = 0;
    if (!line[0]) {
      continue;
    }
    if (!fsOk && strcmp(line, "disk")) {
      Serial.println("ERR Its drive didn't mount.");
    } else if (!strcmp(line, "info")) {
      char sha[65];
      bool loader = hal::sha256(LOADER, sha) && !strcmp(sha, LOADER_SHA256);
      Serial.printf("lilfm1buddy " VERSION " free=%llu loader=%d\nOK\n", disk::freeBytes(), loader);
    } else if (!strcmp(line, "ls")) {
      if (DIR* d = opendir(FS)) {
        while (dirent* e = readdir(d)) {
          struct stat st;
          std::string p = FS "/" + std::string(e->d_name);
          if (!stat(p.c_str(), &st) && S_ISREG(st.st_mode)) {
            Serial.printf("%s %u\n", e->d_name, (unsigned)st.st_size);
          }
        }
        closedir(d);
      }
      Serial.println("OK");
    } else if (!strcmp(line, "log")) {
      if (FILE* f = fopen(LOG_FILE, "r")) {
        char b[256];
        for (size_t k; (k = fread(b, 1, sizeof b, f));) {
          Serial.write((uint8_t*)b, k);
        }
        fclose(f);
      }
      Serial.println("OK");
    } else if (!strcmp(line, "disk")) {
      Serial.println("OK");
      Serial.flush();
      delay(100);
      esp_restart();  // (a computer on it: the drive)
    } else {
      Serial.printf("ERR What's \"%s\"? (info, ls, log, disk)\n", line);
    }
  }
}

void loop() {
  static uint32_t seen = millis();
  if (serial) {
    console();
    if (HWCDC::isPlugged()) {
      seen = millis();
    } else if (millis() - seen > 1000) {
      esp_restart();  // (unplugged: for the FM-1)
    }
  } else if (computer) {
    if (disk::loop()) {
      serialFlag = SERIAL_MODE;
      esp_restart();
    }
  } else {
    board::bleLoop();
  }
  delay(10);
}
