// lilfm1buddy: an ESP32-S3 on the FM-1's USB port. Pick a firmware on the FM-1 itself, and it's written to it.
//
//   1. the FM-1 into its boot ROM: SysEx to its firmware (stock V15, Felucca-based ones). If that doesn't work, the
//      board restarts into fm1buddy's recovery mode, and the key does it as the FM-1 is switched on (the LED yellow)
//   2. the picker (picker/, in this program) into its RAM, and a jump to it. Nothing is written to its flash yet:
//      switching the FM-1 off and on brings its firmware back
//   3. the picker asks for the list (picker.c says how), shows it, and says which one (the LED cyan meanwhile)
//   4. that one is written to it (the LED magenta): the FM-1 back into its boot ROM (the picker takes the SysEx too),
//      JieLi's loader (wl82loader.bin), and the sectors of its flash image (from 0x4000 to its end) that differ, as
//      fm1buddy's recovery writes them. Its head (what the chip starts from) and the flash past the image stay.
//      Then the LED is green (done) or red (it didn't)
//
// The files are in the board's own flash (LittleFS): wl82loader.bin, the firmwares (fm1/*.fwsc) and log.txt. On a
// computer the board is a serial port for the web page (site/), a command a line:
//
//   info                 INFO lilfm1buddy VERSION loader=0|1 files=N free=BYTES total=BYTES
//   ls                   FILE NAME BYTES (a line each firmware), then END
//   rm NAME              OK, or ERR why
//   put NAME BYTES       READY; then the bytes, PIECE at a time, each answered ACK BYTES-SO-FAR; then OK SHA256, or
//                        ERR why. NAME: wl82loader.bin (checked against its SHA-256), or X.fwsc (an FM-1 package)
//   log                  what happened last time, then END
//
// With SCREEN (the cyd build), what happens is on the board's screen too.
#include <Arduino.h>
#include <LittleFS.h>
#ifdef SCREEN
#include <TFT_eSPI.h>
#endif
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <vector>

#include "board.h"
#include "fm1.h"
#include "hal.h"
#include "midi.h"
#include "rom.h"

#define FS "/fs"  // the board's flash (LittleFS)
#define LOADER FS "/wl82loader.bin"
#define FW_DIR FS "/fm1"
#define LOG FS "/log.txt"
#define PICKER_VID 0x1209
#define PICKER_PID 0x0002
#define LIST_MAX 24  // picker.c's PK_MAX
#define NAME_MAX 20  // and PK_NAME
#define LINE_H 16
#define VERSION "0.1.0"
#define PIECE 4096  // put's: what's sent before an ACK

#ifndef SCREEN
#define TFT_WHITE 0
#define TFT_RED 1
#define TFT_GREEN 2
#define TFT_CYAN 3
#define TFT_YELLOW 4
#endif

// the picker (picker/build.sh: build/picker.bin, in this program)
extern const uint8_t pickerStart[] asm("_binary_build_picker_bin_start");
extern const uint8_t pickerEnd[] asm("_binary_build_picker_bin_end");

struct Firmware {
  std::string file, name;  // "fomajam-0.1.7.fwsc", "fomajam-0.1.7"
};

#ifdef SCREEN
static TFT_eSPI tft;
static int line;
#endif
static bool fsOk;

// the LED: what it's doing
static void led(uint8_t r, uint8_t g, uint8_t b) {
#ifdef LED_RGB
  rgbLedWrite(LED_RGB, r, g, b);
#endif
}

// a line on the screen, and in the log
static void say(uint16_t color, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(uint16_t color, const char* fmt, ...) {
  char s[120];
  va_list a;
  va_start(a, fmt);
  vsnprintf(s, sizeof s, fmt, a);
  va_end(a);
  hal::log("lil: %s", s);
  if (fsOk && !hal::onComputer()) {
    if (FILE* f = fopen(LOG, "a")) {
      fprintf(f, "%7.1f %s\n", millis() / 1000.0, s);
      fclose(f);
    }
  }
#ifdef SCREEN
  if (line + LINE_H > tft.height()) {
    tft.fillScreen(TFT_BLACK);
    line = 0;
  }
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(s, 4, line, 2);
  line += LINE_H;
#else
  (void)color;
#endif
}

static void fail(const char* e) {
  led(64, 0, 0);
  say(TFT_RED, "%s", e);
  say(TFT_WHITE, "Done. Reset this board to try again.");
}

// a file from the board's flash, max bytes at most
static bool load(const char* path, std::vector<uint8_t>& out, uint32_t max) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  out.resize(max + 1);
  size_t n = fread(out.data(), 1, out.size(), f);
  fclose(f);
  out.resize(n);
  return n > 0 && n <= max;
}

// the firmwares in the board's flash, by name
static std::vector<Firmware> firmwares() {
  std::vector<Firmware> l;
  if (DIR* d = opendir(FW_DIR)) {
    while (dirent* e = readdir(d)) {
      std::string f = e->d_name;
      if (f[0] != '.' && f.size() > 5 && !strcasecmp(f.c_str() + f.size() - 5, ".fwsc")) {
        l.push_back({f, f.substr(0, f.size() - 5)});
      }
    }
    closedir(d);
  }
  std::sort(l.begin(), l.end(), [](const Firmware& a, const Firmware& b) {
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  if (l.size() > LIST_MAX) {
    l.resize(LIST_MAX);
  }
  return l;
}

// its name for the FM-1's screen: 7-bit, NAME_MAX at most
static std::string shortName(const Firmware& f) {
  std::string out;
  for (char c : f.name) {
    if (out.size() < NAME_MAX) {
      out += c >= 0x20 && c < 0x7F ? c : '?';
    }
  }
  return out;
}

// a running FM-1 on the USB host, within ms: its device, or -1
static int findFm1(uint32_t ms) {
  for (uint32_t start = millis(); millis() - start < ms; delay(100)) {
    for (int d = 0; d < MIDI_USB; d++) {
      if (fm1::candidate(d)) {
        return d;
      }
    }
  }
  return -1;
}

// the picker on USB, within ms: its device, or -1
static int findPicker(uint32_t ms) {
  for (uint32_t start = millis(); millis() - start < ms; delay(100)) {
    for (int d = 0; d < MIDI_USB; d++) {
      midi::Dev v;
      if (midi::dev(d, v) && v.on && v.vid == PICKER_VID && v.pid == PICKER_PID) {
        return d;
      }
    }
  }
  return -1;
}

// the FM-1 into its boot ROM: 0 there, or an error (after it's restarted into key mode: never returns then)
static const char* toRom() {
  if (hal::recoveryMode()) {
    led(64, 48, 0);
    say(TFT_CYAN, "Switch the FM-1 OFF, then ON");
    hal::keyStart();
    for (int last = -1; hal::keyState() != hal::KEY_HOST; delay(50)) {
      int k = hal::keyState();
      if (k != last) {
        static const char* NAMES[] = {"", "waiting for it to be off", "sending the key: switch it ON",
                                      "it took the key", "USB host up"};
        say(TFT_CYAN, "%s", k >= 0 && k <= hal::KEY_HOST ? NAMES[k] : "?");
        last = k;
      }
    }
    return rom::attach(15000);
  }
  say(TFT_WHITE, "Looking for the FM-1...");
  int d = findFm1(15000);
  const char* e = "No FM-1 found.";
  if (d >= 0) {
    midi::Dev v;
    midi::dev(d, v);
    say(TFT_WHITE, "%s: to its boot ROM", v.name);
    e = rom::enter(d);
    if (!e) {
      e = rom::attach(10000);
    }
  }
  if (e) {  // (SLOOP, say: it doesn't take the SysEx) the key, from recovery mode
    say(TFT_YELLOW, "%s", e);
    say(TFT_YELLOW, "Restarting into key mode in 3 s...");
    delay(3000);
    hal::recoveryModeSet(true);  // (the board restarts)
  }
  return e;
}

// the picker's side: the list when it asks, until it says which one; that one, or -1 (it went)
static int listen(int d, const std::vector<Firmware>& list) {
  static const uint8_t HELLO[] = {0xF0, 0x7D, 0x4C, 0x42, 0x10, 0xF7};
  for (;;) {
    midi::Dev v;
    if (!midi::dev(d, v) || !v.on) {
      return -1;
    }
    uint8_t m[SYSEX_MAX];
    uint32_t n = midi::sysex(d, m);
    if (!n) {
      delay(20);
      continue;
    }
    if (n == sizeof HELLO && !memcmp(m, HELLO, n)) {
      for (size_t i = 0; i < list.size(); i++) {
        std::string name = shortName(list[i]);
        uint8_t out[8 + NAME_MAX] = {0xF0, 0x7D, 0x4C, 0x42, 0x01, (uint8_t)i, (uint8_t)list.size()};
        memcpy(out + 7, name.data(), name.size());
        out[7 + name.size()] = 0xF7;
        midi::sendWait(d, out, 8 + name.size(), 1000);
        delay(30);  // (it takes one SysEx at a time: one that comes before it took the last is lost)
      }
      say(TFT_WHITE, "Sent it the list (%u).", (unsigned)list.size());
      midi::drain(d);  // (the hellos that came meanwhile)
    } else if (n == 7 && !memcmp(m, HELLO, 4) && m[4] == 0x12 && m[5] < list.size()) {
      return m[5];
    }
  }
}

static void reading(uint32_t done, uint32_t total) {
  static uint32_t shown;
  if (done < shown) {
    shown = 0;
  }
  if (done >= shown + 256 * 1024 || done == total) {
    say(TFT_CYAN, "  read %u of %u KB", (unsigned)(done / 1024), (unsigned)(total / 1024));
    shown = done;
  }
}

// firmware f onto the FM-1 (device d, the picker), with JieLi's loader: 0, or an error
static const char* install(int d, const Firmware& f, const std::vector<uint8_t>& loader) {
  static Pkg pkg;
  const char* e = pkg.open((FW_DIR "/" + f.file).c_str());
  uint32_t at, n;
  if (e) {
    return e;
  }
  if (!pkg.flash(at, n) || strncmp(pkg.product, "FM-1_", 5)) {
    return "That package has no FM-1 flash image in it.";
  }
  if (n <= WRITE_FROM || n > WRITE_TO) {
    return "Its flash image is a size the FM-1's isn't.";
  }
  const uint8_t* want = pkg.image + at;
  uint32_t end = n;  // (as far as its image goes: 0x93000 for most, more for some: baudgirl 0xBA000, stock V16 0xA9000)
  rom::Chip chip;
  led(48, 0, 48);
  say(TFT_WHITE, "To its boot ROM, and JieLi's loader");
  if ((e = rom::enter(d)) || (e = rom::attach(10000)) || (e = rom::loader(loader.data(), loader.size(), chip))) {
    return e;
  }
  std::vector<uint8_t> now;
  if (chip.key != FM1_KEY || chip.flashId != FM1_FLASH || chip.size < WRITE_TO) {
    e = "That isn't the FM-1's chip: nothing was written.";
  } else {
    say(TFT_WHITE, "Reading its flash");
    e = rom::read(chip, now, reading);
  }
  std::vector<uint32_t> sectors;
  for (uint32_t a = WRITE_FROM; !e && a < end; a += SECTOR) {
    if (memcmp(want + a, &now[a], std::min<uint32_t>(SECTOR, end - a))) {
      sectors.push_back(a);
    }
  }
  if (!e) {
    say(TFT_WHITE, "Writing %u sectors", (unsigned)sectors.size());
  }
  uint8_t sec[SECTOR];
  for (size_t k = 0; !e && k < sectors.size(); k++) {
    uint32_t a = sectors[k];
    memcpy(sec, &now[a], SECTOR);  // (the end of a short image: as it is)
    memcpy(sec, want + a, std::min<uint32_t>(SECTOR, end - a));
    for (int t = 0; t < 3; t++) {
      if (!(e = rom::writeSector(a, sec))) {
        break;
      }
      hal::log("lil: sector %06X: %s", (unsigned)a, e);
    }
    if ((k + 1) % 16 == 0 || k + 1 == sectors.size()) {
      say(TFT_CYAN, "  %u of %u", (unsigned)(k + 1), (unsigned)sectors.size());
    }
  }
  rom::leave();  // (it restarts: into the new firmware)
  if (e && !sectors.empty()) {
    static std::string err;
    err = std::string(e) + " Reset this board to try again before switching the FM-1 off.";
    return err.c_str();
  }
  return e;
}

static void run(void*) {
  std::vector<uint8_t> loader;
  std::vector<Firmware> list;
  const char* e = 0;
  char sha[65];
  led(0, 0, 64);
  if (!fsOk) {
    e = "Its flash didn't mount.";
  } else if (!hal::sha256(LOADER, sha) || strcmp(sha, LOADER_SHA256) || !load(LOADER, loader, LOADER_SIZE)) {
    e = "No wl82loader.bin (or not JieLi's).";
  } else if ((list = firmwares()).empty()) {
    e = "No firmwares (fm1/*.fwsc).";
  }
  if (e || (e = toRom())) {
    if (e) {
      fail(e);
    }
    return vTaskDelete(0);
  }
  say(TFT_WHITE, "%u firmwares. The picker to the FM-1...", (unsigned)list.size());
  if ((e = rom::run(PICKER_AT, pickerStart, pickerEnd - pickerStart))) {
    fail(e);
    return vTaskDelete(0);
  }
  int d = findPicker(10000);
  if (d < 0) {
    fail("The picker didn't show up on USB.");
    return vTaskDelete(0);
  }
  led(0, 48, 48);
  say(TFT_GREEN, "Pick one on the FM-1.");
  int i = listen(d, list);
  if (i < 0) {
    fail("The picker went (the FM-1 restarted?).");
    return vTaskDelete(0);
  }
  say(TFT_GREEN, "Installing %s", list[i].file.c_str());
  if ((e = install(d, list[i], loader))) {
    fail(e);
  } else {
    led(0, 64, 0);
    say(TFT_GREEN, "Installed. Unplug this board.");
  }
  vTaskDelete(0);
}

// ---- on a computer: the web page's commands (at the top) ----

static bool safeName(const char* n) {
  size_t k = strlen(n);
  return k && k < 64 && !strchr(n, '/') && n[0] != '.';
}

static bool isPkg(const char* n) {
  size_t k = strlen(n);
  return k > 5 && !strcasecmp(n + k - 5, ".fwsc");
}

// where NAME lives: wl82loader.bin, or a firmware; "" if it's neither
static std::string pathOf(const char* n) {
  if (!safeName(n)) {
    return "";
  }
  if (!strcmp(n, "wl82loader.bin")) {
    return LOADER;
  }
  return isPkg(n) ? std::string(FW_DIR "/") + n : "";
}

static void info() {
  char sha[65];
  bool loader = hal::sha256(LOADER, sha) && !strcmp(sha, LOADER_SHA256);
  Serial.printf("INFO lilfm1buddy " VERSION " loader=%d files=%u free=%u total=%u\n", loader,
                (unsigned)firmwares().size(), (unsigned)(LittleFS.totalBytes() - LittleFS.usedBytes()),
                (unsigned)LittleFS.totalBytes());
}

static void ls() {
  if (DIR* d = opendir(FW_DIR)) {
    while (dirent* e = readdir(d)) {
      struct stat st;
      std::string p = std::string(FW_DIR "/") + e->d_name;
      if (isPkg(e->d_name) && !stat(p.c_str(), &st)) {
        Serial.printf("FILE %s %u\n", e->d_name, (unsigned)st.st_size);
      }
    }
    closedir(d);
  }
  Serial.println("END");
}

static void rm(const char* n) {
  std::string p = pathOf(n);
  Serial.println(!p.empty() && !remove(p.c_str()) ? "OK" : "ERR No such file.");
}

// NAME's bytes from the web page, into a .part first; checked, then in its place
static const char* put(const char* n, uint32_t size) {
  std::string p = pathOf(n);
  if (p.empty()) {
    return "Only wl82loader.bin and .fwsc files.";
  }
  if (size + 16384 > LittleFS.totalBytes() - LittleFS.usedBytes()) {
    return "No room for it.";
  }
  mkdir(FW_DIR, 0777);
  std::string part = p + ".part";
  FILE* f = fopen(part.c_str(), "wb");
  if (!f) {
    return "Can't write it.";
  }
  static uint8_t buf[PIECE];
  Serial.println("READY");
  uint32_t got = 0;
  while (got < size) {
    uint32_t want = std::min<uint32_t>(PIECE, size - got), have = 0;
    for (uint32_t t = millis(); have < want && millis() - t < 5000;) {
      int k = Serial.read(buf + have, want - have);
      if (k > 0) {
        have += k;
        t = millis();
      } else {
        delay(1);
      }
    }
    if (have < want || fwrite(buf, 1, want, f) != want) {
      fclose(f);
      remove(part.c_str());
      return have < want ? "It stopped coming." : "Can't write it (full?).";
    }
    got += want;
    Serial.printf("ACK %u\n", (unsigned)got);
  }
  fclose(f);
  const char* e = 0;
  char sha[65];
  if (!hal::sha256(part.c_str(), sha)) {
    e = "Can't read it back.";
  } else if (p == LOADER && strcmp(sha, LOADER_SHA256)) {
    e = "That isn't JieLi's wl82loader.bin.";
  } else if (p != LOADER) {
    static Pkg pkg;
    uint32_t at, k;
    e = pkg.open(part.c_str());
    if (!e && (!pkg.flash(at, k) || strncmp(pkg.product, "FM-1_", 5) || k <= WRITE_FROM || k > WRITE_TO)) {
      e = "That isn't an FM-1 firmware package.";
    }
    pkg.close();
  }
  if (e) {
    remove(part.c_str());
    return e;
  }
  remove(p.c_str());
  rename(part.c_str(), p.c_str());
  Serial.printf("OK %s\n", sha);
  return 0;
}

static void console() {
  static char line[160];
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
    char name[80];
    unsigned size;
    if (!strcmp(line, "info")) {
      info();
    } else if (!strcmp(line, "ls")) {
      ls();
    } else if (!strncmp(line, "rm ", 3)) {
      rm(line + 3);
    } else if (sscanf(line, "put %79s %u", name, &size) == 2) {
      if (const char* e = put(name, size)) {
        Serial.printf("ERR %s\n", e);
      }
    } else if (!strcmp(line, "log")) {
      board::printLog();
      if (FILE* f = fopen(LOG, "r")) {
        Serial.println("--- log.txt (lilfm1buddy)");
        char l[200];
        while (fgets(l, sizeof l, f)) {
          Serial.print(l);
        }
        fclose(f);
      }
      Serial.println("END");
    } else if (line[0]) {
      Serial.println("ERR What's that?");
    }
  }
}

void setup() {
  Serial.setRxBufferSize(2 * PIECE);
  Serial.begin(115200);
  board::begin();
  fsOk = LittleFS.begin(true, FS);  // (a new board: its flash formatted, once)
  if (fsOk && !hal::onComputer() && !hal::recoveryMode()) {
    remove(LOG);  // (a new log for each try; key mode's restart goes on with it)
  }
#ifdef SCREEN
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
#endif
  say(TFT_WHITE, "lilfm1buddy " VERSION);
  if (hal::onComputer()) {
    led(16, 16, 16);
    say(TFT_WHITE, "On a computer: the web page's serial port");
    return;
  }
  // the serial port off first, in recovery mode: Serial.end() drives D+ and D- low, and the key is on them
  board::usbBegin();
  xTaskCreatePinnedToCore(run, "lil", 16384, 0, 1, 0, 1);
}

void loop() {
  if (hal::onComputer()) {
    console();
  }
  board::loop();  // (the log: only the main loop writes it)
  delay(5);
}
