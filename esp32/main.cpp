// cyd-test: does a program of ours run in the FM-1's RAM, started by its boot ROM? On an fm1buddy board (the CYD-S3),
// with the FM-1 on its USB port. What happens is on the screen, and in fm1buddy/log.txt on the SD card.
//
//   1. the picker from the SD card: lilfm1buddy/picker.bin (picker/build.sh)
//   2. the FM-1 into its boot ROM: SysEx to its firmware (stock V15, Felucca-based ones). If that doesn't work, the
//      board restarts into fm1buddy's recovery mode, and the key does it as the FM-1 is switched on
//   3. the picker into its RAM, and a jump to it. Nothing is written to its flash: switching the FM-1 off and on
//      brings its firmware back
//   4. the USB devices that come next (the picker for now: felucca-bsp's update loader, "ota-FM-1_900")
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <stdarg.h>

#include <vector>

#include "board.h"
#include "fm1.h"
#include "hal.h"
#include "midi.h"
#include "rom.h"
#include "tusb.h"

#define PICKER "/sdcard/lilfm1buddy/picker.bin"
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
  hal::log("test: %s", s);
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
  say(TFT_WHITE, "Done. Switch the FM-1 off and on.");
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

// the USB port as the S3's controller sees it (HPRT): connected (something pulls D+ or D- up), enabled (reset done),
// the lines (D+, D-) and the speed
#define HPRT (0x60080000u + 0x440)
#define HAINTMSK (0x60080000u + 0x418)
static uint32_t port() {
  uint32_t h = REG_READ(HPRT);
  return (h & 1) | (h & 4) >> 1 | (h >> 10 & 3) << 2 | (h >> 17 & 3) << 4 | (REG_READ(HAINTMSK) & 0xFF) << 8;
}

static void sayPort(uint32_t p) {
  static const char* SPEED[] = {"high", "full", "low", "?"};
  say(TFT_CYAN, "port: %s%s, D+ %d D- %d, %s, ch %02X", p & 1 ? "connected" : "empty", p & 2 ? ", enabled" : "",
      (int)(p >> 2 & 1), (int)(p >> 3 & 1), SPEED[p >> 4 & 3], (unsigned)(p >> 8 & 0xFF));
}

// the USB port and the devices on it, as they change, for a while; whether a device came
static bool watch(uint32_t ms) {
  bool came = false;
  uint32_t seen[MIDI_USB] = {}, last = 0xFFFF;
  bool mounted[CFG_TUH_DEVICE_MAX + 1] = {};
  for (uint32_t start = millis(); millis() - start < ms; delay(20)) {
    uint32_t p = port();
    if (p != last) {
      sayPort(p);
      last = p;
    }
    for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX; a++) {
      bool m = tuh_mounted(a);
      if (m != mounted[a]) {
        uint16_t vid = 0, pid = 0;
        if (m) {
          tuh_vid_pid_get(a, &vid, &pid);
          say(TFT_GREEN, "device %u: %04X:%04X", a, vid, pid);
          came = true;
        } else {
          say(TFT_YELLOW, "device %u went", a);
        }
        mounted[a] = m;
      }
    }
    for (int d = 0; d < MIDI_USB; d++) {
      midi::Dev v;
      if (midi::dev(d, v) && v.on && v.serial != seen[d]) {
        seen[d] = v.serial;
        say(TFT_GREEN, "MIDI: %04X:%04X %s", v.vid, v.pid, v.name);
      }
    }
    if (hal::storageSeen(ROM_VID, ROM_PID)) {
      say(TFT_YELLOW, "The boot ROM is back (the picker crashed?)");
      return came;
    }
  }
  sayPort(port());
  return came;
}

static void test(void*) {
  std::vector<uint8_t> picker;
  const char* e = 0;
  if (!hal::sd()) {
    e = "No SD card.";
  } else if (!loadPicker(picker)) {
    e = "No " PICKER " (or too big).";
  }
  if (e) {
    fail(e);
    return vTaskDelete(0);
  }
  say(TFT_WHITE, "picker.bin: %u bytes", (unsigned)picker.size());
  if (hal::recoveryMode()) {
    say(TFT_CYAN, "Key mode: switch the FM-1 OFF, then ON");
    hal::keyStart();
    for (int last = -1; hal::keyState() != hal::KEY_HOST; delay(50)) {
      int k = hal::keyState();
      if (k != last) {
        static const char* NAMES[] = {"idle", "waiting for it to be off", "sending the key: switch it ON",
                                      "it took the key", "USB host up"};
        say(TFT_CYAN, "key: %s", k >= 0 && k <= hal::KEY_HOST ? NAMES[k] : "?");
        last = k;
      }
    }
  } else {
    say(TFT_WHITE, "Looking for a running FM-1...");
    int d = findFm1(15000);
    if (d >= 0) {
      midi::Dev v;
      midi::dev(d, v);
      say(TFT_WHITE, "%s: SysEx to its boot ROM", v.name);
      e = rom::enter(d);
      if (!e) {
        e = rom::attach(10000);
      }
    } else {
      e = "No FM-1 found.";
    }
    if (e) {  // (SLOOP, say: it doesn't take the SysEx) the key, from recovery mode
      say(TFT_YELLOW, "%s", e);
      say(TFT_YELLOW, "Restarting into key mode in 5 s...");
      delay(5000);
      hal::recoveryModeSet(true);  // (the board restarts)
      return vTaskDelete(0);
    }
  }
  if (hal::recoveryMode() && (e = rom::attach(15000))) {
    fail(e);
    return vTaskDelete(0);
  }
  sayPort(port());
  say(TFT_WHITE, "Boot ROM: picker to %08X", (unsigned)PICKER_AT);
  if ((e = rom::run(PICKER_AT, picker.data(), picker.size()))) {
    fail(e);
    return vTaskDelete(0);
  }
  say(TFT_GREEN, "Jumped. Watching USB for 30 s:");
  say(TFT_GREEN, "(the FM-1 should say UPDATING)");
  watch(30000);
  say(TFT_WHITE, "Done. Switch the FM-1 off and on.");
  vTaskDelete(0);
}

// on a computer: its serial port, for tools/s3.py (fm1buddy's) log, cat PATH and ls [PATH]
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
    }
  }
}

void setup() {
  Serial.begin(115200);
  board::begin();
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  say(TFT_WHITE, "lilfm1buddy test");
  if (hal::onComputer()) {
    say(TFT_WHITE, "On a computer: serial console");
    say(TFT_WHITE, "  third_party/fm1buddy/tools/s3.py log");
    return;
  }
  // the serial port off first, in recovery mode: Serial.end() drives D+ and D- low, and the key is on them
  board::usbBegin();
  xTaskCreatePinnedToCore(test, "test", 8192, 0, 1, 0, 1);
}

void loop() {
  if (hal::onComputer()) {
    console();
  }
  board::loop();  // (the log: only the main loop writes it)
  delay(5);
}
