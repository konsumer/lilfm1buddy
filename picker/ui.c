/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Konsumer */
/* The menu's screens: what it knows, and how each looks. Nothing here touches the hardware but the screen
 * (screen.c), so tools/preview.c draws them on a computer too. Part of picker.c's single unit.
 *
 * The look: slanted panels in the theme's accent (fomajam's themes: theme.txt, or the settings page), big italic
 * capitals (FONT_L), and the row that's picked thrown forward on its own panel with a shadow behind it. Each screen
 * is, top to bottom:
 *
 *   the pages (HOME, SAVE, GLO: the buttons that open them), and where the row picked is in the list
 *   the page's name on a slanted panel; both batteries at the right (the FM-1's or SMK-37's, and lilfm1buddy's)
 *   the page: a list, a question, or details
 *   the keys that do something here, and what they do (the buttons themselves glow too: picker.c's pk_glow) */

/* ---- what it knows ---- */
#define PK_MAX 48u  /* files */
#define PK_FILE 64u /* a file's name, whole */
#define PK_LINE 120u

#if FOMA_SMK37
#define PK_KIND " smk"
#define PK_DEVICE "SMK-37"
#define PK_HOME "PATCH" /* (the SMK-37's buttons where the FM-1's are: fomajam's fm1.h) */
#define PK_SAVE "BT"
#define PK_GLO "GLOBE"
#define PK_EDIT "PARA"
#define PK_REC "REC"
#define PK_OCT_UP "OCT+"
#define PK_OCT_DOWN "OCT-"
#else
#define PK_KIND " fm1"
#define PK_DEVICE "FM-1"
#define PK_HOME "HOME"
#define PK_SAVE "SAVE"
#define PK_GLO "GLO"
#define PK_EDIT "EDIT"
#define PK_REC "REC"
#define PK_OCT_UP "OCT+"
#define PK_OCT_DOWN "OCT-"
#endif
#define PK_RED RGB(255, 72, 72)

static char pk_files[PK_MAX][PK_FILE + 1u]; /* this device's firmwares and backups on the drive */
static uint32_t pk_sizes[PK_MAX];
static uint32_t pk_n;
static int32_t pk_dev = -1, pk_lil = -1; /* the batteries, 0..3 bars (-1: not known) */

/* the board's info (the reply to "info"): its version, its drive's room, whether a loader is on it, and what the FM-1's
 * chip said of itself the last time it was written or backed up (0: not known yet) */
static char pk_ver[16];
static uint32_t pk_free;
static int pk_loader = -1; /* -1: not known */
static uint32_t pk_chip_key, pk_chip_flash, pk_chip_size;

/* a file's details (the reply to "about") */
static char pk_ab_product[24], pk_ab_date[12];
static uint32_t pk_ab_size, pk_ab_ok;

/* the pages, and the rows on each: a file (its index), or one of these */
enum { PG_FW,
       PG_SAVE,
       PG_SET,
       PG_COUNT };
#define PK_ROW_BACKUP 0x100u
#define PK_ROW_THEME 0x101u
#define PK_ROW_ABOUT 0x102u
static uint32_t pk_page;
static uint32_t pk_rows[PK_MAX + 2u], pk_nrows;
static uint32_t pk_sel_on[PG_COUNT]; /* the row picked on each page */
#define pk_sel pk_sel_on[pk_page]

/* what's on the screen */
enum { V_WAIT,
       V_LIST,
       V_SURE,
       V_BUSY,
       V_ERROR,
       V_DETAILS,
       V_ABOUT };
enum { DO_INSTALL,
       DO_BACKUP,
       DO_DELETE };
static uint32_t pk_view, pk_doing; /* (V_SURE, V_BUSY: which) */
static uint32_t pk_editing;        /* the theme's row: turning SEL changes it */
static char pk_err[PK_LINE + 1u];

static int pk_ends(const char* s, const char* e) {
  uint32_t n = str_len(s), k = str_len(e);
  return n > k && str_eq(s + n - k, e);
}

/* the rows of the page that's up */
static void pk_rows_make(void) {
  uint32_t i;
  pk_nrows = 0;
  if (pk_page == PG_SAVE)
    pk_rows[pk_nrows++] = PK_ROW_BACKUP;
  if (pk_page == PG_SET) {
    pk_rows[pk_nrows++] = PK_ROW_THEME;
    pk_rows[pk_nrows++] = PK_ROW_ABOUT;
  }
  for (i = 0; i < pk_n && pk_page != PG_SET; i++)
    if (pk_ends(pk_files[i], pk_page == PG_FW ? ".fwsc" : ".bin"))
      pk_rows[pk_nrows++] = i;
  if (pk_sel >= pk_nrows)
    pk_sel = pk_nrows ? pk_nrows - 1u : 0;
}

/* the file of the row picked, or 0 */
static const char* pk_file_on(void) {
  return pk_nrows && pk_rows[pk_sel] < PK_MAX ? pk_files[pk_rows[pk_sel]] : 0;
}

/* a file's name as a row shows it: its name (big) and its version (small). "hortator-0.23.2-bluetooth.fwsc": HORTATOR,
 * 0.23.2-bluetooth (at the first - or _ before a digit); "fm1-3.bin": BACKUP 3 */
static void pk_split(const char* file, char* big, char* small, uint32_t max) {
  uint32_t n = 0, dot = 0, cut;
  for (; file[n]; n++)
    if (file[n] == '.')
      dot = n;
  n = dot ? dot : n;
  for (cut = 1; cut + 1u < n && !((file[cut] == '-' || file[cut] == '_') && file[cut + 1] >= '0' && file[cut + 1] <= '9');
       cut++);
  if (cut + 1u >= n)
    cut = n;
  if (pk_ends(file, ".bin") && cut == 3u && (!memcmp(file, "fm1", 3) || !memcmp(file, "smk", 3))) {
    str_cpy(big, "BACKUP ", max);
    str_cpy(big + 7, file + 4, n - 4u + 1u < max - 7u ? n - 4u + 1u : max - 7u);
    small[0] = 0;
    return;
  }
  str_cpy(big, file, cut + 1u < max ? cut + 1u : max);
  str_cpy(small, cut < n ? file + cut + 1u : "", n - cut < max ? n - cut : max);
}

/* bytes as "595 KB" */
static void pk_size(char* out, uint32_t n) {
  const char* unit = " B";
  if (n >= 1024u * 1024u)
    n = (n + 512u * 1024u) / (1024u * 1024u), unit = " MB";
  else if (n >= 1024u)
    n = (n + 512u) / 1024u, unit = " KB";
  fmt_int(out, (int32_t)n);
  str_cpy(out + str_len(out), unit, 4);
}

/* v in digits of hex, 0x and all ("0x980F") */
static void pk_hex(char* out, uint32_t v, uint32_t digits) {
  static const char H[] = "0123456789ABCDEF";
  uint32_t i;
  out[0] = '0', out[1] = 'x';
  for (i = 0; i < digits; i++)
    out[2u + i] = H[(v >> (4u * (digits - 1u - i))) & 0xFu];
  out[2u + digits] = 0;
}

/* ---- the parts of a screen ---- */
#define PK_LEAN 5 /* the panels' slant: eighths of a pixel a row */
#define PK_ITAL 3 /* the text's */
#define PK_TOP 58 /* the page's top */
#define PK_FOOT 214

/* a battery at x, y (22 x 12): an outline, and its bars (red when empty) */
static void pk_cell(int32_t x, int32_t y, int32_t bars) {
  uint16_t c = bars == 0 ? PK_RED : th->text;
  int32_t b;
  scr_rect(x, y, 20, 12, 2, c);
  scr_rect(x + 2, y + 2, 16, 8, 0, th->bg);
  scr_rect(x + 20, y + 3, 2, 6, 0, c);
  for (b = 0; b < bars; b++)
    scr_rect(x + 3 + b * 5, y + 3, 4, 6, 0, bars == 0 ? PK_RED : th->accent);
}

/* a key's name on a small slanted chip, and what it does after it: returns where it ends */
static int32_t pk_key(int32_t x, int32_t y, const char* key, const char* what, uint16_t chip, uint16_t on) {
  int32_t w = scr_text_w(&FONT_S, key) + 10;
  scr_slant(x, y, w, 18, PK_LEAN, chip);
  scr_ital = PK_ITAL;
  scr_text(x + 6, y + 1, &FONT_S, key, on, w);
  if (what)
    x = scr_text(x + w + 5, y + 1, &FONT_S, what, th->text, 240) - w - 5;
  scr_ital = 0;
  return x + w + 5;
}

/* the pages, and where the row picked is (03/17) */
static void pk_tabs(int counter) {
  static const char* const NAMES[PG_COUNT] = {PK_HOME, PK_SAVE, PK_GLO};
  int32_t x = 4, i;
  for (i = 0; i < PG_COUNT; i++)
    x = pk_key(x, 2, NAMES[i], 0, (uint32_t)i == pk_page ? th->accent : th->panel,
               (uint32_t)i == pk_page ? th->bg : th->dim) +
        1;
  if (counter && pk_nrows) {
    char s[12], b[6];
    s[0] = 0;
    fmt_int(b, (int32_t)pk_sel + 1);
    if (pk_sel + 1u < 10u)
      str_cpy(s, "0", 2);
    str_cpy(s + str_len(s), b, 6);
    str_cpy(s + str_len(s), "/", 2);
    fmt_int(b, (int32_t)pk_nrows);
    if (pk_nrows < 10u)
      str_cpy(s + str_len(s), "0", 2);
    str_cpy(s + str_len(s), b, 6);
    scr_ital = PK_ITAL;
    scr_text(236 - scr_text_w(&FONT_S, s), 3, &FONT_S, s, th->dim, 80);
    scr_ital = 0;
  }
}

/* the page's name, on its panel (a white one under it, a little lower: its shadow), and the batteries */
static void pk_title(const char* name, uint16_t c) {
  int32_t w = scr_text_w(&FONT_L, name) + 34;
  if (w > 178)
    w = 178;
  scr_slant(-10, 26, w + 4, 28, PK_LEAN, th->text);
  scr_slant(-10, 22, w, 30, PK_LEAN, c);
  scr_ital = PK_ITAL;
  scr_bold(10, 20, &FONT_L, name, th->bg, w - 18);
  scr_ital = 0;
  if (pk_dev >= 0) {
    scr_text(212 - scr_text_w(&FONT_S, PK_DEVICE), 22, &FONT_S, PK_DEVICE, th->dim, 60);
    pk_cell(216, 24, pk_dev);
  }
  if (pk_lil >= 0) {
    scr_text(212 - scr_text_w(&FONT_S, "LIL"), 38, &FONT_S, "LIL", th->dim, 60);
    pk_cell(216, 40, pk_lil);
  }
}

/* the keys at the bottom: up to three, each its key and what it does */
static void pk_foot(const char* k1, const char* w1, const char* k2, const char* w2, const char* k3, const char* w3) {
  int32_t x = 6;
  scr_rect(0, PK_FOOT - 4, 240, 1, 0, th->panel);
  if (k1)
    x = pk_key(x, PK_FOOT + 2, k1, w1, th->accent, th->bg) + 6;
  if (k2)
    x = pk_key(x, PK_FOOT + 2, k2, w2, th->panel, th->text) + 6;
  if (k3)
    pk_key(x, PK_FOOT + 2, k3, w3, th->panel, th->text);
}

/* a row's two lines: what it is (big), and more about it (small) */
static void pk_row_text(uint32_t r, char* big, char* small, uint32_t max) {
  uint32_t row = pk_rows[r];
  if (row == PK_ROW_BACKUP) {
    str_cpy(big, "BACK UP NOW", max);
    str_cpy(small, "the whole flash, to a file", max);
  } else if (row == PK_ROW_THEME) {
    str_cpy(big, "THEME", max);
    str_cpy(small, th->name, max);
  } else if (row == PK_ROW_ABOUT) {
    str_cpy(big, "ABOUT", max);
    str_cpy(small, "the board, and the chip", max);
  } else {
    pk_split(pk_files[row], big, small, max);
    if (!small[0] || pk_page == PG_SAVE) { /* (a backup: its size instead) */
      if (small[0])
        str_cpy(small + str_len(small), "  ", max - str_len(small));
      pk_size(small + str_len(small), pk_sizes[row]);
    }
  }
}

/* the list: the row picked thrown forward on its panel, two lines; the others a line each */
#define PK_ROW_H 30
#define PK_PICK_H 54
#define PK_PICK_GAP 4 /* (its shadow's) */
static void pk_list(void) {
  char big[PK_FILE + 1u], small[PK_FILE + 1u];
  uint32_t top = pk_sel ? pk_sel - 1u : 0, r;
  int32_t y = PK_TOP;
  if (top + 4u > pk_nrows)
    top = pk_nrows > 4u ? pk_nrows - 4u : 0;
  /* behind it, a slab leaning across (the theme's panel, faintly) */
  scr_slant(150, PK_TOP - 4, 120, PK_FOOT - PK_TOP - 2, 9, scr_mix(th->panel, th->bg, 9));
  if (!pk_nrows) {
    scr_ital = PK_ITAL;
    scr_bold(18, 90, &FONT_L, "NOTHING HERE", th->dim, 220);
    scr_ital = 0;
    scr_text(20, 130, &FONT_S, pk_page == PG_FW ? "Put .fwsc files on its drive:" : "", th->text, 220);
    scr_text(20, 148, &FONT_S, pk_page == PG_FW ? "the web page's Drive tab." : "", th->text, 220);
    return;
  }
  for (r = top; r < pk_nrows && r < top + 4u; r++) {
    pk_row_text(r, big, small, sizeof big);
    scr_ital = PK_ITAL;
    if (r == pk_sel) {
      scr_slant(10, y + 6, 228, PK_PICK_H - 6, PK_LEAN, th->text); /* (its shadow) */
      scr_slant(2, y + 2, 228, PK_PICK_H - 6, PK_LEAN, th->accent);
      scr_bold(18, y, &FONT_L, big, th->bg, 206);
      if (pk_editing && pk_rows[r] == PK_ROW_THEME) {
        str_cpy(small, "< ", sizeof small);
        str_cpy(small + 2, th->name, sizeof small - 2u);
        str_cpy(small + str_len(small), " >  turn SEL", sizeof small - str_len(small));
      }
      scr_text(22, y + 30, &FONT_S, small, th->bg, 200);
      y += PK_PICK_H + PK_PICK_GAP;
    } else {
      int32_t end = scr_text(16, y - 4, &FONT_L, big, th->text, 214);
      if (small[0] && pk_rows[r] < PK_MAX && end + 8 + scr_text_w(&FONT_S, small) < 232) /* (only if it fits) */
        scr_text(end + 8, y + 9, &FONT_S, small, th->dim, 232 - end - 8);
      y += PK_ROW_H;
    }
    scr_ital = 0;
  }
}

/* a line of details: its name at the left, small and dim; its value at the right */
static void pk_info_row(int32_t y, const char* name, const char* value, uint16_t vc) {
  int32_t room = 228 - 14 - scr_text_w(&FONT_S, name) - 10, w = scr_text_w(&FONT_S, value);
  scr_ital = PK_ITAL;
  scr_text(14, y, &FONT_S, name, th->dim, 110);
  scr_text(228 - (w < room ? w : room), y, &FONT_S, value, vc, room);
  scr_ital = 0;
  scr_rect(14, y + 18, 214, 1, 0, th->panel);
}

/* a big line, centered, slanted */
static void pk_big(int32_t y, const char* s, uint16_t c) {
  int32_t w = scr_text_w(&FONT_L, s);
  scr_ital = PK_ITAL;
  scr_bold(w < 232 ? (240 - w) / 2 : 4, y, &FONT_L, s, c, 232);
  scr_ital = 0;
}

static void pk_center(int32_t y, const char* s, uint16_t c) {
  int32_t w = scr_text_w(&FONT_S, s);
  scr_text(w < 236 ? (240 - w) / 2 : 2, y, &FONT_S, s, c, 236);
}

/* an error, wrapped to lines of 28 at most */
static void pk_wrap(int32_t y, const char* e, uint16_t c) {
  char l[32];
  uint32_t n, cut;
  while (*e && y < PK_FOOT - 20) {
    n = str_len(e);
    cut = n;
    if (n > 28u) { /* (at the last space that fits) */
      for (cut = 28u; cut && e[cut] != ' '; cut--);
      if (!cut)
        cut = 28u;
    }
    str_cpy(l, e, cut + 1u < sizeof l ? cut + 1u : sizeof l);
    pk_center(y, l, c);
    e += cut + (e[cut] == ' ');
    y += 20;
  }
}

/* ---- the screens ---- */
static void pk_paint_list(void) {
  const char* f = pk_file_on();
  static const char* const TITLES[PG_COUNT] = {"FIRMWARE", "BACKUPS", "SETTINGS"};
  pk_tabs(1);
  pk_title(TITLES[pk_page], th->accent);
  pk_list();
  if (pk_page == PG_SET)
    pk_foot(PK_OCT_UP, pk_editing ? "keep" : pk_rows[pk_sel] == PK_ROW_THEME ? "change"
                                                                             : "open",
            pk_editing ? "other" : 0,
            "undo", 0, 0);
  else if (!f)
    pk_foot(pk_nrows ? PK_OCT_UP : 0, "back up", 0, 0, 0, 0);
  else
    pk_foot(PK_OCT_UP, pk_page == PG_FW ? "install" : "restore", PK_EDIT, "details", 0, 0);
}

static void pk_paint_sure(void) {
  char big[PK_FILE + 1u], small[PK_FILE + 1u];
  static const char* const ASK[] = {"INSTALL?", "BACK UP?", "DELETE?"};
  const char* ask = pk_doing == DO_INSTALL && pk_page == PG_SAVE ? "RESTORE?" : ASK[pk_doing];
  pk_tabs(0);
  pk_title(ask, pk_doing == DO_DELETE ? PK_RED : th->accent);
  if (pk_doing == DO_BACKUP) {
    pk_big(84, "ALL OF ITS", th->text);
    pk_big(116, "FLASH", th->text);
    pk_center(156, "to fm1-N.bin on the drive", th->dim);
  } else {
    pk_split(pk_file_on(), big, small, sizeof big);
    pk_big(92, big, th->text);
    pk_center(132, small, th->dim);
  }
  if (pk_doing != DO_DELETE && pk_dev == 0)
    pk_center(182, "Low battery: charge it first", PK_RED);
  pk_foot(PK_OCT_UP, "yes", "any other", "no", 0, 0);
}

static void pk_paint_busy(void) {
  char big[PK_FILE + 1u], small[PK_FILE + 1u];
  pk_tabs(0);
  pk_title(pk_doing == DO_BACKUP ? "BACKING UP" : pk_page == PG_SAVE ? "RESTORING"
                                                                     : "INSTALLING",
           th->accent);
  if (pk_doing != DO_BACKUP) {
    pk_split(pk_file_on(), big, small, sizeof big);
    pk_big(92, big, th->text);
    pk_center(132, small, th->dim);
  }
  pk_center(176, "Don't unplug it", th->dim);
}

static void pk_paint_details(void) {
  char big[PK_FILE + 1u], small[PK_FILE + 1u], v[24];
  pk_tabs(0);
  pk_title("DETAILS", th->accent);
  pk_split(pk_file_on(), big, small, sizeof big);
  scr_ital = PK_ITAL;
  scr_bold(14, 56, &FONT_L, big, th->text, 220);
  scr_text(18, 86, &FONT_S, small, th->dim, 214);
  scr_ital = 0;
  pk_info_row(112, "KIND", pk_page == PG_FW ? "firmware" : "backup", th->text);
  pk_size(v, pk_ab_ok ? pk_ab_size : pk_sizes[pk_rows[pk_sel]]);
  pk_info_row(136, "SIZE", v, th->text);
  pk_info_row(160, "FOR", pk_ab_product[0] ? pk_ab_product : pk_ab_ok ? PK_DEVICE
                                                                      : "..",
              th->text);
  pk_info_row(184, "DATE", pk_ab_date[0] ? pk_ab_date : pk_ab_ok ? "-"
                                                                 : "..",
              pk_ab_date[0] ? th->text : th->dim);
  pk_foot(PK_OCT_UP, pk_page == PG_FW ? "install" : "restore", PK_REC, "delete", 0, 0);
}

static void pk_paint_about(void) {
  char v[28];
  pk_tabs(0);
  pk_title("ABOUT", th->accent);
  if (pk_chip_flash) { /* what it said of itself, the last time the board wrote or read it */
    pk_hex(v, pk_chip_flash, 6);
    if (pk_chip_size) {
      char m[8];
      fmt_int(m, (int32_t)(pk_chip_size / (1024u * 1024u)));
      str_cpy(v + str_len(v), " ", 2);
      str_cpy(v + str_len(v), m, sizeof v - str_len(v));
      str_cpy(v + str_len(v), "M", sizeof v - str_len(v));
    }
    pk_info_row(62, "FLASH CHIP", v, th->text);
  } else
    pk_info_row(62, "FLASH CHIP", "not yet", th->dim);
  if (pk_chip_key) {
    pk_hex(v, pk_chip_key, 4);
    pk_info_row(86, "CHIP KEY", v, th->text);
  } else
    pk_info_row(86, "CHIP KEY", "-", th->dim);
  pk_info_row(110, "BOOTLOADER", "WL82 ROM", th->text); /* (its chip's, in ROM: nothing can change it) */
  pk_info_row(134, "LOADER", pk_loader < 0 ? "-" : pk_loader ? "ready"
                                                             : "missing",
              pk_loader == 0 ? PK_RED : th->text);
  pk_size(v, pk_free);
  str_cpy(v + str_len(v), " free", sizeof v - str_len(v));
  pk_info_row(158, "DRIVE", v, th->text);
  pk_info_row(182, "VERSION", pk_ver[0] ? pk_ver : "-", th->text);
  pk_foot(PK_OCT_DOWN, "back", 0, 0, 0, 0);
}

static void pk_paint(void) {
  if (pk_view == V_WAIT) {
    pk_tabs(0);
    pk_title("HELLO", th->accent);
    pk_big(96, "LILFM1BUDDY", th->text);
    pk_center(140, "Asking for the list..", th->dim);
  } else if (pk_view == V_ERROR) {
    pk_tabs(0);
    pk_title("OOPS", PK_RED);
    pk_wrap(84, pk_err, th->text);
    pk_foot(PK_OCT_UP, "ok", 0, 0, 0, 0);
  } else if (pk_view == V_LIST)
    pk_paint_list();
  else if (pk_view == V_SURE)
    pk_paint_sure();
  else if (pk_view == V_BUSY)
    pk_paint_busy();
  else if (pk_view == V_DETAILS)
    pk_paint_details();
  else
    pk_paint_about();
}

/* rows y0 .. y1 of the screen, drawn again (a strip at a time) */
static void scr_draw(uint32_t y0, uint32_t y1) {
  for (; y0 < y1; y0 += SCR_H) {
    scr_begin(0, (int32_t)y0, 240, y1 - y0 < SCR_H ? y1 - y0 : SCR_H, th->bg);
    pk_paint();
    scr_blit();
  }
}

#define pk_redraw() scr_draw(0, 240)
