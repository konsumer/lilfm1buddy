/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Konsumer */
/* The menu in a browser (the web page's "Try it"): picker/'s own screens and workings (screen.c, ui.c, menu.c), built
 * for WebAssembly with no C library (fomajam's libc.c has what they use), and a pretend board that answers it as the
 * ESP32 does (esp32/main.cpp's commands), with made-up files on its drive. The page (site/emu.js) gives it the keys
 * and shows its screen. tools/emu.sh builds it. */
#include <stdint.h>

#include "libc.c"
#include "fm1_lcd_hw.h" /* (tools/preview's: the screen kept in lcd_fb) */
#define PK_LINKED() 1
static void pk_wire(const uint8_t* m, uint32_t n);
#include "screen.c"
#include "ui.c"
#include "menu.c"

/* ---- the pretend board ---- */
typedef struct {
  char name[PK_FILE + 1u];
  uint32_t size;
} emu_file_t;
static emu_file_t files[PK_MAX];
static uint32_t nfiles, backups;
static char theme[16];

/* what it says, a line at a time (one every 30 ms, as the real one does) */
#define OUT_N 64u
static char out[OUT_N][PK_LINE + 1u];
static uint32_t out_head, out_tail, out_at, now_ms;

/* a job (installing, backing up): the loader's screen for a while, then the FM-1 runs its firmware */
enum { JOB_NONE,
       JOB_INSTALL,
       JOB_BACKUP,
       JOB_RUNNING };
static uint32_t job, job_at;
static char job_file[PK_FILE + 1u];

static void say(const char* a, const char* b, const char* c) {
  char* s = out[out_head % OUT_N];
  str_cpy(s, a, PK_LINE + 1u);
  if (b)
    str_cpy(s + str_len(s), b, PK_LINE + 1u - str_len(s));
  if (c)
    str_cpy(s + str_len(s), c, PK_LINE + 1u - str_len(s));
  out_head++;
}

static void add(const char* name, uint32_t size) {
  if (nfiles < PK_MAX) {
    str_cpy(files[nfiles].name, name, PK_FILE + 1u);
    files[nfiles++].size = size;
  }
}

static int find(const char* name) {
  uint32_t i;
  for (i = 0; i < nfiles; i++)
    if (str_eq(files[i].name, name))
      return (int)i;
  return -1;
}

/* a command from the menu (F0 7D 4C 42 TEXT F7), answered as esp32/main.cpp's command() does */
static void pk_wire(const uint8_t* m, uint32_t n) {
  char s[PK_LINE + 1u], num[12];
  const char* arg;
  uint32_t i, k = 0;
  for (i = 4; i + 1u < n && k < PK_LINE; i++)
    s[k++] = (char)m[i];
  s[k] = 0;
  for (arg = s; *arg && *arg != ' '; arg++);
  if (*arg)
    arg++;
  if (!memcmp(s, "info", 4)) {
    char line[PK_LINE + 1u];
    uint32_t used = 0;
    for (i = 0; i < nfiles; i++)
      used += files[i].size;
    fmt_int(num, (int32_t)(3276800u - used));
    str_cpy(line, "lilfm1buddy 0.5.0 free=", sizeof line);
    str_cpy(line + str_len(line), num, sizeof line - str_len(line));
    say(line, " loader=1 battery=2 theme=", theme[0] ? theme : "BLACK");
    out_head--; /* (and the chip, on the same line) */
    str_cpy(out[out_head % OUT_N] + str_len(out[out_head % OUT_N]), " chip=980F,0B4014,1048576", PK_LINE);
    out_head++;
  } else if (!memcmp(s, "ls", 2)) {
    for (i = 0; i < nfiles; i++) {
      fmt_int(num, (int32_t)files[i].size);
      say(files[i].name, " ", num);
      out_head--;
      str_cpy(out[out_head % OUT_N] + str_len(out[out_head % OUT_N]), " fm1", PK_LINE);
      out_head++;
    }
  } else if (!memcmp(s, "about ", 6)) {
    int f = find(arg);
    if (f < 0) {
      say("ERR No such file.", 0, 0);
      return;
    }
    fmt_int(num, (int32_t)files[f].size);
    say("size=", num, pk_ends(arg, ".fwsc") ? " kind=fm1 product=FM-1_974 date=2026-10-30" : " kind=fm1 date=2026-11-18");
  } else if (!memcmp(s, "theme ", 6))
    str_cpy(theme, arg, sizeof theme);
  else if (!memcmp(s, "rm ", 3)) {
    int f = find(arg);
    if (f < 0) {
      say("ERR No such file.", 0, 0);
      return;
    }
    files[f] = files[--nfiles];
  } else if (!memcmp(s, "install ", 8) || !memcmp(s, "backup", 6)) {
    if (job)
      return; /* (asked again, as the menu does each second: it's started) */
    job = s[0] == 'i' ? JOB_INSTALL : JOB_BACKUP;
    job_at = now_ms;
    str_cpy(job_file, arg, sizeof job_file);
  } else {
    say("ERR What's that?", 0, 0);
    return;
  }
  say("OK", 0, 0);
}

/* the loader's screen (third_party/fm1buddy/loader): what it's doing and how far, and the flash's sectors */
static void loader_screen(uint32_t t) {
  uint32_t pct = t >= 4000u ? 100u : t / 40u, y, i;
  char line[24], num[6];
  str_cpy(line, job == JOB_BACKUP ? "BACKING UP " : "INSTALLING ", sizeof line);
  fmt_int(num, (int32_t)pct);
  str_cpy(line + str_len(line), num, sizeof line - str_len(line));
  str_cpy(line + str_len(line), "%", sizeof line - str_len(line));
  for (y = 0; y < 240u; y += SCR_H) {
    scr_begin(0, (int32_t)y, 240, SCR_H, 0);
    scr_text(8, 4, &FONT_S, line, 0xFFFF, 232);
    scr_text(8, 22, &FONT_S, job == JOB_BACKUP ? ". NOT READ + READ" : ". SAME  + WRITE", 0x8410, 232);
    for (i = 0; i < 256u; i++) { /* (16 x 16 sectors of 4 KiB) */
      uint32_t done = i * 100u / 256u < pct, x = 24u + (i % 16u) * 12u, sy = 46u + (i / 16u) * 12u;
      uint16_t c = !done ? 0x2104 : job == JOB_BACKUP ? 0x07E0
                                : (i * 7u) % 5u       ? 0x4208
                                                      : 0xF81F;
      scr_rect((int32_t)x, (int32_t)sy, 10, 10, 0, c);
    }
    scr_blit();
  }
}

/* the FM-1 running its firmware: nothing of ours on its screen */
static void running_screen(void) {
  uint32_t y;
  for (y = 0; y < 240u; y += SCR_H) {
    scr_begin(0, (int32_t)y, 240, SCR_H, 0);
    scr_text(40, 100, &FONT_S, "(its firmware runs)", 0x8410, 200);
    scr_text(28, 124, &FONT_S, "BOOT: the menu again", 0x8410, 200);
    scr_blit();
  }
}

/* the menu from the start, as when the board sends it again */
static void menu_start(void) {
  pk_n = pk_have = pk_editing = 0;
  pk_page = PG_FW;
  pk_sel_on[PG_FW] = pk_sel_on[PG_SAVE] = pk_sel_on[PG_SET] = 0;
  pk_err[0] = 0;
  rq = RQ_INFO;
  rq_at = now_ms - 1000u;
  rq_tries = 0;
  out_tail = out_head;
  job = JOB_NONE;
  pk_begin();
}

/* ---- for the page ---- */
void emu_init(void) {
  add("FM-1_016.fwsc", 718196);
  add("fomajam-0.1.7.fwsc", 610110);
  add("hortator-0.23.2-bluetooth.fwsc", 609649);
  add("felucca-1.6.1.fwsc", 610090);
  add("sloop-2.8.fwsc", 610035);
  add("melodee-1.4.5.fwsc", 610545);
  add("x0x-1.1.0.fwsc", 610032);
  add("baudgirl-FM-1+VA-102.fwsc", 859700);
  add("fm1-1.bin", 1048576);
  backups = 1;
  pk_dev = 2;
  scr_init();
  menu_start();
}

/* BOOT on the board: the menu again */
void emu_boot(void) { menu_start(); }

/* the screen: 240 x 240, RGB565 */
uint16_t* emu_fb(void) { return lcd_fb; }

/* a frame: the time (ms), and the keys since the last (SEL's steps, the buttons pressed and piano keys pressed, as
 * bits of their ids, another knob turned); returns the buttons that glow */
uint32_t emu_frame(uint32_t now, int32_t steps, uint32_t btn, uint32_t notes, uint32_t knobs) {
  pk_keys_t k;
  now_ms = now;
  if (job == JOB_RUNNING)
    return 0;
  if (job && now - job_at >= 300u) { /* (the board has it in its boot ROM, with the loader) */
    uint32_t t = now - job_at - 300u;
    loader_screen(t);
    if (t >= 4600u) {
      if (job == JOB_BACKUP) {
        char name[16], num[6];
        fmt_int(num, (int32_t)++backups);
        str_cpy(name, "fm1-", sizeof name);
        str_cpy(name + 4, num, sizeof name - 4u);
        str_cpy(name + str_len(name), ".bin", sizeof name - str_len(name));
        add(name, 1048576);
      }
      job = JOB_RUNNING;
      running_screen();
    }
    return 0;
  }
  if (out_tail != out_head && now - out_at >= 30u) {
    const char* s = out[out_tail++ % OUT_N];
    pk_line((const uint8_t*)s, str_len(s), now);
    out_at = now;
  }
  k.steps = steps;
  k.btn = btn;
  k.notes = notes;
  k.knobs = knobs;
  pk_tick(now, &k);
  return pk_lit();
}
