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
 * "info" (the board's battery, its theme, ...) and "ls" (the firmwares and backups on its drive: the ESP32 says which
 * device each is for), once a second until both are answered; then what the pages need: "about NAME" (a file's
 * details), "theme NAME" (the one picked), "rm NAME", and for what's picked, "install NAME" or "backup" (once a second,
 * until the ESP32 sends the boot ROM's key, F0 22 24 35 7D F7, and does it).
 *
 * Its pages (ui.c draws them), each on its own button, which glow dimly with the other buttons that do something
 * (fm1_input.h's LED layers; the menu polls its keys, so pk_glow lights them itself):
 *
 *   HOME  the firmwares on the drive: SEL picks one (a piano key jumps to its row), OCT+ installs it (and again to be
 *         sure), EDIT shows its details, REC deletes it
 *   SAVE  backing it up (the first row), and the backups: OCT+ restores one, EDIT and REC as on HOME
 *   GLO   the theme (OCT+, then SEL changes it as you look: OCT+ keeps it, anything else puts it back) and ABOUT: what
 *         the FM-1's chip is (FLASH CHIP, CHIP KEY: what it said of itself the last time it was written or read), the
 *         loader, the drive's room and the board's version
 *
 * On the SMK-37 they're its PATCH, BT and GLOBE buttons, and PARA for EDIT (fomajam's fm1.h). The ESP32 then writes it
 * through the boot ROM and its loader (loader/, m-bootloader's: it draws the flash's sectors on this screen as it
 * goes), as fm1buddy's recovery does: the flash driver here (fm1_flash.h) wants the flash as the SPL leaves it, and the
 * boot ROM doesn't. Nothing is written until one is picked: switching the FM-1 off and on keeps what's on it. Runs from
 * RAM with interrupts off: USB is polled. Single compilation unit. */
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
static int ota_erase(uint32_t off) {
  (void)off;
  return -1;
}
static int ota_prog(uint32_t off, const void* p, uint32_t n) {
  (void)off;
  (void)p;
  (void)n;
  return -1;
}
static int ota_fread(uint32_t off, void* p, uint32_t n) { return fl_read_ram(off, p, n); }
static void ota_show(uint32_t step, int32_t code) {
  (void)step;
  (void)code;
}
static void ota_commit(const uint8_t* parm) { (void)parm; }
#include "ota.c"

static void ldr_record_clear(void) { fm1_updata_parm_clear(); }
#include "screen.c"
#include "ui.c"

/* ---- the menu: what it asks the ESP32 ---- */
#if FOMA_SMK37
#define PK_SEL_ENC 1u /* its K2: SELECT on the FM-1 (fomajam's fm1.c) */
#define PK_B_HOME SMK_BTN_PATCH
#define PK_B_SAVE SMK_BTN_BT
#define PK_B_GLO SMK_BTN_GLOBE
#define PK_B_EDIT SMK_BTN_PARA
#define PK_B_REC SMK_BTN_SEQ_REC
#else
#define PK_SEL_ENC 0u
#define PK_B_HOME 8u /* the FM-1's buttons (fomajam's fm1.h: its hardware ids) */
#define PK_B_SAVE 9u
#define PK_B_GLO 7u
#define PK_B_EDIT 6u
#define PK_B_REC 13u
#endif
#define PK_B(b) (1u << (b))

static int32_t pk_batt_raw; /* the device's battery: smoothed ADC (0: not read yet) */

/* what's asked of the ESP32 (one thing at a time, asked again each second it doesn't answer), and its answer */
enum { RQ_NONE,
       RQ_INFO,
       RQ_LS,
       RQ_ABOUT,
       RQ_THEME,
       RQ_RM };
static uint32_t rq = RQ_INFO, rq_at, rq_tries;
static char rq_arg[PK_FILE + 1u];
static uint32_t pk_have; /* the list has come */

/* a line to the ESP32: F0 7D 4C 42 TEXT F7 */
static void pk_say(const char* a, const char* b) {
  uint8_t m[4u + PK_LINE + 1u] = {0xF0, 0x7D, 0x4C, 0x42};
  uint32_t n = 4;
  for (; *a && n < 4u + PK_LINE; a++)
    m[n++] = (uint8_t)*a;
  for (; b && *b && n < 4u + PK_LINE; b++)
    m[n++] = (uint8_t)*b;
  m[n++] = 0xF7;
  ota_wire_send(m, n);
}

static void rq_send(uint32_t now) {
  static const char* const CMD[] = {"", "info", "ls", "about ", "theme ", "rm "};
  rq_at = now;
  rq_tries++;
  if (rq == RQ_LS)
    pk_n = 0; /* (it all comes again) */
  pk_say(CMD[rq], rq >= RQ_ABOUT ? rq_arg : 0);
}

static void rq_start(uint32_t what, const char* arg, uint32_t now) {
  rq = what;
  rq_tries = 0;
  str_cpy(rq_arg, arg ? arg : "", sizeof rq_arg);
  if (usb.config)
    rq_send(now);
  else
    rq_at = now - 1000u;
}

/* a number in base (10 or 16) from s; *end, after it (0: none) */
static uint32_t pk_num(const char* s, uint32_t base, const char** end) {
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

/* a word of s (up to a space) into out */
static void pk_word(char* out, const char* s, uint32_t max) {
  uint32_t n = 0;
  for (; s[n] && s[n] != ' ' && n + 1u < max; n++)
    out[n] = s[n];
  out[n] = 0;
}

/* "NAME BYTES KIND" from ls: a file, if it's for this device (KIND fm1 or smk: a firmware, or a backup) */
static void pk_file(const char* s) {
  uint32_t n = str_len(s), b;
  if (n < 4u || !str_eq(s + n - 4u, PK_KIND) || pk_n >= PK_MAX)
    return;
  for (b = n - 4u; b && s[b - 1u] != ' '; b--) /* (BYTES: after the space before it) */
    ;
  if (b < 2u || b - 1u > PK_FILE)
    return;
  memcpy(pk_files[pk_n], s, b - 1u);
  pk_files[pk_n][b - 1u] = 0;
  pk_sizes[pk_n++] = pk_num(s + b, 10, 0);
}

/* a theme by its name (theme.txt on the ESP32's drive, in its info), fomajam's first if it has none of that name */
static void pk_theme(const char* s) {
  int i, k;
  for (i = 0; i < THEME_COUNT; i++) {
    for (k = 0; THEMES[i].name[k] && (s[k] == THEMES[i].name[k] || s[k] == THEMES[i].name[k] + 32); k++);
    if (!THEMES[i].name[k] && (s[k] == ' ' || !s[k])) {
      th = &THEMES[i];
      return;
    }
  }
}

/* what's asked is answered: what comes next */
static void pk_done(uint32_t now) {
  uint32_t was = rq;
  rq = RQ_NONE;
  if (was == RQ_INFO)
    rq_start(RQ_LS, 0, now);
  else if (was == RQ_LS) {
    pk_have = 1;
    if (pk_view == V_LIST) {
      pk_rows_make();
      pk_redraw();
    }
  } else if (was == RQ_ABOUT) {
    pk_ab_ok = 1;
    if (pk_view == V_DETAILS)
      pk_redraw();
  } else if (was == RQ_RM)
    rq_start(RQ_LS, 0, now); /* (the list again, without it) */
}

/* a line from the ESP32 (after F0 7D 4C 42): the answer to what was asked */
static void pk_line(const uint8_t* p, uint32_t n, uint32_t now) {
  char s[PK_LINE + 1u];
  uint32_t i;
  const char *b, *v;
  if (n > PK_LINE)
    n = PK_LINE;
  for (i = 0; i < n; i++)
    s[i] = (char)p[i];
  s[n] = 0;
  if (str_eq(s, "OK")) {
    if (rq)
      pk_done(now);
  } else if (s[0] == 'E' && s[1] == 'R' && s[2] == 'R') {
    str_cpy(pk_err, s + (n > 4u ? 4u : n), sizeof pk_err);
    if (rq != RQ_INFO && rq != RQ_LS)
      rq = RQ_NONE;
  } else if (rq == RQ_INFO) {
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
      } else if (!memcmp(b, "lilfm1buddy ", 12))
        pk_word(pk_ver, b + 12, sizeof pk_ver);
  } else if (rq == RQ_LS)
    pk_file(s);
  else if (rq == RQ_ABOUT) {
    for (b = s; *b; b++)
      if (!memcmp(b, "size=", 5))
        pk_ab_size = pk_num(b + 5, 10, 0);
      else if (!memcmp(b, "product=", 8))
        pk_word(pk_ab_product, b + 8, sizeof pk_ab_product);
      else if (!memcmp(b, "date=", 5))
        pk_word(pk_ab_date, b + 5, sizeof pk_ab_date);
  }
}

/* the device's battery, every second: Felucca's smoothing and bars (ui_draw.c) */
static int pk_batt_poll(uint32_t now) {
  static uint32_t last;
  int32_t b, was = pk_dev;
  if (pk_batt_raw && now - last < 1000u)
    return 0;
  last = now;
  b = fm1_adc_read(FM1_ADC_BATT);
  if (b > 0)
    pk_batt_raw = pk_batt_raw ? pk_batt_raw + (b - pk_batt_raw) / 32 : b;
  if (pk_batt_raw)
    pk_dev = pk_batt_raw >= 591 ? 3 : pk_batt_raw >= 561 ? 2
                                  : pk_batt_raw >= 531   ? 1
                                                         : 0;
  return pk_dev != was;
}

/* ---- the keys ---- */
typedef struct {
  int32_t steps;  /* the SEL knob's */
  uint32_t btn;   /* buttons pressed */
  uint32_t notes; /* piano keys pressed */
  uint32_t knobs; /* another knob turned */
} pk_keys_t;

static void pk_input(pk_keys_t* k) {
  uint32_t e;
  fm1_input_scan();
  k->steps = 0;
  k->knobs = 0;
  for (e = 0; e < FM1_NENC; e++) {
    int32_t t = fm1_enc_take(e);
    if (e == PK_SEL_ENC)
      k->steps = t;
    else
      k->knobs |= t != 0;
  }
  k->btn = fm1_input_edges(0);
  k->notes = fm1_input_note_edges();
}

/* the LEDs behind the keys in ids (a bit a key id: 0..13 the buttons, 14..40 the notes), dimly, once: the polled
 * scan lights only fm1_led (its glow layer rides the timer ISR, and this menu runs with interrupts off), so what can be
 * pressed shows on the buttons themselves */
#if FOMA_SMK37
static void pk_glow(uint32_t ids) { (void)ids; } /* (the SMK-37's keys have no dim layer: its pads are the platform's) */
#else
#define PK_GLOW_US (FM1_GLOW_NS / 1000u + 1u)
static void pk_glow(uint32_t ids) {
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

#define PK_PAGES (PK_B(PK_B_HOME) | PK_B(PK_B_SAVE) | PK_B(PK_B_GLO))
#define PK_OK PK_B(FM1_BTN_OCT_UP)
#define PK_BACK PK_B(FM1_BTN_OCT_DOWN)

/* what glows: the keys the screen takes */
static uint32_t pk_lit(void) {
  uint32_t file = pk_file_on() ? PK_B(PK_B_EDIT) | PK_B(PK_B_REC) : 0;
  if (pk_view == V_LIST)
    return pk_editing ? PK_OK | PK_BACK : PK_PAGES | (pk_nrows ? PK_OK : 0) | file;
  if (pk_view == V_DETAILS)
    return PK_PAGES | PK_OK | PK_BACK | PK_B(PK_B_REC);
  if (pk_view == V_ABOUT)
    return PK_PAGES | PK_BACK;
  return pk_view == V_SURE || pk_view == V_ERROR ? PK_OK : 0;
}

/* the picked row, to the ESP32: install it, or back up (once a second, until the boot ROM's key comes) */
static void pk_do(void) {
  if (pk_doing == DO_BACKUP)
    pk_say("backup", 0);
  else
    pk_say("install ", pk_file_on());
}

static void pk_to(uint32_t view) {
  pk_view = view;
  pk_rows_make();
  pk_redraw();
}

/* a page's button: that page */
static int pk_page_key(uint32_t btn) {
  uint32_t pg = btn & PK_B(PK_B_HOME) ? PG_FW : btn & PK_B(PK_B_SAVE) ? PG_SAVE
                                            : btn & PK_B(PK_B_GLO)    ? PG_SET
                                                                      : PG_COUNT;
  if (pg == PG_COUNT)
    return 0;
  pk_page = pg;
  pk_to(V_LIST);
  return 1;
}

/* the keys, on the list */
static void pk_on_list(const pk_keys_t* k, uint32_t now, const theme_t** was) {
  uint32_t row = pk_nrows ? pk_rows[pk_sel] : PK_MAX;
  if (pk_editing) { /* (the theme: SEL changes it, OCT+ keeps it, anything else puts it back) */
    int32_t i = (int32_t)(th - THEMES) + k->steps;
    if (k->steps)
      th = &THEMES[(i % THEME_COUNT + THEME_COUNT) % THEME_COUNT]; /* (pk_main draws it) */
    else if (k->btn & PK_OK) {
      pk_editing = 0;
      rq_start(RQ_THEME, th->name, now);
      pk_redraw();
    } else if (k->btn || k->knobs || k->notes) {
      pk_editing = 0;
      if (th == *was)
        pk_redraw();
      th = *was;
    }
    return;
  }
  if (rq == RQ_LS || pk_page_key(k->btn)) /* (the list is coming again: nothing to pick) */
    return;
  if (k->steps || k->notes) {
    uint32_t sel = pk_sel, b;
    if (k->steps > 0)
      sel = sel + (uint32_t)k->steps >= pk_nrows ? (pk_nrows ? pk_nrows - 1u : 0) : sel + (uint32_t)k->steps;
    else if (k->steps < 0)
      sel = (uint32_t)-k->steps > sel ? 0 : sel - (uint32_t)-k->steps;
    for (b = 0; b < 32u; b++) /* (a piano key: its row) */
      if (k->notes >> b & 1u) {
        if (b < pk_nrows)
          sel = b;
        break;
      }
    if (sel != pk_sel) {
      pk_sel = sel;
      pk_redraw();
    }
  }
  if (k->btn & PK_OK && pk_nrows) {
    if (row == PK_ROW_THEME) {
      *was = th;
      pk_editing = 1;
      pk_redraw();
    } else if (row == PK_ROW_ABOUT)
      pk_to(V_ABOUT);
    else {
      pk_doing = row == PK_ROW_BACKUP ? DO_BACKUP : DO_INSTALL;
      pk_to(V_SURE);
    }
  } else if (k->btn & PK_B(PK_B_EDIT) && row < PK_MAX) {
    pk_ab_ok = 0;
    pk_ab_product[0] = pk_ab_date[0] = 0;
    rq_start(RQ_ABOUT, pk_files[row], now);
    pk_to(V_DETAILS);
  } else if (k->btn & PK_B(PK_B_REC) && row < PK_MAX) {
    pk_doing = DO_DELETE;
    pk_to(V_SURE);
  }
}

static void pk_main(void) {
  uint32_t said = 0;
  int32_t shown_lil = -1;
  const theme_t *shown_th = th, *th_was = th;
  pk_keys_t k;
  fm1_wdt_arm(0x0D); /* ~8 s, fed by ldr_poll */
  fm1_time_init();
  scr_init();
  fm1_input_init();
  fm1_adc_init();
  pk_view = V_WAIT;
  pk_redraw();
  usb_start();
  for (;;) {
    const uint8_t* p;
    uint32_t n, now = ldr_ms();
    ldr_poll();
    usb_retry(now);
    if (usb.uboot_req) { /* soft key: mask-ROM UBOOT, as in Felucca */
      usb_detach();
      fm1_delay_ms(30);
      fm1_enter_uboot();
    }
    if (ota_frame_get(&p, &n) && n >= 4u && p[0] == 0x7D && p[1] == 0x4C && p[2] == 0x42 && p[3] >= 0x20u) {
      pk_line(p + 3, n - 3u, now);
      ota_frame_done();
      rq_at = said = now; /* (it's answering: asked again only once it's quiet for a second) */
    }
    ota_service();        /* handshake: who it is */
    usb.ota_req = 0;      /* (M-UPGRADE: not here) */
    if (th != shown_th) { /* (its theme came, or another was picked) */
      shown_th = th;
      pk_redraw();
    }
    if (pk_err[0] && pk_view != V_ERROR) {
      pk_editing = 0;
      pk_to(V_ERROR);
    }
    if (pk_view == V_WAIT && pk_have)
      pk_to(V_LIST);
    if (pk_view == V_BUSY) {
      if (usb.config && now - said >= 1000u) {
        said = now;
        pk_do();
      }
    } else if (rq && usb.config && now - rq_at >= 1000u) {
      if (rq_tries >= 5u && rq != RQ_INFO && rq != RQ_LS)
        rq = RQ_NONE; /* (it isn't answering that: never mind) */
      else
        rq_send(now);
    }
    if (pk_batt_poll(now) | (pk_lil != shown_lil)) {
      shown_lil = pk_lil;
      scr_draw(0, PK_TOP);
    }
    pk_input(&k);
    pk_glow(pk_lit());
    if (pk_view == V_LIST)
      pk_on_list(&k, now, &th_was);
    else if (pk_view == V_DETAILS) {
      if (k.btn & PK_OK) {
        pk_doing = DO_INSTALL;
        pk_to(V_SURE);
      } else if (k.btn & PK_B(PK_B_REC)) {
        pk_doing = DO_DELETE;
        pk_to(V_SURE);
      } else if (!pk_page_key(k.btn) && (k.btn || k.steps || k.knobs || k.notes))
        pk_to(V_LIST);
    } else if (pk_view == V_ABOUT) {
      if (!pk_page_key(k.btn) && (k.btn || k.steps || k.knobs || k.notes))
        pk_to(V_LIST);
    } else if (pk_view == V_SURE) {
      if (k.btn & PK_OK) {
        if (pk_doing == DO_DELETE) {
          rq_start(RQ_RM, pk_file_on(), now);
          pk_to(V_LIST);
        } else {
          said = now;
          pk_do();
          pk_to(V_BUSY);
        }
      } else if (k.btn || k.steps || k.knobs || k.notes)
        pk_to(V_LIST);
    } else if (pk_view == V_ERROR && (k.btn & PK_OK || k.steps)) {
      pk_err[0] = 0;
      pk_to(pk_have ? V_LIST : V_WAIT);
    }
  }
}

extern uint32_t _bss_start[], _bss_end[];

void ldr_cstart(void) /* crt0_ldr.S: interrupts off, our stacks */
{
  uint32_t* p;
  ldr_record_clear();
  for (p = _bss_start; p < _bss_end; p++)
    *p = 0;
  pk_main();
}
