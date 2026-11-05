/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; Charles Vestal (the screen); David Konsumer
 * (the menu) */
/* lilfm1buddy's menu: fomajam's update loader (platform/loader: felucca-bsp's, with fomajam's platform for each
 * device), with a menu in front. Built for the FM-1 (lilfm1.bin) and, with FOMA_SMK37, the SMK-37 (lilsmk.bin: its
 * screen, and its keys, buttons and knobs through smk_input.h, which keeps fm1_input.h's calls).
 *
 * The ESP32 writes it to RAM through the boot ROM and jumps to it (esp32/rom.cpp), from its drive (lilfm1.bin,
 * lilsmk.bin). It's then a USB-MIDI device ("Felucca Update", 1209:0002) that answers M-UPGRADE as "ota-FM-1_900", as the
 * loader does, and speaks the ESP32's commands (esp32/main.cpp), a line in each SysEx: F0 7D 4C 42 TEXT F7. It asks
 * "info" (for the ESP32's battery) and "ls" (the FM-1's firmwares and backups on its drive: the ESP32 says which), once a second until both are
 * answered; then, for the row picked, "install NAME" or "backup" (once a second, until the ESP32 sends the boot ROM's
 * key, F0 22 24 35 7D F7, and does it).
 *
 * Its header shows both batteries: the FM-1's (its ADC, in Felucca's bars) and the ESP32's. OCT- shows what the
 * FM-1's chip is (FLASH CHIP, CHIP KEY: the ESP32's info says what the chip said of itself the last time it was
 * written or read), the loader, the drive's room and the ESP32's version; the buttons that do something glow dimly
 * (fm1_input.h's LED layers; the menu polls its keys, so pk_glow lights them itself).
 *
 * The ESP32 then writes it through the boot ROM and its loader (loader/, m-bootloader's: it draws the flash's sectors
 * on this screen as it goes), as fm1buddy's recovery does: the flash driver here (fm1_flash.h) wants the flash as the
 * SPL leaves it, and the boot ROM doesn't. Nothing is written until one is picked: switching the FM-1 off and on keeps
 * what's on it. Any knob moves, any button picks (and again to be sure), a piano key jumps to its row. Runs from RAM
 * with interrupts off: USB is polled. Single compilation unit. */
#include <stdint.h>
#define FELUCCA_LOADER 1
#define FELUCCA_CDC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_USB_PID 0x0002
#if FOMA_SMK37
#define FELUCCA_ID "ota-SMK-37 Pro_900" /* (who it is, to the ESP32: an SMK-37, any of its names) */
#else
#define FELUCCA_ID "ota-FM-1_900"
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
/* as the loader: the app area and update records; never the head, nor Felucca's store */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x97000u) || FL_IN(off, n, 0xE0000u, 0xFC000u))
#include "fm1_flash.h"
static void ldr_poll(void);
#define FM1_INPUT_IDLE() ldr_poll()
#if FOMA_SMK37
#include "smk_input.h" /* the SMK-37's keys, buttons, knobs (and its battery) */
#else
#include "fm1_input.h"
#include "fm1_adc.h"
#endif
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
#define PK_MAX 24u  /* rows: backing up, then files */
#define PK_FILE 64u /* a file's name, whole */
#define PK_LINE 120u
#define PK_HEAD 40u  /* the header's height */
#define PK_TOP 46u   /* the list's top */
#define PK_ROW_H 28u /* a row */
#define PK_ROWS 6u   /* rows on screen */
#define PK_FOOT 218u /* the hint's top */

#if FOMA_SMK37
#define PK_KIND " smk"
#define PK_DEVICE "SMK-37"
#define PK_SEL_ENC 1u /* its K2: SELECT on the FM-1 (fomajam's fm1.c) */
#else
#define PK_KIND " fm1"
#define PK_DEVICE "FM-1"
#define PK_SEL_ENC 0u
#endif
#define PK_RED RGB(255, 72, 72)

static char pk_files[PK_MAX][PK_FILE + 1u]; /* row 0: "" (backing up) */
static uint32_t pk_n;                        /* rows */
static uint32_t pk_sel, pk_top;              /* the highlighted row, the first on screen */
static int32_t pk_batt_raw;                  /* the device's battery: smoothed ADC (0: not read yet) */
static int32_t pk_dev = -1, pk_lil = -1;     /* the batteries, 0..3 bars (-1: not known) */

/* what was asked of the ESP32, and is being answered */
enum { PK_ASK_INFO, PK_ASK_LS, PK_ASKED_ALL };
static uint32_t pk_asking;
static char pk_err[PK_LINE + 1u];

/* the board's info (the reply to "info"): its version, its drive's room, whether a loader is on it, and what the FM-1's
 * chip said of itself the last time it was written or backed up (0: not known yet) */
static char pk_ver[16];
static uint32_t pk_free;
static int pk_loader = -1; /* -1: not known */
static uint32_t pk_chip_key, pk_chip_flash, pk_chip_size;

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

/* "NAME BYTES KIND" from ls: a row, if it's for this device (KIND fm1 or smk: a firmware, or a backup) */
static void pk_file(const char *s)
{
  uint32_t n = str_len(s), spaces = 0;
  if (n < 4u || !str_eq(s + n - 4u, PK_KIND) || pk_n >= PK_MAX)
    return;
  for (n -= 4u; n && spaces < 1u; n--) /* (the name: up to the space before BYTES) */
    spaces += s[n - 1u] == ' ';
  if (!n || n > PK_FILE)
    return;
  memcpy(pk_files[pk_n], s, n);
  pk_files[pk_n][n] = 0;
  pk_n++;
}

/* a theme by its name (theme.txt on the ESP32's drive, in its info), fomajam's first if it has none of that name */
static void pk_theme(const char *s)
{
  int i, k;
  for (i = 0; i < THEME_COUNT; i++) {
    for (k = 0; THEMES[i].name[k] && (s[k] == THEMES[i].name[k] || s[k] == THEMES[i].name[k] + 32); k++)
      ;
    if (!THEMES[i].name[k] && (s[k] == ' ' || !s[k])) {
      th = &THEMES[i];
      return;
    }
  }
}

/* a number in base (10 or 16) from s; *end, after it (0: none) */
static uint32_t pk_num(const char *s, uint32_t base, const char **end)
{
  uint32_t v = 0;
  for (; *s; s++) {
    uint32_t d;
    if (*s >= '0' && *s <= '9')
      d = (uint32_t)(*s - '0');
    else if (base == 16 && *s >= 'A' && *s <= 'F')
      d = (uint32_t)(*s - 'A' + 10);
    else if (base == 16 && *s >= 'a' && *s <= 'f')
      d = (uint32_t)(*s - 'a' + 10);
    else
      break;
    v = v * base + d;
  }
  if (end)
    *end = s;
  return v;
}

/* v in digits of hex, 0x and all ("0x980F") */
static void pk_hex(char *out, uint32_t v, uint32_t digits)
{
  static const char H[] = "0123456789ABCDEF";
  uint32_t i;
  out[0] = '0', out[1] = 'x';
  for (i = 0; i < digits; i++)
    out[2u + i] = H[(v >> (4u * (digits - 1u - i))) & 0xFu];
  out[2u + digits] = 0;
}

/* a line from the ESP32 (after F0 7D 4C 42): the answer to what was asked */
static void pk_line(const uint8_t *p, uint32_t n)
{
  char s[PK_LINE + 1u];
  uint32_t i;
  const char *b, *v;
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
      if (!memcmp(b, "battery=", 8))
        pk_lil = b[8] >= '0' && b[8] <= '3' ? b[8] - '0' : -1;
      else if (!memcmp(b, "theme=", 6))
        pk_theme(b + 6);
      else if (!memcmp(b, "free=", 5))
        pk_free = pk_num(b + 5, 10, 0);
      else if (!memcmp(b, "loader=", 7))
        pk_loader = b[7] == '1';
      else if (!memcmp(b, "chip=", 5)) { /* key,flash,size: what its chip is (the board keeps it) */
        pk_chip_key = pk_num(b + 5, 16, &v);
        pk_chip_flash = pk_num(v + 1, 16, &v);
        pk_chip_size = pk_num(v + 1, 10, 0);
      } else if (!memcmp(b, "lilfm1buddy ", 12)) {
        uint32_t n;
        for (v = b + 12, n = 0; v[n] && v[n] != ' ' && n + 1u < sizeof pk_ver; n++)
          pk_ver[n] = v[n];
        pk_ver[n] = 0;
      }
  } else if (pk_asking == PK_ASK_LS)
    pk_file(s);
}

/* the device's battery, every second: Felucca's smoothing and bars (ui_draw.c) */
static int pk_batt_poll(uint32_t now)
{
  static uint32_t last;
  int32_t b, was = pk_dev;
  if (pk_batt_raw && now - last < 1000u)
    return 0;
  last = now;
  b = fm1_adc_read(FM1_ADC_BATT);
  if (b > 0)
    pk_batt_raw = pk_batt_raw ? pk_batt_raw + (b - pk_batt_raw) / 32 : b;
  if (pk_batt_raw)
    pk_dev = pk_batt_raw >= 591 ? 3 : pk_batt_raw >= 561 ? 2 : pk_batt_raw >= 531 ? 1 : 0;
  return pk_dev != was;
}

/* a battery at x, y in the strip (22 x 12): an outline, and its bars (red when empty) */
static void pk_cell(int32_t x, int32_t y, int32_t bars)
{
  uint16_t c = bars == 0 ? PK_RED : th->text;
  int32_t b;
  scr_rect(x, y, 20, 12, 2, c);
  scr_rect(x + 2, y + 2, 16, 8, 0, th->panel);
  scr_rect(x + 20, y + 3, 2, 6, 0, c);
  for (b = 0; b < bars; b++)
    scr_rect(x + 3 + b * 5, y + 3, 4, 6, 0, th->accent);
}

/* the header: the title, and both batteries (the device's, and lilfm1buddy's) */
static void pk_header(void)
{
  int32_t x = 228;
  scr_begin(240, PK_HEAD, th->panel);
  scr_text(12, 12, &FONT_S, "lilfm1buddy", th->accent, th->panel, 120);
  if (pk_lil >= 0) {
    x -= 22;
    pk_cell(x, 14, pk_lil);
    x -= scr_text_w(&FONT_S, "LIL") + 4;
    scr_text(x, 12, &FONT_S, "LIL", th->dim, th->panel, 40);
    x -= 10;
  }
  if (pk_dev >= 0) {
    x -= 22;
    pk_cell(x, 14, pk_dev);
    x -= scr_text_w(&FONT_S, PK_DEVICE) + 4;
    scr_text(x, 12, &FONT_S, PK_DEVICE, th->dim, th->panel, 60);
  }
  scr_blit(0, 0);
}

/* a row's name: the file's, without its extension */
static void pk_name(uint32_t r, char *out, uint32_t max)
{
  uint32_t n = 0, dot = 0;
  if (!r) {
    str_cpy(out, "Back up this " PK_DEVICE, max);
    return;
  }
  for (; pk_files[r][n]; n++)
    if (pk_files[r][n] == '.')
      dot = n;
  n = dot ? dot : n;
  if (n >= max)
    n = max - 1u;
  memcpy(out, pk_files[r], n);
  out[n] = 0;
}

/* row r of the list (on screen, or not drawn) */
static void pk_row(uint32_t r)
{
  char name[PK_FILE + 1u];
  uint32_t on = r == pk_sel;
  uint16_t fg = on ? th->text : r ? scr_mix(th->text, th->bg, 11) : th->accent, bg = on ? th->panel : th->bg;
  if (r < pk_top || r >= pk_top + PK_ROWS)
    return;
  scr_begin(232, PK_ROW_H, th->bg);
  if (on) {
    scr_rect(4, 1, 224, PK_ROW_H - 2u, 6, th->panel);
    scr_rect(4, 7, 3, PK_ROW_H - 14u, 1, th->accent);
  }
  pk_name(r, name, sizeof name);
  scr_text(16, 6, &FONT_S, name, r ? fg : th->accent, bg, 200);
  scr_blit(0, PK_TOP + (r - pk_top) * PK_ROW_H);
}

/* the scroll bar, at the right edge of the list */
static void pk_scroll(void)
{
  uint32_t h = PK_ROWS * PK_ROW_H, len = pk_n > PK_ROWS ? h * PK_ROWS / pk_n : h;
  uint32_t at = pk_n > PK_ROWS ? (h - len) * pk_top / (pk_n - PK_ROWS) : 0, y;
  for (y = 0; y < h; y += SCR_H) {
    scr_begin(4, h - y < SCR_H ? h - y : SCR_H, th->bg);
    if (pk_n > PK_ROWS) {
      scr_rect(1, 0, 2, (int32_t)scr_h, 0, th->panel);
      scr_rect(0, (int32_t)at - (int32_t)y, 4, (int32_t)len, 2, th->dim);
    }
    scr_blit(234, PK_TOP + y);
  }
}

static void pk_list(void)
{
  uint32_t r;
  if (pk_sel < pk_top)
    pk_top = pk_sel;
  if (pk_sel >= pk_top + PK_ROWS)
    pk_top = pk_sel - PK_ROWS + 1u;
  for (r = pk_top; r < pk_top + PK_ROWS; r++)
    if (r < pk_n)
      pk_row(r);
    else
      scr_fill(0, PK_TOP + (r - pk_top) * PK_ROW_H, 232, PK_ROW_H, th->bg);
  pk_scroll();
}

/* the hint at the bottom */
static void pk_hint(const char *s)
{
  scr_begin(240, 22, th->bg);
  scr_center(3, &FONT_S, s, th->dim, th->bg);
  scr_blit(0, PK_FOOT);
}

/* what there is to press, as chips at the bottom (the buttons' own LEDs glow: pk_glow); a chip a key, and what it
 * does, the first (the one to press to go on) in the accent */
static void pk_keys(const char *a, const char *b)
{
  int32_t aw = scr_text_w(&FONT_S, a) + 14, bw = b ? scr_text_w(&FONT_S, b) + 14 : 0;
  int32_t x = (240 - aw - bw - (b ? 8 : 0)) / 2;
  scr_begin(240, 22, th->bg);
  scr_rect(x, 2, aw, 18, 5, th->panel);
  scr_text(x + 7, 3, &FONT_S, a, th->accent, th->panel, aw - 8);
  if (b) {
    scr_rect(x + aw + 8, 2, bw, 18, 5, th->panel);
    scr_text(x + aw + 15, 3, &FONT_S, b, th->text, th->panel, bw - 8);
  }
  scr_blit(0, PK_FOOT);
}

/* a line of the info card: its name at the left, in the dim colour; its value at the right */
static void pk_info_row(uint32_t y, const char *name, const char *value, uint16_t vc)
{
  int32_t w = scr_text_w(&FONT_S, value);
  scr_begin(240, 20, th->bg);
  scr_text(12, 2, &FONT_S, name, th->dim, th->bg, 118);
  scr_text(228 - w, 2, &FONT_S, value, vc, th->bg, 220);
  scr_blit(0, y);
}

/* INFO (OCT-): what the FM-1's chip is, as M-Flasher's page says it, and what the board has */
static void pk_info(void)
{
  char v[28];
  scr_fill(0, PK_HEAD, 240, 240u - PK_HEAD, th->bg);
  pk_header();
  scr_begin(240, 26, th->bg);
  scr_center(0, &FONT_L, "INFO", th->accent, th->bg);
  scr_blit(0, 50);
  if (pk_chip_flash) { /* what it said of itself, the last time the board wrote or read it */
    pk_hex(v, pk_chip_flash, 6);
    if (pk_chip_size) {
      char m[8];
      fmt_int(m, (int32_t)(pk_chip_size / (1024u * 1024u)));
      str_cpy(v + str_len(v), " ", 2);
      str_cpy(v + str_len(v), m, sizeof v - str_len(v));
      str_cpy(v + str_len(v), "M", sizeof v - str_len(v));
    }
    pk_info_row(84, "FLASH CHIP", v, th->text);
  } else
    pk_info_row(84, "FLASH CHIP", "NOT YET", th->dim);
  if (pk_chip_key) {
    pk_hex(v, pk_chip_key, 4);
    pk_info_row(106, "CHIP KEY", v, th->text);
  } else
    pk_info_row(106, "CHIP KEY", "-", th->dim);
  pk_info_row(128, "BOOTLOADER", "WL82 ROM", th->text); /* (its chip's, in ROM: nothing can change it) */
  pk_info_row(150, "LOADER", pk_loader < 0 ? "-" : pk_loader ? "READY" : "MISSING",
              pk_loader == 0 ? PK_RED : th->text);
  {
    const char *unit = "B";
    uint32_t n = pk_free;
    if (n > 1024u * 1024u)
      n /= 1024u * 1024u, unit = "M";
    else if (n > 1024u)
      n /= 1024u, unit = "K";
    fmt_int(v, (int32_t)n);
    str_cpy(v + str_len(v), " ", 2);
    str_cpy(v + str_len(v), unit, 3);
    str_cpy(v + str_len(v), " FREE", 7);
    pk_info_row(172, "DRIVE", v, th->text);
  }
  pk_info_row(194, "VERSION", pk_ver[0] ? pk_ver : "-", th->text);
  pk_keys("OCT+ BACK", 0);
}

/* a whole screen: the header, a big title, two lines (0 for none; b in bc), and a hint */
static void pk_screen(const char *title, uint16_t tc, const char *a, const char *b, uint16_t bc, const char *hint)
{
  scr_fill(0, PK_HEAD, 240, 240u - PK_HEAD, th->bg);
  pk_header();
  scr_begin(240, 32, th->bg);
  scr_center(0, &FONT_L, title, tc, th->bg);
  scr_blit(0, 76);
  if (a) {
    scr_begin(240, 20, th->bg);
    scr_center(2, &FONT_S, a, th->text, th->bg);
    scr_blit(0, 124);
  }
  if (b) {
    scr_begin(240, 20, th->bg);
    scr_center(2, &FONT_S, b, bc, th->bg);
    scr_blit(0, 148);
  }
  if (hint)
    pk_hint(hint);
}

/* an error, a line or two of it */
static void pk_error(const char *e)
{
  char l1[32], l2[32];
  uint32_t n = str_len(e), cut = n;
  if (n > 28u) { /* (at the last space that fits) */
    for (cut = 28u; cut && e[cut] != ' '; cut--)
      ;
    if (!cut)
      cut = 28u;
  }
  str_cpy(l1, e, cut + 1u < sizeof l1 ? cut + 1u : sizeof l1);
  str_cpy(l2, e + cut + (e[cut] == ' '), sizeof l2);
  pk_screen("OOPS", PK_RED, l1, l2[0] ? l2 : 0, th->text, "OCT+: BACK");
}

static void pk_menu(void)
{
  scr_fill(0, PK_HEAD, 240, 240u - PK_HEAD, th->bg);
  pk_header();
  pk_list();
  pk_keys("OCT+ PICK", "OCT- INFO");
}

/* the SEL knob's steps, OCT+, OCT-, and anything else touched, since last time */
static void pk_input(int32_t *steps, uint32_t *ok, uint32_t *info, uint32_t *other)
{
  uint32_t e, b;
  fm1_input_scan();
  *steps = 0;
  *other = 0;
  for (e = 0; e < FM1_NENC; e++) {
    int32_t k = fm1_enc_take(e);
    if (e == PK_SEL_ENC)
      *steps = k;
    else
      *other |= k != 0;
  }
  b = fm1_input_edges(0);
  *ok = b >> FM1_BTN_OCT_UP & 1u;
  *info = b >> FM1_BTN_OCT_DOWN & 1u;
  *other |= (b & ~((1u << FM1_BTN_OCT_UP) | (1u << FM1_BTN_OCT_DOWN))) != 0 || fm1_input_note_edges() != 0;
}

/* the LEDs behind the keys in ids (a bit a key id: 0..13 the buttons, 14..40 the notes), dimly, once: the polled
 * scan lights only fm1_led (its glow layer rides the timer ISR, and this menu runs with interrupts off), so what can be
 * pressed shows on the buttons themselves, and the screen doesn't have to say it */
#if FOMA_SMK37
static void pk_glow(uint32_t ids) { (void)ids; } /* (the SMK-37's keys have no dim layer: its pads are the platform's) */
#else
#define PK_GLOW_US (FM1_GLOW_NS / 1000u + 1u)
static void pk_glow(uint32_t ids)
{
  uint32_t p, r;
  if (!ids)
    return;
  for (p = 0; p < FM1_NCOL; p++) {
    uint8_t bits = 0;
    for (r = 1; r < 5u; r++)
      if ((uint32_t)FM1_KEYMAP[r][p] < 41u && (ids >> (uint32_t)FM1_KEYMAP[r][p] & 1u))
        bits |= (uint8_t)(1u << r);
    if (!bits)
      continue;
    fm1__led_lines(0);
    fm1__sr_word(0xFFFFu ^ (1u << p) ^ (p < 2u ? 1u << (11u + p) : 0u));
    fm1__wait(FM1_SETTLE_US);
    fm1__led_lines(bits);
    fm1__wait(PK_GLOW_US);
    fm1__led_lines(0);
  }
}
#endif

/* the picked row, to the ESP32: install it, or back up (once a second, until the boot ROM's key comes) */
static void pk_do(void)
{
  if (pk_sel)
    pk_say("install ", pk_files[pk_sel]);
  else
    pk_say("backup", 0);
}

enum { PK_WAIT, PK_MENU, PK_SURE, PK_ASKED, PK_ERROR, PK_INFO };

static void pk_sure(void)
{
  char name[PK_FILE + 1u];
  pk_name(pk_sel, name, sizeof name);
  pk_screen(pk_sel ? "INSTALL?" : "BACK UP?", th->accent, pk_sel ? name : "All of its flash, to a file",
            pk_dev == 0 ? "Low battery: charge it first" : 0, PK_RED, 0);
  pk_keys("OCT+ YES", "OTHER NO");
}

static void pk_main(void)
{
  uint32_t state = PK_WAIT, said = 0, ok, info, other;
  int32_t shown_lil = -1, steps;
  const theme_t *shown_th = th;
  fm1_wdt_arm(0x0D); /* ~8 s, fed by ldr_poll */
  fm1_time_init();
  scr_init();
  fm1_input_init();
  fm1_adc_init();
  pk_screen("LILFM1BUDDY", th->accent, 0, "Asking for the list..", th->dim, 0);
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
      said = now; /* (it's answering: asked again only once it's quiet for a second) */
    }
    ota_service();   /* handshake: who it is */
    usb.ota_req = 0; /* (M-UPGRADE: not here) */
    if (th != shown_th) { /* (its theme came) */
      shown_th = th;
      scr_fill(0, 0, 240, 240, th->bg);
      pk_screen("LILFM1BUDDY", th->accent, 0, "Asking for the list..", th->dim, 0);
    }
    if (pk_err[0]) {
      pk_error(pk_err);
      pk_err[0] = 0;
      state = PK_ERROR;
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
      pk_header();
    }
    pk_input(&steps, &ok, &info, &other);
    /* the buttons' LEDs, dimly: what the screen takes (OCT+ is the one to go on with; OCT- has INFO, and the way
     * back from it) */
    pk_glow(state == PK_MENU || state == PK_SURE || state == PK_INFO
                ? (1u << FM1_BTN_OCT_UP) | (1u << FM1_BTN_OCT_DOWN)
                : state == PK_ERROR ? 1u << FM1_BTN_OCT_UP : 0);
    if (state == PK_MENU) {
      uint32_t was = pk_sel, top = pk_top;
      if (steps > 0)
        pk_sel = pk_sel + 1u >= pk_n ? pk_n - 1u : pk_sel + 1u;
      else if (steps < 0 && pk_sel)
        pk_sel--;
      if (pk_sel != was) {
        if (pk_sel < pk_top)
          pk_top = pk_sel;
        if (pk_sel >= pk_top + PK_ROWS)
          pk_top = pk_sel - PK_ROWS + 1u;
        if (pk_top != top)
          pk_list();
        else {
          pk_row(was);
          pk_row(pk_sel);
        }
      }
      if (info) { /* (what the chip is, and what the board has) */
        state = PK_INFO;
        pk_info();
      } else if (ok) {
        state = PK_SURE;
        pk_sure();
      }
    } else if (state == PK_INFO) {
      if (ok || info || other || steps) {
        state = PK_MENU;
        pk_menu();
      }
    } else if (state == PK_SURE) {
      if (ok) {
        char name[PK_FILE + 1u];
        state = PK_ASKED;
        said = now;
        pk_do();
        pk_name(pk_sel, name, sizeof name);
        pk_screen(pk_sel ? "INSTALLING" : "BACKING UP", th->accent, pk_sel ? name : 0, "Don't unplug it", th->dim, 0);
      } else if (steps || other) {
        state = PK_MENU;
        pk_menu();
      }
    } else if (state == PK_ERROR && (ok || steps)) {
      state = pk_asking == PK_ASKED_ALL ? PK_MENU : PK_WAIT;
      if (state == PK_MENU)
        pk_menu();
      else
        pk_screen("LILFM1BUDDY", th->accent, 0, "Asking for the list..", th->dim, 0);
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
