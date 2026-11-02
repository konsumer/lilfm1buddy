/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Charles Vestal (the screen); David Konsumer
 * (the menu) */
/* lilfm1buddy's picker: felucca-bsp's update loader (loader/loader.c, ldr_screen.c), with a menu in front.
 *
 * The ESP32 writes it to RAM through the boot ROM and jumps to it (esp32/rom.cpp): built in, or _lil on its
 * drive. It's then a USB-MIDI device ("Felucca Update", 1209:0002) that answers M-UPGRADE as "ota-FM-1_900", as the
 * loader does, and speaks the ESP32's commands (esp32/main.cpp), a line in each SysEx: F0 7D 4C 42 TEXT F7. It asks
 * "info" (for the ESP32's battery) and "ls" (the firmwares and backups on its drive), once a second until both are
 * answered; then, for the row picked, "install NAME" or "backup" (once a second, until the ESP32 sends the boot ROM's
 * key, F0 22 24 35 7D F7, and does it).
 *
 * Its header shows both batteries: the FM-1's (its ADC, in Felucca's bars) and the ESP32's.
 *
 * The ESP32 then writes it through the boot ROM and JieLi's loader, as fm1buddy's recovery does: the flash driver
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
#define PK_MAX 24u   /* rows: backing up, then files */
#define PK_FILE 64u  /* a file's name, whole */
#define PK_NAME 20u  /* a row's characters (scale 2: 12 px each) */
#define PK_ROWS 8u   /* rows on screen */
#define PK_ROW_H 22u
#define PK_TOP 40u
#define PK_LINE 120u

static char pk_files[PK_MAX][PK_FILE + 1u]; /* row 0: "" (backing up) */
static char pk_names[PK_MAX][PK_NAME + 1u];
static uint32_t pk_n;           /* rows */
static uint32_t pk_sel, pk_top; /* the highlighted row, the first on screen */
static int32_t pk_batt_raw;     /* the FM-1's battery: smoothed ADC (0: not read yet) */
static int32_t pk_fm1 = -1, pk_lil = -1; /* the batteries, 0..3 bars (-1: not known) */

/* what was asked of the ESP32, and is being answered */
enum { PK_ASK_INFO, PK_ASK_LS, PK_ASKED_ALL };
static uint32_t pk_asking;
static char pk_err[PK_LINE + 1u];

/* a line to the ESP32: F0 7D 4C 42 TEXT F7 */
static void pk_say(const char *a, const char *b)
{
  uint8_t m[4u + PK_LINE + 1u] = {0xF0, 0x7D, 0x4C, 0x42};
  uint32_t n = 4;
  for (; *a && n < 4u + PK_LINE; a++)
    m[n++] = (uint8_t)*a;
  for (; b && *b && n < 4u + PK_LINE; b++)
    m[n++] = (uint8_t)*b;
  m[n++] = 0xF7;
  ota_wire_send(m, n);
}

static int pk_ends(const char *s, uint32_t n, const char *e)
{
  uint32_t k = str_len(e), i;
  if (n <= k)
    return 0;
  for (i = 0; i < k; i++) {
    char c = s[n - k + i];
    if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != e[i])
      return 0;
  }
  return 1;
}

/* "NAME BYTES" from ls: a row, if it's a firmware (.fwsc) or a backup (.bin) */
static void pk_file(const char *s)
{
  uint32_t n = str_len(s), k;
  while (n && s[n - 1u] != ' ')
    n--;
  if (n)
    n--; /* (the name: up to the last space) */
  if (!n || n > PK_FILE || pk_n >= PK_MAX || s[0] == '.' || !(pk_ends(s, n, ".fwsc") || pk_ends(s, n, ".bin")) ||
      (n == 14u && !memcmp(s, "wl82loader.bin", 14)))
    return;
  memcpy(pk_files[pk_n], s, n);
  pk_files[pk_n][n] = 0;
  while (n && s[n - 1u] != '.')
    n--;
  for (k = 0; k < PK_NAME && k + 1u < n; k++) {
    char c = s[k];
    pk_names[pk_n][k] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
  }
  pk_names[pk_n][k] = 0;
  pk_n++;
}

/* a line from the ESP32 (after F0 7D 4C 42): the answer to what was asked */
static void pk_line(const uint8_t *p, uint32_t n)
{
  char s[PK_LINE + 1u];
  uint32_t i;
  const char *b;
  if (n > PK_LINE)
    n = PK_LINE;
  for (i = 0; i < n; i++)
    s[i] = (char)p[i];
  s[n] = 0;
  if (str_eq(s, "OK")) {
    if (pk_asking == PK_ASK_INFO) {
      pk_asking = PK_ASK_LS;
      pk_n = 1; /* (row 0: backing up) */
      pk_say("ls", 0);
    } else if (pk_asking == PK_ASK_LS)
      pk_asking = PK_ASKED_ALL;
  } else if (s[0] == 'E' && s[1] == 'R' && s[2] == 'R')
    str_cpy(pk_err, s + (n > 4u ? 4u : n), sizeof pk_err);
  else if (pk_asking == PK_ASK_INFO) {
    for (b = s; *b; b++)
      if (!memcmp(b, "battery=", 8)) {
        pk_lil = b[8] >= '0' && b[8] <= '3' ? b[8] - '0' : -1;
        break;
      }
  } else if (pk_asking == PK_ASK_LS)
    pk_file(s);
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
  pk_screen("PICK ONE", 0, 0);
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

/* the picked row, to the ESP32: install it, or back up (once a second, until the boot ROM's key comes) */
static void pk_do(void)
{
  if (pk_sel)
    pk_say("install ", pk_files[pk_sel]);
  else
    pk_say("backup", 0);
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
  str_cpy(pk_names[0], "BACK UP THIS FM-1", sizeof pk_names[0]);
  pk_screen("LILFM1BUDDY", "WAITING FOR", "THE LIST");
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
    if (ota_frame_get(&p, &n) && n >= 4u && p[0] == 0x7D && p[1] == 0x4C && p[2] == 0x42 && p[3] >= 0x20u) {
      pk_line(p + 3, n - 3u);
      ota_frame_done();
    }
    ota_service(); /* handshake: who it is */
    usb.ota_req = 0; /* (M-UPGRADE: not here) */
    if (pk_err[0]) {
      pk_screen("BUDDY SAYS", 0, pk_err);
      pk_err[0] = 0;
      if (state == PK_ASKED)
        state = PK_SURE; /* (button: again) */
    }
    if (state == PK_WAIT && pk_asking == PK_ASKED_ALL) {
      state = PK_MENU;
      pk_menu();
    }
    if ((state == PK_WAIT || state == PK_ASKED) && now - said >= 1000u) {
      said = now;
      if (usb.config && state == PK_ASKED)
        pk_do();
      else if (usb.config && pk_asking != PK_ASKED_ALL) { /* (from the top, until it's all come) */
        pk_asking = PK_ASK_INFO;
        pk_say("info", 0);
      }
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
        pk_screen(pk_sel ? "INSTALL" : "BACK UP", pk_sel ? pk_names[pk_sel] : "ALL OF ITS FLASH", "BUTTON: YES   KNOB: NO");
        if (pk_fm1 == 0)
          scr_center(160, "LOW BATTERY: CHARGE THE FM-1 FIRST", 1, SCR_BAR);
      }
    } else if (state == PK_SURE) {
      if (button) {
        state = PK_ASKED;
        said = now;
        pk_do();
        pk_screen(pk_sel ? "INSTALLING" : "BACKING UP", pk_names[pk_sel], "DO NOT UNPLUG");
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
