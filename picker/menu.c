/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Konsumer */
/* The menu's workings: what it asks the ESP32 and makes of its answers, and what each key does on each screen (ui.c
 * draws them). Nothing here touches the hardware: its host gives it the keys (pk_tick), the ESP32's lines (pk_line),
 * sends its lines (pk_wire: a whole SysEx) and says whether USB is up (PK_LINKED): picker.c on the FM-1, and
 * tools/emu/emu.c in a browser. Part of their single unit, after ui.c. */

#if FOMA_SMK37
#define PK_SEL_ENC 1u /* its K2: SELECT on the FM-1 (fomajam's fm1.c) */
#define PK_B_HOME SMK_BTN_PATCH
#define PK_B_SAVE SMK_BTN_BT
#define PK_B_GLO SMK_BTN_GLOBE
#define PK_B_EDIT SMK_BTN_PARA
#define PK_B_REC SMK_BTN_SEQ_REC
#define PK_B_UP SMK_BTN_OCT_UP
#define PK_B_DOWN SMK_BTN_OCT_DOWN
#else
#define PK_SEL_ENC 0u
#define PK_B_HOME 8u /* the FM-1's buttons (fomajam's fm1.h: its hardware ids) */
#define PK_B_SAVE 9u
#define PK_B_GLO 7u
#define PK_B_EDIT 6u
#define PK_B_REC 13u
#define PK_B_UP 1u
#define PK_B_DOWN 0u
#endif
#define PK_B(b) (1u << (b))

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
  pk_wire(m, n);
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
  if (PK_LINKED())
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
  rq_at = now; /* (it's answering: asked again only once it's quiet for a second) */
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

/* ---- the keys ---- */
typedef struct {
  int32_t steps;  /* the SEL knob's */
  uint32_t btn;   /* buttons pressed */
  uint32_t notes; /* piano keys pressed */
  uint32_t knobs; /* another knob turned */
} pk_keys_t;

#define PK_PAGES (PK_B(PK_B_HOME) | PK_B(PK_B_SAVE) | PK_B(PK_B_GLO))
#define PK_OK PK_B(PK_B_UP)
#define PK_BACK PK_B(PK_B_DOWN)

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

/* the menu's start: its first screen */
static void pk_begin(void) {
  pk_view = V_WAIT;
  pk_redraw();
}

/* the menu, each time round (now: ms; k: the keys since last time) */
static void pk_tick(uint32_t now, const pk_keys_t* k) {
  static const theme_t *shown_th, *th_was;
  static int32_t shown_dev = -1, shown_lil = -1;
  static uint32_t said;
  if (!shown_th)
    shown_th = th_was = th;
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
    if (PK_LINKED() && now - said >= 1000u && now - rq_at >= 1000u) {
      said = now;
      pk_do();
    }
  } else if (rq && PK_LINKED() && now - rq_at >= 1000u) {
    if (rq_tries >= 5u && rq != RQ_INFO && rq != RQ_LS)
      rq = RQ_NONE; /* (it isn't answering that: never mind) */
    else
      rq_send(now);
  }
  if (pk_dev != shown_dev || pk_lil != shown_lil) {
    shown_dev = pk_dev;
    shown_lil = pk_lil;
    scr_draw(0, PK_TOP);
  }
  if (pk_view == V_LIST)
    pk_on_list(k, now, &th_was);
  else if (pk_view == V_DETAILS) {
    if (k->btn & PK_OK) {
      pk_doing = DO_INSTALL;
      pk_to(V_SURE);
    } else if (k->btn & PK_B(PK_B_REC)) {
      pk_doing = DO_DELETE;
      pk_to(V_SURE);
    } else if (!pk_page_key(k->btn) && (k->btn || k->steps || k->knobs || k->notes))
      pk_to(V_LIST);
  } else if (pk_view == V_ABOUT) {
    if (!pk_page_key(k->btn) && (k->btn || k->steps || k->knobs || k->notes))
      pk_to(V_LIST);
  } else if (pk_view == V_SURE) {
    if (k->btn & PK_OK) {
      if (pk_doing == DO_DELETE) {
        rq_start(RQ_RM, pk_file_on(), now);
        pk_to(V_LIST);
      } else {
        said = now;
        pk_do();
        pk_to(V_BUSY);
      }
    } else if (k->btn || k->steps || k->knobs || k->notes)
      pk_to(V_LIST);
  } else if (pk_view == V_ERROR && (k->btn & PK_OK || k->steps)) {
    pk_err[0] = 0;
    pk_to(pk_have ? V_LIST : V_WAIT);
  }
}
