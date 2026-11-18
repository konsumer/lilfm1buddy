/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (felucca-bsp's loader/ldr_screen.c: the panel's start); David Konsumer (the rest) */
/* The menu's screen: the panel started as ldr_screen.c does, and drawn a strip at a time (up to 240 x SCR_H, in RAM:
 * the SPI DMA reads RAM only) with fomajam's fonts (platform/gen/felucca_font.h: Terminus, 4-bit anti-aliased) and
 * its themes (src/themes.h, the colors its GLOBAL page picks from). Everything is drawn in the screen's own
 * coordinates, clipped to the strip: a whole screen is the same drawing for each strip down it (ui.c's scr_draw).
 * Part of picker.c's single unit (and tools/preview.c's). */
#include "fm1_lcd_hw.h"

typedef struct { /* fomajam's gfx.c */
  uint8_t h;
  uint8_t pad; /* the bitmap starts this many pixels left of the pen */
  uint8_t first, last;
  const uint8_t* adv; /* advance per glyph */
  const uint8_t* bw;  /* bitmap width per glyph */
  const uint16_t* off;
  const uint8_t* data;
} felucca_font_t;
#include "felucca_font.h" /* FONT_S (8 x 16, ASCII), FONT_L (16 x 32, ' '..'_') */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define GROOVE_H /* (themes.h wants only this of fomajam's groove.h) */
#define TRACKS 9
#include "themes.h"

#define SCR_H 40u                     /* the tallest strip */
static uint16_t scr_px[240u * SCR_H]; /* byte-swapped (the panel takes MSB first) */
static int32_t scr_x0, scr_y0;        /* where the strip is on the screen */
static uint32_t scr_w, scr_h;
static int32_t scr_ital; /* text's slant: eighths of a pixel to the right, a row up */
static uint8_t scr_small[8];
static const theme_t* th = &THEMES[0];

static void scr_cmd(uint8_t c) {
  fm1_lcd_send_cmd(c);
  fm1_lcd_wait();
  fm1_lcd_deselect();
}

static void scr_data(const void* p, uint32_t n) /* n > 0 */
{
  fm1_lcd_send_data(p, n);
  fm1_lcd_wait();
  fm1_lcd_deselect();
}

static void scr_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
  scr_small[0] = (uint8_t)(x0 >> 8);
  scr_small[1] = (uint8_t)x0;
  scr_small[2] = (uint8_t)(x1 >> 8);
  scr_small[3] = (uint8_t)x1;
  scr_cmd(0x2A);
  scr_data(scr_small, 4);
  scr_small[4] = (uint8_t)(y0 >> 8);
  scr_small[5] = (uint8_t)y0;
  scr_small[6] = (uint8_t)(y1 >> 8);
  scr_small[7] = (uint8_t)y1;
  scr_cmd(0x2B);
  scr_data(scr_small + 4, 4);
  scr_cmd(0x2C);
}

static uint16_t scr_swap(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

/* a over b by t (0..15) */
static uint16_t scr_mix(uint16_t a, uint16_t b, uint32_t t) {
  uint32_t r = ((a >> 11) * t + (b >> 11) * (15u - t)) / 15u;
  uint32_t g = (((a >> 5) & 63u) * t + ((b >> 5) & 63u) * (15u - t)) / 15u;
  uint32_t l = ((a & 31u) * t + (b & 31u) * (15u - t)) / 15u;
  return (uint16_t)(r << 11 | g << 5 | l);
}

/* a w x h strip at x, y on the screen, all bg; then drawn into, then scr_blit */
static void scr_begin(int32_t x, int32_t y, uint32_t w, uint32_t h, uint16_t bg) {
  uint32_t i;
  scr_x0 = x;
  scr_y0 = y;
  scr_w = w;
  scr_h = h > SCR_H ? SCR_H : h;
  for (i = 0; i < scr_w * scr_h; i++)
    scr_px[i] = scr_swap(bg);
}

static void scr_blit(void) {
  scr_window((uint32_t)scr_x0, (uint32_t)scr_y0, (uint32_t)scr_x0 + scr_w - 1u, (uint32_t)scr_y0 + scr_h - 1u);
  scr_data(scr_px, scr_w * scr_h * 2u);
}

/* c over the pixel at x, y by a (0..15), if it's in the strip */
static void scr_put(int32_t x, int32_t y, uint16_t c, uint32_t a) {
  uint32_t px = (uint32_t)(x - scr_x0), py = (uint32_t)(y - scr_y0);
  if (!a || px >= scr_w || py >= scr_h)
    return;
  px += py * scr_w;
  scr_px[px] = scr_swap(a >= 15u ? c : scr_mix(c, scr_swap(scr_px[px]), a));
}

/* whether rows y .. y + h - 1 touch the strip */
static int scr_rows(int32_t y, int32_t h) { return y < scr_y0 + (int32_t)scr_h && y + h > scr_y0; }

/* a rectangle, its corners rounded by r (0: square) */
static void scr_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c) {
  int32_t i, j;
  if (!scr_rows(y, h))
    return;
  for (j = 0; j < h; j++) {
    int32_t in = 0, d = j < r ? r - 1 - j : j >= h - r ? j - (h - r)
                                                       : -1;
    if (d >= 0) /* (in from the side: where the corner's circle is, this row) */
      while (in < r && (r - in) * (r - in) + d * d > r * r)
        in++;
    for (i = x + in; i < x + w - in; i++)
      scr_put(i, y + j, c, 15);
  }
}

/* a parallelogram, w wide and h high, its bottom left at x: each row up is k eighths of a pixel further right (a
 * slash, as the menu's panels lean). Its slanted sides are smoothed */
static void scr_slant(int32_t x, int32_t y, int32_t w, int32_t h, int32_t k, uint16_t c) {
  int32_t j;
  if (!scr_rows(y, h))
    return;
  for (j = 0; j < h; j++) {
    int32_t l = x * 8 + (h - 1 - j) * k, r = l + w * 8, px; /* (in eighths) */
    if (y + j < scr_y0 || y + j >= scr_y0 + (int32_t)scr_h)
      continue;
    for (px = l >> 3; px * 8 < r; px++) {
      int32_t a = px * 8 < l ? px * 8 + 8 - l : 8, b = px * 8 + 8 > r ? r - px * 8 : 8; /* (cover: of 8) */
      int32_t cov = a + b - 8;
      if (cov > 0)
        scr_put(px, y + j, c, (uint32_t)(cov * 15 / 8));
    }
  }
}

static uint32_t scr_glyph(const felucca_font_t* f, uint32_t ch) {
  if (ch >= 'a' && ch <= 'z' && f->last < 'a')
    ch -= 32u;
  if (ch < f->first || ch > f->last)
    ch = '?';
  return ch - f->first;
}

static int32_t scr_text_w(const felucca_font_t* f, const char* s) {
  int32_t w = 0;
  for (; *s; s++)
    w += f->adv[scr_glyph(f, (uint8_t)*s)];
  return w;
}

/* one glyph, its pen at x, y, in fg (slanted by scr_ital); returns the next pen */
static int32_t scr_char(int32_t x, int32_t y, const felucca_font_t* f, uint32_t gi, uint16_t fg) {
  uint32_t gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
  const uint8_t* gd = f->data + f->off[gi];
  if (!scr_rows(y, f->h))
    return x + f->adv[gi];
  for (gy = 0; gy < f->h; gy++) {
    int32_t s = ((int32_t)f->h - 1 - (int32_t)gy) * scr_ital, si = s >> 3, sf = s & 7, prev = 0;
    for (gx = 0; gx <= w; gx++) { /* (one more: what the slant's fraction carries over) */
      int32_t v = 0;
      if (gx < w) {
        v = gd[gy * bpr + gx / 2u];
        v = (gx & 1u) ? (v & 15) : (v >> 4);
      }
      scr_put(x - f->pad + (int32_t)gx + si, y + (int32_t)gy, fg, (uint32_t)((v * (8 - sf) + prev * sf) >> 3));
      prev = v;
    }
  }
  return x + f->adv[gi];
}

/* text at x, y, at most max px wide (cut short with ".." if it's wider); returns its end */
static int32_t scr_text(int32_t x, int32_t y, const felucca_font_t* f, const char* s, uint16_t fg, int32_t max) {
  uint32_t dot = scr_glyph(f, '.');
  int cut = scr_text_w(f, s) > max;
  int32_t end = x + max - (cut ? 2 * f->adv[dot] : 0);
  for (; *s; s++) {
    uint32_t gi = scr_glyph(f, (uint8_t)*s);
    if (x + f->adv[gi] > end)
      break;
    x = scr_char(x, y, f, gi, fg);
  }
  if (cut) {
    x = scr_char(x, y, f, dot, fg);
    x = scr_char(x, y, f, dot, fg);
  }
  return x;
}

/* the same, heavier: drawn twice, a pixel apart */
static int32_t scr_bold(int32_t x, int32_t y, const felucca_font_t* f, const char* s, uint16_t fg, int32_t max) {
  scr_text(x, y, f, s, fg, max - 1);
  return scr_text(x + 1, y, f, s, fg, max - 1) + 1;
}

/* a whole area in one color, a strip at a time */
static void scr_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) {
  for (; h; h -= scr_h, y += scr_h) {
    scr_begin((int32_t)x, (int32_t)y, w, h, c);
    scr_blit();
  }
}

static void scr_init(void) {
  fm1_lcd_hw_init(); /* pins, backlight on, SPI1 */
  fm1_delay_ms(20);
  scr_cmd(0x01); /* SWRESET */
  fm1_delay_ms(150);
  scr_cmd(0x11); /* SLPOUT */
  fm1_delay_ms(120);
  scr_cmd(0x3A); /* COLMOD: RGB565 */
  scr_small[0] = 0x55;
  scr_data(scr_small, 1);
  scr_cmd(0x36); /* MADCTL: top-left origin, RGB order */
  scr_small[0] = 0x00;
  scr_data(scr_small, 1);
  scr_cmd(0x21); /* INVON: IPS panel */
  scr_cmd(0x13); /* NORON */
  scr_fill(0, 0, 240, 240, th->bg);
  scr_cmd(0x29); /* DISPON */
}
