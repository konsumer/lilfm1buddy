// lilfm1buddy on an fm1buddy board (the CYD-S3), with the FM-1 on its USB port: pick a firmware on the FM-1 itself,
// from the ones on the SD card (/fm1/*.fwsc, named as fm1buddy names them). What happens is on the board's screen,
// and in fm1buddy/log.txt on the SD card.
//
//   1. the FM-1 into its boot ROM: SysEx to its firmware (stock V15, Felucca-based ones). If that doesn't work, the
//      board restarts into fm1buddy's recovery mode, and the key does it as the FM-1 is switched on
//   2. the picker (lilfm1buddy/picker.bin, picker/build.sh) into its RAM, and a jump to it. Nothing is written to its
//      flash yet: switching the FM-1 off and on brings its firmware back
//   3. the picker asks for the list (picker.c says how), shows it, and says which one
//   4. that one is installed to it, as fm1buddy installs (fm1::install: the picker is an update loader)
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdarg.h>

#include <string>
#include <vector>

#include "board.h"
#include "catalog.h"
#include "fm1.h"
#include "hal.h"
#include "midi.h"
#include "rom.h"

#define PICKER "/sdcard/lilfm1buddy/picker.bin"
#define PICKER_VID 0x1209
#define PICKER_PID 0x0002
#define LIST_MAX 24  // picker.c's PK_MAX
#define NAME_MAX 20  // and PK_NAME
#define LINE_H 16

static TFT_eSPI tft;
static int line;

// a line on the screen, and in the log
static void say(uint16_t color, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(uint16_t color, const char* fmt, ...) {
  char s[120];
  va_list a;
  va_start(a, fmt);
  vsnprintf(s, sizeof s, fmt, a);
  va_end(a);
  hal::log("lil: %s", s);
  if (line + LINE_H > tft.height()) {
    tft.fillScreen(TFT_BLACK);
    line = 0;
  }
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(s, 4, line, 2);
  line += LINE_H;
}

static void fail(const char* e) {
  say(TFT_RED, "%s", e);
  say(TFT_WHITE, "Done. Reset this board to try again.");
}

static bool loadPicker(std::vector<uint8_t>& out) {
  FILE* f = fopen(PICKER, "rb");
  if (!f) {
    return false;
  }
  out.resize(PICKER_MAX + 1);
  size_t n = fread(out.data(), 1, out.size(), f);
  fclose(f);
  out.resize(n);
  return n > 0 && n <= PICKER_MAX;
}

// the firmwares on the SD card, as fm1buddy lists them
static std::vector<Firmware> firmwares() {
  std::string err;
  catalog::parseHub(catalog::hub, catalog::hubUpdated, err);  // (names from the Hub's list, if it's there)
  catalog::scanSd();
  std::vector<Firmware> l = catalog::sd;
  if (l.size() > LIST_MAX) {
    l.resize(LIST_MAX);
  }
  return l;
}

// its name for the FM-1's screen: 7-bit, NAME_MAX at most
static std::string shortName(const Firmware& f) {
  std::string n = f.name.empty() ? f.file : f.name + (f.version.empty() ? "" : " " + f.version);
  if (f.name.empty() && n.size() > 5 && n.compare(n.size() - 5, 5, ".fwsc") == 0) {
    n.resize(n.size() - 5);
  }
  std::string out;
  for (char c : n) {
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

static void progress(Fm1Step step, uint32_t bytes, uint32_t) {
  static Fm1Step last = STEP_DONE;
  static uint32_t shown;
  static const char* NAMES[] = {"starting", "checking", "the loader", "writing", "restarting", "done"};
  if (step != last) {
    say(TFT_CYAN, "install: %s", NAMES[step]);
    last = step;
    shown = 0;
  }
  if (bytes >= shown + 128 * 1024) {
    say(TFT_CYAN, "  %u KB", (unsigned)(bytes / 1024));
    shown = bytes;
  }
}

static void run(void*) {
  std::vector<uint8_t> picker;
  std::vector<Firmware> list;
  const char* e = 0;
  if (!hal::sd()) {
    e = "No SD card.";
  } else if (!loadPicker(picker)) {
    e = "No " PICKER " (or too big).";
  } else if ((list = firmwares()).empty()) {
    e = "No firmwares in /fm1 on the SD card.";
  }
  if (e || (e = toRom())) {
    if (e) {
      fail(e);
    }
    return vTaskDelete(0);
  }
  say(TFT_WHITE, "%u firmwares. The picker to the FM-1...", (unsigned)list.size());
  if ((e = rom::run(PICKER_AT, picker.data(), picker.size()))) {
    fail(e);
    return vTaskDelete(0);
  }
  int d = findPicker(10000);
  if (d < 0) {
    fail("The picker didn't show up on USB.");
    return vTaskDelete(0);
  }
  say(TFT_GREEN, "Pick one on the FM-1.");
  int i = listen(d, list);
  if (i < 0) {
    fail("The picker went (the FM-1 restarted?).");
    return vTaskDelete(0);
  }
  say(TFT_GREEN, "Installing %s", list[i].file.c_str());
  Pkg pkg;
  e = pkg.open(catalog::path(FW_DIR "/" + list[i].file).c_str());
  if (!e) {
    e = fm1::install(pkg, progress);
  }
  if (e) {
    fail(e);
  } else {
    say(TFT_GREEN, "Installed. Unplug this board.");
  }
  vTaskDelete(0);
}

// on a computer: its serial port, for tools/s3.py (fm1buddy's) log, cat PATH and ls [PATH]; and "disk": the board
// restarts as a USB disk (the SD card), until it's reset
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
    if (!strcmp(line, "log")) {
      board::printLog();
    } else if (!strncmp(line, "cat ", 4)) {
      board::printFile(line + 4);
    } else if (!strcmp(line, "ls") || !strncmp(line, "ls ", 3)) {
      board::printDir(line[2] ? line + 3 : "");
    } else if (!strcmp(line, "disk")) {
      hal::diskModeSet(true);  // (the board restarts)
    }
  }
}

void setup() {
  Serial.begin(115200);
  board::begin();
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  say(TFT_WHITE, "lilfm1buddy");
  if (hal::diskMode()) {  // (fm1buddy's: the SD card is the computer's)
    say(TFT_WHITE, "USB disk: the SD card is on the computer.");
    say(TFT_WHITE, "Eject it there, then reset this board.");
    board::usbBegin();
    return;
  }
  if (hal::onComputer()) {
    say(TFT_WHITE, "On a computer: serial console");
    say(TFT_WHITE, "  third_party/fm1buddy/tools/s3.py log");
    say(TFT_WHITE, "  \"disk\": the SD card as a USB disk");
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
