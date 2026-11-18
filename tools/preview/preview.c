/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Konsumer */
/* The menu's screens (picker/ui.c), drawn on a computer: each into OUT/NAME.ppm, as the FM-1 would show it, with
 * made-up files on the drive. tools/preview.sh builds and runs it, and makes PNGs of them. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define memset lib_memset /* (fomajam's libc.c: its string helpers, its memset & co. renamed out of the way) */
#define memcpy lib_memcpy
#define memcmp lib_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
#include "screen.c"
#include "ui.c"

static const char* out = ".";

static void save(const char* name) {
  char path[256];
  FILE* f;
  int i;
  snprintf(path, sizeof path, "%s/%s.ppm", out, name);
  f = fopen(path, "wb");
  if (!f)
    return;
  fprintf(f, "P6\n240 240\n255\n");
  for (i = 0; i < 240 * 240; i++) {
    uint16_t c = lcd_fb[i];
    fputc((c >> 11) * 255 / 31, f);
    fputc(((c >> 5) & 63) * 255 / 63, f);
    fputc((c & 31) * 255 / 31, f);
  }
  fclose(f);
}

static void file(const char* name, uint32_t size) {
  str_cpy(pk_files[pk_n], name, PK_FILE + 1u);
  pk_sizes[pk_n++] = size;
}

static void show(const char* name, uint32_t view) {
  pk_view = view;
  pk_rows_make();
  pk_redraw();
  save(name);
}

int main(int argc, char** argv) {
  int t;
  if (argc > 1)
    out = argv[1];
  file("FM-1_016.fwsc", 718196);
  file("fomajam-0.1.7.fwsc", 610110);
  file("hortator-0.23.2-bluetooth.fwsc", 609649);
  file("felucca-1.6.1.fwsc", 610090);
  file("sloop-2.8.fwsc", 610035);
  file("baudgirl-FM-1+VA-102.fwsc", 859700);
  file("fm1-1.bin", 1048576);
  file("fm1-2.bin", 1048576);
  pk_dev = 2, pk_lil = 3;
  str_cpy(pk_ver, "0.5.0", sizeof pk_ver);
  pk_free = 1234567, pk_loader = 1, pk_chip_key = 0x980F, pk_chip_flash = 0x0B4014, pk_chip_size = 1048576;
  scr_init();
  show("wait", V_WAIT);
  pk_sel_on[PG_FW] = 2;
  show("firmware", V_LIST);
  pk_doing = DO_INSTALL;
  show("install", V_SURE);
  show("installing", V_BUSY);
  str_cpy(pk_ab_product, "FM-1_974", sizeof pk_ab_product);
  str_cpy(pk_ab_date, "2026-10-30", sizeof pk_ab_date);
  pk_ab_size = 609649, pk_ab_ok = 1;
  show("details", V_DETAILS);
  pk_page = PG_SAVE;
  pk_sel_on[PG_SAVE] = 0;
  show("backups", V_LIST);
  pk_sel_on[PG_SAVE] = 1;
  show("backups-pick", V_LIST);
  pk_doing = DO_DELETE;
  show("delete", V_SURE);
  pk_doing = DO_BACKUP;
  show("backup", V_SURE);
  pk_page = PG_SET;
  show("settings", V_LIST);
  show("about", V_ABOUT);
  pk_editing = 1;
  for (t = 0; t < THEME_COUNT; t++) {
    char name[32];
    th = &THEMES[t];
    snprintf(name, sizeof name, "theme-%s", th->name);
    show(name, V_LIST);
  }
  th = &THEMES[0];
  pk_editing = 0;
  str_cpy(pk_err, "That's an SMK-37's, and this is an FM-1: nothing was written.", sizeof pk_err);
  show("error", V_ERROR);
  pk_page = PG_FW, pk_n = 0;
  show("empty", V_LIST);
  return 0;
}
