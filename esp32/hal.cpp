// What fm1buddy's code here (its USB host, the key, MIDI routing, Bluetooth MIDI, packages) needs from the board, and
// no more. And the log: this run's lines, kept to go to log.txt on the drive (written once, not a line at a time:
// the drive has no wear levelling), and to the serial port while a computer is on it
#include "hal.h"

#include <Arduino.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <stdarg.h>

#include <mutex>
#include <string>

#include "board.h"
#include "lil.h"

#define LOG_MAX 32768
#define KEY_MODE 0x4B455921
static RTC_NOINIT_ATTR uint32_t keyFlag;  // (kept through a restart, not a power cycle)
static bool keyMode = [] {
  bool on = keyFlag == KEY_MODE && esp_reset_reason() == ESP_RST_SW;
  keyFlag = 0;
  return on;
}();

static std::mutex logLock;
static std::string logText;

uint32_t hal::millis() {
  return ::millis();
}

void hal::log(const char* fmt, ...) {
  char s[160], line[180];
  va_list a;
  va_start(a, fmt);
  vsnprintf(s, sizeof s, fmt, a);
  va_end(a);
  snprintf(line, sizeof line, "%7.1f %s\n", ::millis() / 1000.0, s);
  std::lock_guard<std::mutex> g(logLock);
  if (logText.size() < LOG_MAX) {
    logText += line;
  }
  if (Serial) {
    Serial.print(line);
  }
}

void logSave() {
  std::lock_guard<std::mutex> g(logLock);
  if (FILE* f = fopen(LOG_FILE, "w")) {
    fwrite(logText.data(), 1, logText.size(), f);
    fclose(f);
  }
}

void logLoad() {
  std::string was;
  if (FILE* f = fopen(LOG_FILE, "r")) {
    char b[256];
    for (size_t n; (n = fread(b, 1, sizeof b, f));) {
      was.append(b, n);
    }
    fclose(f);
  }
  std::lock_guard<std::mutex> g(logLock);
  logText = was + "--- (restarted)\n" + logText;
}

// the key (key_s3.cpp): a mode the board restarts into, the FM-1 straight on its port
bool hal::recoveryMode() {
  return keyMode;
}

void hal::recoveryModeSet(bool on) {
  keyFlag = on ? KEY_MODE : 0;
  hal::log("lil: restarting%s", on ? " into key mode" : "");
  logSave();
  esp_restart();
}

static void hex(const uint8_t sum[32], char out[65]) {
  for (int i = 0; i < 32; i++) {
    sprintf(out + i * 2, "%02x", sum[i]);
  }
}

bool hal::sha256(const uint8_t* p, size_t n, char out[65]) {
  uint8_t sum[32];
  if (mbedtls_sha256(p, n, sum, 0)) {
    return false;
  }
  hex(sum, out);
  return true;
}

bool hal::sha256(const char* path, char out[65]) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  mbedtls_sha256_context c;
  mbedtls_sha256_init(&c);
  mbedtls_sha256_starts(&c, 0);
  static uint8_t buf[4096];
  for (size_t n; (n = fread(buf, 1, sizeof buf, f));) {
    mbedtls_sha256_update(&c, buf, n);
  }
  fclose(f);
  uint8_t sum[32];
  mbedtls_sha256_finish(&c, sum);
  mbedtls_sha256_free(&c);
  hex(sum, out);
  return true;
}

// midi.cpp's midimap.txt: on the drive
namespace catalog {
std::string path(const std::string& p) {
  return FS + p;
}
}  // namespace catalog

// fm1buddy's, that lilfm1buddy hasn't: Wi-Fi, USB audio, USB MIDI for a computer
void hal::wifiOff() {}
void board::audioPoll() {}
bool board::computerOn() {
  return false;
}
uint32_t board::computerOut(const uint8_t*, uint32_t n) {
  return n;
}
