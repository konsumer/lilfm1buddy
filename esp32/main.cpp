// lilfm1buddy: an ESP32-S3 on the FM-1's USB port. What it does depends on what's on that port when it starts:
//
// A computer: a USB drive ("LILFM1") with what it keeps on it:
//
//   *.fwsc          firmwares (FM-1 packages)
//   *.bin           backups of an FM-1's flash
//   _lil            the menu on the FM-1's screen (picker/: run from its RAM), if it isn't the one built in
//   wl82loader.bin  JieLi's loader, for writing the FM-1's flash (the web page gets it)
//   midimap.txt     MIDI routing on the FM-1 (fm1buddy's rules: midimap.h). A line "bluetooth" turns on Bluetooth
//                   MIDI too, "bluetooth NAME" connects to a Bluetooth MIDI device of that name as well
//   log.txt         what happened last time on the FM-1
//
// Ejected, it's the computer's serial port until it's unplugged (for installing this program, and its commands:
// below, and tools/lil.py). Unplugged, it restarts (for the FM-1). Installing it any time: the chip's own download
// mode (hold BOOT, tap reset), which nothing here can break.
//
// The FM-1, straight or on a powered hub with other MIDI devices: their USB host. With a midimap.txt it routes MIDI
// between them (and Bluetooth). Press BOOT (it wakes it too) for the menu:
//
//   1. the FM-1 into its boot ROM: SysEx to its firmware (stock V15, Felucca-based ones). If that doesn't work, the
//      board restarts into key mode (fm1buddy's key_s3.cpp), and the key does it as the FM-1 is switched on. The key
//      needs the FM-1 straight on the port: no hub
//   2. the menu (_lil, or the one built in) into its RAM, and a jump to it. Nothing is written to its flash:
//      switching the FM-1 off and on brings its firmware back
//   3. the menu asks for what it needs with the commands below, and for what's picked: "install NAME" (the FM-1
//      back into its boot ROM, JieLi's loader, and the sectors from 0x4000 to the image's end that differ; its head,
//      what the chip starts from, and key_mac stay) or "backup" (its whole flash to fm1-N.bin)
//
// Then it sleeps (and when nothing's happening for a while): BOOT or its reset button wakes it.
//
// The commands, the same on the serial port (a line each) and from any USB MIDI device on the FM-1 side (a line in
// each SysEx: F0 7D 4C 42 TEXT F7, answered the same way): the menu's, and any FM-1 firmware's that wants them. Each
// answer ends with a line "OK" or "ERR why":
//
//   info               lilfm1buddy VERSION free=BYTES loader=0|1 battery=BARS (0..3, -1: it can't tell)
//   ls                 NAME BYTES, a line for each file on the drive (names up to 60 characters)
//   read OFF LEN NAME  LEN bytes (up to 64) of a file from OFF, in hex
//   rm NAME            the file gone
//   install NAME       (from the FM-1) that firmware or backup onto it; OK first, then it's done
//   backup             (from the FM-1) its whole flash to fm1-N.bin (smk-N.bin: an SMK-37's); OK first
//   run NAME           (from the FM-1) a program into its RAM (like _lil), and a jump to it; OK first
//   log                (serial) log.txt
//   disk               (serial) the USB drive again
#include <Arduino.h>
#include <dirent.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <soc/rtc_cntl_reg.h>
#include <stdarg.h>
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

#define MAP FS "/midimap.txt"
#define MENU "_lil"
#define FLASH_SIZE 0x100000u
// The SMK-37 (and its other names: fm1.h isSmk) is the FM-1's chip in a keyboard, the same to its boot ROM, but its
// flash has its own factory calibration (keys, pads, wheels) in the sector at SMK_CAL, "trimdat" at SMK_MARK: unique
// to each keyboard, never written (fm1buddy's uboot.h). Its firmwares and backups are its own: never an FM-1's
#define SMK_CAL 0xC1000u
#define SMK_MARK 0xC1224u
#define IDLE_MS 120000  // nothing to do for this long: sleep
#define MENU_MS 600000  // (with the menu up: nothing picked)
#define VERSION "0.3.0"

// the menu built in (picker/build.sh: build/picker.bin)
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

// a running FM-1 on USB within ms: its device, or -1
static int find(uint32_t ms) {
  for (uint32_t start = millis(); millis() - start < ms; delay(100)) {
    for (int d = 0; d < MIDI_USB; d++) {
      if (fm1::candidate(d)) {
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

// from a program (device d: the menu, or a firmware that takes the boot ROM's SysEx) to JieLi's loader, on the FM-1's chip: 0, or an error
static const char* toLoader(int d, const std::vector<uint8_t>& loader, rom::Chip& chip) {
  const char* e;
  if ((e = rom::enter(d)) || (e = rom::attach(10000)) || (e = rom::loader(loader.data(), loader.size(), chip))) {
    return e;
  }
  if (chip.key != FM1_KEY || chip.flashId != FM1_FLASH || chip.size != FLASH_SIZE) {
    rom::leave();
    return "That isn't the FM-1's (or SMK-37's) chip: nothing was done.";
  }
  return 0;
}

// the whole flash to fm1-N.bin, or smk-N.bin for an SMK-37 (its calibration's mark in it), as fm1buddy names them:
// 0, or an error
static const char* backup(int d, const std::vector<uint8_t>& loader) {
  if (disk::freeBytes() < FLASH_SIZE + 65536) {
    return "No room on the drive for a backup.";
  }
  const char* part = FS "/backup.part";
  bool smk = false;
  led(LED_READING);
  rom::Chip chip;
  const char* e = toLoader(d, loader, chip);
  FILE* f = e ? 0 : fopen(part, "wb");
  if (!e && !f) {
    e = "Can't write to the drive.";
  }
  static uint8_t buf[SECTOR];
  for (uint32_t a = 0; !e && f && a < FLASH_SIZE; a += SECTOR) {
    if (!(e = rom::read(chip, a, buf, SECTOR)) && fwrite(buf, 1, SECTOR, f) != SECTOR) {
      e = "Can't write to the drive (full?).";
    }
    smk |= a == SMK_MARK / SECTOR * SECTOR && !memcmp(buf + SMK_MARK % SECTOR, "trimdat", 7);
  }
  rom::leave();  // (it restarts, into its firmware)
  if (f) {
    fclose(f);
  }
  char path[40];
  struct stat st;
  for (int n = 1; !e && (snprintf(path, sizeof path, FS "/%s-%d.bin", smk ? "smk" : "fm1", n), !stat(path, &st)); n++) {
  }
  if (e || rename(part, path)) {
    remove(part);
    return e ? e : "Can't write to the drive.";
  }
  say("Saved %s", path + sizeof FS);
  return 0;
}

// a firmware (.fwsc) or a backup (.bin) onto the FM-1 or SMK-37, if it's one of its own: 0, or an error
static bool smkMark(const uint8_t* calSector) {
  return !memcmp(calSector + SMK_MARK - SMK_CAL, "trimdat", 7);
}

static const char* install(int d, const std::string& file, const std::vector<uint8_t>& loader) {
  static Pkg pkg;
  std::vector<uint8_t> raw;
  std::string path = FS "/" + file;
  const uint8_t* want;  // what the flash should have, from 0
  uint32_t end;         // up to here
  bool forSmk;          // it's an SMK-37's
  const char* e;
  if (endsWith(file, ".bin")) {
    FILE* f = fopen(path.c_str(), "rb");
    raw.resize(FLASH_SIZE + 1);
    size_t n = f ? fread(raw.data(), 1, raw.size(), f) : 0;
    if (f) {
      fclose(f);
    }
    if (n != FLASH_SIZE) {
      return "That isn't a backup of an FM-1 or SMK-37 (1 MB).";
    }
    want = raw.data();
    end = WRITE_TO;
    forSmk = smkMark(want + SMK_CAL);
  } else {
    uint32_t at;
    if ((e = pkg.open(path.c_str()))) {
      return e;
    }
    forSmk = isSmk(pkg.product);
    if (!pkg.flash(at, end) || (strncmp(pkg.product, "FM-1_", 5) && !forSmk)) {
      return "That package has no FM-1 or SMK-37 flash image in it.";
    }
    if (end <= WRITE_FROM || end > (forSmk ? SMK_CAL : WRITE_TO)) {
      return "Its flash image is a size it can't be.";
    }
    want = pkg.image + at;
  }
  led(LED_WRITING);
  rom::Chip chip;
  if ((e = toLoader(d, loader, chip))) {
    return e;
  }
  // which it is (its calibration, or none), before anything's written: its own firmwares and backups only
  static uint8_t cal[SECTOR];
  if ((e = rom::read(chip, SMK_CAL, cal, SECTOR))) {
    rom::leave();
    return e;
  }
  if (smkMark(cal) != forSmk) {
    rom::leave();
    return smkMark(cal) ? "That's an FM-1's, and this is an SMK-37: nothing was written."
                        : "That's an SMK-37's, and this is an FM-1: nothing was written.";
  }
  // a sector at a time: read, and written only if it differs
  uint32_t total = (end - WRITE_FROM + SECTOR - 1) / SECTOR, written = 0;
  static uint8_t now[SECTOR], sec[SECTOR];
  for (uint32_t a = WRITE_FROM, k = 1; !e && a < end; a += SECTOR, k++) {
    uint32_t n = std::min<uint32_t>(SECTOR, end - a);
    if (forSmk && a == SMK_CAL) {
      continue;  // (its calibration: its own, even restoring a backup)
    }
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

// a file on the drive: false if there's none, or it's more than max
static bool load(const char* name, std::vector<uint8_t>& out, uint32_t max) {
  FILE* f = fsOk ? fopen((FS "/" + std::string(name)).c_str(), "rb") : 0;
  out.resize(max + 1);
  out.resize(f ? fread(out.data(), 1, out.size(), f) : 0);
  if (f) {
    fclose(f);
  }
  return !out.empty() && out.size() <= max;
}

static const char* jieliLoader(std::vector<uint8_t>& out) {
  char sha[65];
  return load("wl82loader.bin", out, LOADER_SIZE) && hal::sha256(out.data(), out.size(), sha) && !strcmp(sha, LOADER_SHA256)
             ? 0
             : "No wl82loader.bin on its drive (or not JieLi's).";
}

// the boot ROM there (after toRom, or enter): a program into the FM-1's RAM, and a jump to it. 0, or an error
static const char* runProgram(const char* name) {
  std::vector<uint8_t> p;
  if (load(name, p, PICKER_MAX)) {
    say("%s, from its drive", name);
    return rom::run(PICKER_AT, p.data(), p.size());
  }
  if (strcmp(name, MENU)) {
    return "No such program on its drive.";
  }
  return rom::run(PICKER_AT, pickerStart, pickerEnd - pickerStart);  // (the menu built in)
}

// ---- the commands ----
static int replyTo = -1;  // where answers go: the serial port (-1), or a USB MIDI device

static void reply(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void reply(const char* fmt, ...) {
  char s[160];
  va_list a;
  va_start(a, fmt);
  vsnprintf(s, sizeof s, fmt, a);
  va_end(a);
  if (replyTo < 0) {
    Serial.println(s);
    return;
  }
  uint8_t m[sizeof s + 5] = {0xF0, 0x7D, 0x4C, 0x42};
  uint32_t n = 4;
  for (const char* c = s; *c; c++) {
    m[n++] = *c & 0x80 ? '?' : *c;  // (SysEx: 7-bit)
  }
  m[n++] = 0xF7;
  midi::sendWait(replyTo, m, n, 1000);
  delay(30);  // (the menu takes one SysEx at a time: one that comes before it took the last is lost)
}

// what a command left to do once it's answered (install, backup, run, disk), and for which device
enum Act { ACT_NONE,
           ACT_INSTALL,
           ACT_BACKUP,
           ACT_RUN,
           ACT_DISK };
static Act act;
static std::string actFile;
static int actDev;

static bool fileName(const char* n) {
  return n[0] && n[0] != '.' && !strchr(n, '/') && strlen(n) <= 60;
}

static bool exists(const char* n) {
  struct stat st;
  return fileName(n) && !stat((FS "/" + std::string(n)).c_str(), &st) && S_ISREG(st.st_mode);
}

// a command from the serial port (dev -1) or USB MIDI device dev; answered there
static void command(const char* line, int dev) {
  char cmd[16] = "", arg[80] = "";
  unsigned off, len;
  sscanf(line, "%15s %79[^\r\n]", cmd, arg);
  replyTo = dev;
  bool fm1 = dev >= 0;
  say("< %s", line);
  if (!fsOk && strcmp(cmd, "disk")) {
    reply("ERR Its drive didn't mount.");
    return;
  }
  if (!strcmp(cmd, "info")) {
    std::vector<uint8_t> l;
    reply("lilfm1buddy " VERSION " free=%llu loader=%d battery=%d", disk::freeBytes(), !jieliLoader(l), batteryBars());
  } else if (!strcmp(cmd, "ls")) {
    if (DIR* d = opendir(FS)) {
      while (dirent* e = readdir(d)) {
        struct stat st;
        if (fileName(e->d_name) && !stat((FS "/" + std::string(e->d_name)).c_str(), &st) && S_ISREG(st.st_mode)) {
          reply("%s %u", e->d_name, (unsigned)st.st_size);
        }
      }
      closedir(d);
    }
  } else if (!strcmp(cmd, "read") && sscanf(arg, "%u %u %79[^\r\n]", &off, &len, arg) == 3 && len <= 64) {
    FILE* f = exists(arg) ? fopen((FS "/" + std::string(arg)).c_str(), "rb") : 0;
    uint8_t b[64];
    char hex[129] = "";
    size_t n = f && !fseek(f, off, SEEK_SET) ? fread(b, 1, len, f) : 0;
    if (f) {
      fclose(f);
    }
    if (!f) {
      reply("ERR No such file.");
      return;
    }
    for (size_t i = 0; i < n; i++) {
      sprintf(hex + i * 2, "%02x", b[i]);
    }
    reply("%s", hex);
  } else if (!strcmp(cmd, "rm")) {
    if (!exists(arg) || remove((FS "/" + std::string(arg)).c_str())) {
      reply("ERR No such file.");
      return;
    }
  } else if (fm1 && (!strcmp(cmd, "install") || !strcmp(cmd, "run"))) {
    if (!exists(arg) && strcmp(arg, MENU)) {
      reply("ERR No such file.");
      return;
    }
    act = cmd[0] == 'i' ? ACT_INSTALL : ACT_RUN, actFile = arg, actDev = dev;
  } else if (fm1 && !strcmp(cmd, "backup")) {
    act = ACT_BACKUP, actDev = dev;
  } else if (!fm1 && !strcmp(cmd, "log")) {
    if (FILE* f = fopen(LOG_FILE, "r")) {
      char b[256];
      for (size_t k; (k = fread(b, 1, sizeof b, f));) {
        Serial.write((uint8_t*)b, k);
      }
      fclose(f);
    }
  } else if (!fm1 && !strcmp(cmd, "disk")) {
    act = ACT_DISK;
  } else {
    reply("ERR What's \"%s\"?", cmd);
    return;
  }
  reply("OK");
}

// ---- on the FM-1 ----
// the menu on it: 0, or an error
static const char* startMenu() {
  midi::route(false);
  const char* e = toRom();
  if (!e && !(e = runProgram(MENU))) {
    led(LED_PICK);
    say("The menu's on the FM-1.");
  }
  return e;
}

// what a command left to do: install, backup (then it sleeps), or run
static void doAction() {
  Act a = act;
  act = ACT_NONE;
  std::vector<uint8_t> loader;
  const char* e = 0;
  if (a == ACT_RUN) {
    if ((e = rom::enter(actDev)) || (e = rom::attach(10000)) || (e = runProgram(actFile.c_str()))) {
      fail(e);
      sleepNow();
    }
    return;
  }
  say("%s %s", a == ACT_INSTALL ? "Installing" : "Backing up", actFile.c_str());
  if (!(e = jieliLoader(loader))) {
    e = a == ACT_INSTALL ? install(actDev, actFile, loader) : backup(actDev, loader);
  }
  if (e) {
    fail(e);
  } else {
    led(LED_DONE);
    say("Done.");
  }
  sleepNow();
}

// commands from the USB MIDI devices: true if any came
static bool commands() {
  bool any = false;
  for (int d = 0; d < MIDI_USB; d++) {
    uint8_t m[SYSEX_MAX + 1];
    for (uint32_t n; (n = midi::sysex(d, m));) {
      if (n > 5 && m[1] == 0x7D && m[2] == 0x4C && m[3] == 0x42 && m[4] >= 0x20) {
        m[n - 1] = 0;  // (the F7)
        command((char*)m + 4, d);
        any = true;
      }
    }
  }
  return any;
}

// on the FM-1 (or a hub): routing, and commands, until BOOT (or a wake by it, or key mode): the menu
static void host(void*) {
  bool menu = hal::recoveryMode() || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
  bool routes = !menu && routing();
  const char* e;
  if (menu && (e = startMenu())) {
    fail(e);
    sleepNow();
  }
  if (!menu) {
    led(routes ? LED_ROUTING : LED_READY);
    say("%sBOOT: the menu on the FM-1.", routes ? "Routing MIDI. " : "");
  }
  uint32_t idle = millis(), count = 0;
  for (;;) {
    delay(10);
    if (!menu && bootPressed()) {
      if ((e = startMenu())) {
        fail(e);
        sleepNow();
      }
      menu = true, routes = false, idle = millis();
    }
    if (commands()) {
      idle = millis();
    }
    if (act) {
      doAction();
      idle = millis();
    }
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
      bleConnect();
    }
    if (millis() - idle > (menu ? MENU_MS : IDLE_MS)) {
      say("%s", menu ? "Nothing was picked." : "Nothing to do.");
      sleepNow(0);
    }
  }
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

// ---- on a computer, ejected: the commands on the serial port ----
static void console() {
  static char line[120];
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
    if (line[0]) {
      command(line, -1);
    }
  }
  if (act == ACT_DISK) {
    Serial.flush();
    delay(100);
    esp_restart();  // (a computer on it: the drive)
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
