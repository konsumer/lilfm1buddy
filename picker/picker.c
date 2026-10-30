/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Charles Vestal (the screen); David Konsumer
 * (the menu) */
/* lilfm1buddy's picker: felucca-bsp's update loader (loader/loader.c, ldr_screen.c), with a menu in front.
 *
 * The ESP32 writes it to RAM through the boot ROM and jumps to it (esp32/rom.cpp). It's then a USB-MIDI device
 * ("Felucca Update", 1209:0002) that answers M-UPGRADE as "ota-FM-1_900", as the loader does, and:
 *
 *   picker -> ESP32  F0 7D 4C 42 10 F7          hello: send the list (once a second until it has all of it)
 *   ESP32 -> picker  F0 7D 4C 42 01 i n NAME F7  firmware i of n (NAME: ASCII, 7-bit)
 *   picker -> ESP32  F0 7D 4C 42 12 i F7        install firmware i (once a second, until the ESP32 sends the boot
 *                                               ROM's key: F0 22 24 35 7D F7)
 *   ESP32 -> picker  F0 7D 4C 42 02 L F7        its own battery: L 0..3 bars (it doesn't send it if it can't tell)
 *
 * Its header shows both batteries: the FM-1's (its ADC, in Felucca's bars) and the ESP32's.
 *
 * Then the ESP32 writes it through the boot ROM and JieLi's loader, as fm1buddy's recovery does: the flash driver
 * here (fm1_flash.h) wants the flash as the SPL leaves it, and the boot ROM doesn't. Nothing is written until one is
 * picked: switching the FM-1 off and on keeps what's on it. Any knob moves, any button picks (and again to be
 * sure), a piano key jumps to its row. Runs from RAM with interrupts off: USB is polled. Single compilation unit. */
#include <stdint.h>
#define FELUCCA_LOADER 1
#define FELUCCA_CDC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_USB_PID 0x0002
#define FELUCCA_ID "ota-FM-1_900"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
/* as the loader: the app area and update records; never the head, nor Felucca's store */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x97000u) || FL_IN(off, n, 0xE0000u, 0xFC000u))
#include "fm1_flash.h"
static void ldr_poll(void);
#define FM1_INPUT_IDLE() ldr_poll()
#include "fm1_input.h"
#include "libc.c"
#include "usb.c"

static uint32_t ldr_ms(void) /* monotonic ms; survives the TIMER4 wrap (178.9 s) */
{
  static uint32_t last, acc, ms;
  uint32_t now = fm1_ticks();
  acc += now - last;
  last = now;
  ms += acc / (1000u * FM1_TICKS_PER_US);
  acc %= 1000u * FM1_TICKS_PER_US;
  return ms;
}

static void ldr_poll(void) /* USB at ~2 kHz, watchdog */
{
  static uint32_t last;
  uint32_t now = fm1_ticks();
  fm1_wdt_feed();
  if (now - last >= 500u * FM1_TICKS_PER_US) {
    last = now;
    usb_poll();
  }
}

/* ---- ota.c hooks (the SysEx frame hooks are in usb.c) ---- */
static uint32_t ota_now_ms(void) { return ldr_ms(); }
static void ota_idle(void) { ldr_poll(); }
static int ota_erase(uint32_t off)
{
  (void)off;
  return -1;
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
  (void)off;
  (void)p;
  (void)n;
  return -1;
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
  (void)step;
  (void)code;
}
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "ota.c"

static void ldr_record_clear(void) { fm1_updata_parm_clear(); }
#include "screen.c"

/* ---- the menu ---- */
#define PK_MAX 24u   /* firmwares */
#define PK_NAME 20u  /* a row's characters (scale 2: 12 px each) */
#define PK_ROWS 8u   /* rows on screen */
#define PK_ROW_H 22u
#define PK_TOP 40u

static char pk_names[PK_MAX][PK_NAME + 1u];
static uint8_t pk_have[PK_MAX];
static uint32_t pk_n;          /* firmwares, as the ESP32 said (0: no list yet) */
static uint32_t pk_sel, pk_top; /* the highlighted row, the first on screen */
static int32_t pk_batt_raw;     /* the FM-1's battery: smoothed ADC (0: not read yet) */
static int32_t pk_fm1 = -1, pk_lil = -1; /* the batteries, 0..3 bars (-1: not known) */

static void pk_send(uint8_t cmd, int32_t arg)
{
  uint8_t m[7] = {0xF0, 0x7D, 0x4C, 0x42, cmd, 0, 0xF7};
  if (arg < 0) {
    m[5] = 0xF7;
    ota_wire_send(m, 6);
  } else {
    m[5] = (uint8_t)arg;
    ota_wire_send(m, 7);
  }
}

static int pk_complete(void)
{
  uint32_t i;
  if (!pk_n)
    return 0;
  for (i = 0; i < pk_n; i++)
    if (!pk_have[i])
      return 0;
  return 1;
}

/* one of ours (the bytes after F0 7D 4C 42), from the ESP32 */
static void pk_frame(const uint8_t *p, uint32_t n)
{
  uint32_t i, k;
  if (n >= 3u && p[0] == 0x01 && p[1] < PK_MAX && p[2] && p[2] <= PK_MAX) {
    i = p[1];
    if (pk_n != p[2]) { /* a new list */
      pk_n = p[2];
      for (k = 0; k < PK_MAX; k++)
        pk_have[k] = 0;
    }
    for (k = 0; k < PK_NAME && 3u + k < n; k++) {
      char c = (char)p[3u + k];
      pk_names[i][k] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    pk_names[i][k] = 0;
    pk_have[i] = 1;
  } else if (n >= 2u && p[0] == 0x02 && p[1] <= 3u)
    pk_lil = p[1];
}

/* a battery: an outline and up to 3 bars, at x, y (24 x 9) */
static void pk_cell(uint32_t x, uint32_t y, int32_t bars)
{
  uint16_t c = bars == 0 ? SCR_BAR : SCR_FG;
  int32_t b;
  scr_fill(x, y, 21, 9, c);
  scr_fill(x + 1u, y + 1u, 19, 7, SCR_BG);
  scr_fill(x + 21u, y + 2u, 2, 5, c);
  for (b = 0; b < bars; b++)
    scr_fill(x + 3u + (uint32_t)b * 6u, y + 2u, 4, 5, c);
}

/* both batteries, under the title */
static void pk_batts(void)
{
  uint32_t w = pk_lil >= 0 ? 116u : 52u, x = (240u - w) / 2u;
  scr_fill(0, 27, 240, 11, SCR_BG);
  if (pk_fm1 < 0)
    return;
  scr_line(x, 28, "FM-1", 1, SCR_FG, SCR_BG);
  pk_cell(x + 28u, 27, pk_fm1);
  if (pk_lil >= 0) {
    scr_line(x + 64u, 28, "BUDDY", 1, SCR_FG, SCR_BG);
    pk_cell(x + 98u, 27, pk_lil);
  }
}

/* the FM-1's battery, every second: Felucca's smoothing and bars (ui_draw.c) */
static int pk_batt_poll(uint32_t now)
{
  static uint32_t last;
  int32_t b, was = pk_fm1;
  if (pk_batt_raw && now - last < 1000u)
    return 0;
  last = now;
  b = fm1_adc_read(FM1_ADC_BATT);
  if (b > 0)
    pk_batt_raw = pk_batt_raw ? pk_batt_raw + (b - pk_batt_raw) / 32 : b;
  if (pk_batt_raw)
    pk_fm1 = pk_batt_raw >= 591 ? 3 : pk_batt_raw >= 561 ? 2 : pk_batt_raw >= 531 ? 1 : 0;
  return pk_fm1 != was;
}

static void pk_row(uint32_t r)
{
  uint32_t y = PK_TOP + (r - pk_top) * PK_ROW_H, on = r == pk_sel;
  scr_fill(0, y, 240, PK_ROW_H, on ? SCR_BAR : SCR_BG);
  scr_line(8, y + 4u, pk_names[r], 2, on ? SCR_BG : SCR_FG, on ? SCR_BAR : SCR_BG);
}

static void pk_list(void)
{
  uint32_t r, rows = pk_n;
  if (pk_sel < pk_top)
    pk_top = pk_sel;
  if (pk_sel >= pk_top + PK_ROWS)
    pk_top = pk_sel - PK_ROWS + 1u;
  scr_fill(0, PK_TOP, 240, PK_ROWS * PK_ROW_H, SCR_BG);
  for (r = pk_top; r < rows && r < pk_top + PK_ROWS; r++)
    pk_row(r);
}

static void pk_screen(const char *title, const char *a, const char *b)
{
  scr_fill(0, 0, 240, 240, SCR_BG);
  scr_center(10, title, 2, SCR_FG);
  pk_batts();
  if (a)
    scr_center(96, a, 2, SCR_FG);
  if (b)
    scr_center(130, b, 1, SCR_FG);
}

static void pk_menu(void)
{
  pk_screen("PICK A FIRMWARE", 0, 0);
  scr_center(222, "KNOB: MOVE   BUTTON: PICK", 1, SCR_FG);
  pk_list();
}

/* what the keys and knobs did since last time: steps (any knob), a button pressed, a piano key pressed (-1: none) */
static void pk_input(int32_t *steps, uint32_t *button, int32_t *key)
{
  uint32_t e, notes;
  fm1_input_scan();
  *steps = 0;
  for (e = 0; e < FM1_NENC; e++)
    *steps += fm1_enc_take(e);
  *button = fm1_input_edges(0);
  notes = fm1_input_note_edges();
  *key = -1;
  for (e = 0; e < 27u; e++)
    if (notes & (1u << e)) {
      *key = (int32_t)e;
      break;
    }
}

enum { PK_WAIT, PK_MENU, PK_SURE, PK_ASKED };

static void pk_main(void)
{
  uint32_t state = PK_WAIT, said = 0, button;
  int32_t shown_lil = -1;
  int32_t steps, key;
  fm1_wdt_arm(0x0D); /* ~8 s, fed by ldr_poll */
  fm1_time_init();
  scr_init();
  fm1_input_init();
  fm1_adc_init();
  pk_screen("LILFM1BUDDY", "WAITING FOR", "THE LIST OF FIRMWARES");
  usb_start();
  for (;;) {
    const uint8_t *p;
    uint32_t n, now = ldr_ms();
    ldr_poll();
    usb_retry(now);
    if (usb.uboot_req) { /* soft key: mask-ROM UBOOT, as in Felucca */
      usb_detach();
      fm1_delay_ms(30);
      fm1_enter_uboot();
    }
    if (ota_frame_get(&p, &n) && n >= 3u && p[0] == 0x7D && p[1] == 0x4C && p[2] == 0x42) {
      pk_frame(p + 3, n - 3u);
      ota_frame_done();
    }
    ota_service(); /* handshake: who it is */
    usb.ota_req = 0; /* (M-UPGRADE: not here) */
    if (state == PK_WAIT && pk_complete()) {
      state = PK_MENU;
      pk_menu();
    }
    if ((state == PK_WAIT || state == PK_ASKED) && now - said >= 1000u) {
      said = now;
      if (usb.config)
        pk_send(state == PK_WAIT ? 0x10 : 0x12, state == PK_WAIT ? -1 : (int32_t)pk_sel);
    }
    if (pk_batt_poll(now) | (pk_lil != shown_lil)) {
      shown_lil = pk_lil;
      pk_batts();
    }
    pk_input(&steps, &button, &key);
    if (state == PK_MENU) {
      uint32_t was = pk_sel, rows = pk_n;
      if (key >= 0 && (uint32_t)key < rows)
        pk_sel = (uint32_t)key;
      else if (steps > 0)
        pk_sel = pk_sel + (uint32_t)steps >= rows ? rows - 1u : pk_sel + (uint32_t)steps;
      else if (steps < 0)
        pk_sel = (uint32_t)-steps > pk_sel ? 0 : pk_sel - (uint32_t)-steps;
      if (pk_sel != was) {
        if (pk_sel >= pk_top && pk_sel < pk_top + PK_ROWS && was >= pk_top && was < pk_top + PK_ROWS) {
          pk_row(was);
          pk_row(pk_sel);
        } else
          pk_list();
      }
      if (button) {
        state = PK_SURE;
        pk_screen("INSTALL", pk_names[pk_sel], "BUTTON: YES   KNOB: NO");
        if (pk_fm1 == 0)
          scr_center(160, "LOW BATTERY: CHARGE THE FM-1 FIRST", 1, SCR_BAR);
      }
    } else if (state == PK_SURE) {
      if (button) {
        state = PK_ASKED;
        said = 0;
        pk_screen("INSTALLING", pk_names[pk_sel], "DO NOT UNPLUG");
      } else if (steps || key >= 0) {
        state = PK_MENU;
        pk_menu();
      }
    }
  }
}

extern uint32_t _bss_start[], _bss_end[];

void ldr_cstart(void) /* crt0_ldr.S: interrupts off, our stacks */
{
  uint32_t *p;
  ldr_record_clear();
  for (p = _bss_start; p < _bss_end; p++)
    *p = 0;
  pk_main();
}
