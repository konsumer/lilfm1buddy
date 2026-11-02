// The LED: what the board is doing. An RGB LED (LED_RGB) shows it in colours and blinks, one of a colour (LED_PIN,
// lit at LED_ON) in the blinks alone. A short flash over either for activity: the drive read (green) or written
// (red), MIDI going through (white). On a one-colour LED, a flash is a moment off
#include <Arduino.h>
#include <driver/gpio.h>

#include <atomic>

#include "lil.h"

struct Look {
  uint8_t r, g, b;
  uint16_t blink[6];  // on, off, on, off... in ms (0 ends it; none: off)
};

// in Led's order
static const Look LOOKS[] = {
    {0, 0, 0, {0}},                                // off
    {0, 0, 48, {1000, 0}},                         // serial: blue
    {48, 48, 48, {100, 100, 100, 100, 100, 100}},  // to disk: three white blinks
    {0, 40, 40, {1000, 0}},                        // disk: cyan
    {0, 12, 0, {1000, 0}},                         // ready: dim green
    {0, 48, 0, {1000, 0}},                         // routing: green
    {0, 0, 64, {100, 900}},                        // looking: a blue blip a second
    {64, 48, 0, {500, 500}},                       // switch it off and on: a slow yellow blink
    {0, 48, 48, {900, 100}},                       // pick: cyan, a short gap a second
    {48, 48, 48, {100, 100}},                      // reading: a fast white blink
    {48, 0, 48, {100, 100}},                       // writing: a fast magenta blink
    {0, 64, 0, {1000, 0}},                         // done: bright green
    {64, 0, 0, {100, 150, 100, 150, 100, 1000}}};  // failed: three red blips, a pause
static const uint8_t FLASHES[][3] = {{0, 64, 0}, {64, 0, 0}, {48, 48, 48}};

static std::atomic<int> state{LED_OFF}, flashing{0};
static std::atomic<uint32_t> flashUntil{0};

// in a blink's on (or off) part, with a flash over it (f) or not (-1)
static void show(bool on, int f) {
  static int32_t last = -1;
#ifdef LED_RGB
  const uint8_t* c = f >= 0 ? FLASHES[f] : &LOOKS[state].r;
  int32_t v = f >= 0 || on ? c[0] << 16 | c[1] << 8 | c[2] : 0;
  if (v != last) {
    rgbLedWrite(LED_RGB, c[0] * !!v, c[1] * !!v, c[2] * !!v);
  }
#elif defined(LED_PIN)
  int32_t v = f >= 0 ? !on : on;
  if (v != last) {
    digitalWrite(LED_PIN, v ? LED_ON : !LED_ON);
  }
#else
  int32_t v = 0;
#endif
  last = v;
}

static void task(void*) {
  for (;;) {
    int s = state;
    const uint16_t* b = LOOKS[s].blink;
    if (!b[0]) {
      show(false, -1);
      delay(20);
      continue;
    }
    for (int i = 0; i < 6 && b[i] && state == s; i++) {
      for (uint32_t t = millis(); millis() - t < b[i] && state == s; delay(10)) {
        show(i % 2 == 0, (int32_t)(flashUntil - millis()) > 0 ? flashing.load() : -1);
      }
    }
  }
}

void led(Led l) {
  state = l;
}

void ledFlash(Flash f) {
  flashing = f;
  flashUntil = millis() + 40;
}

void ledBegin() {
#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);
#endif
  pinMode(BOOT_PIN, INPUT_PULLUP);
  xTaskCreatePinnedToCore(task, "led", 2048, 0, 1, 0, 1);
}

void ledEnd() {
  led(LED_OFF);
  delay(100);
#ifdef LED_PIN
  gpio_hold_en((gpio_num_t)LED_PIN);
  gpio_deep_sleep_hold_en();
#endif
}

bool bootPressed() {
  static bool was = true;  // (held at the start: not a press)
  bool down = digitalRead(BOOT_PIN) == LOW;
  bool pressed = down && !was;
  was = down;
  return pressed;
}
