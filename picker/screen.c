/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (felucca-bsp's loader/ldr_screen.c: the panel's start); David Konsumer (the rest) */
/* The menu's screen: the panel started as ldr_screen.c does, and drawn a strip at a time (up to 240 x SCR_H, in RAM:
 * the SPI DMA reads RAM only) with fomajam's fonts (platform/gen/felucca_font.h: Terminus, 4-bit anti-aliased) and
 * its themes (src/themes.h, the colors its GLOBAL page picks from). Part of picker.c's single unit. */
#include "fm1_lcd_hw.h"

typedef struct { /* fomajam's gfx.c */
  uint8_t h;
  uint8_t pad; /* the bitmap starts this many pixels left of the pen */
  uint8_t first, last;
  const uint8_t *adv; /* advance per glyph */
  const uint8_t *bw;  /* bitmap width per glyph */
  const uint16_t *off;
  const uint8_t *data;
} felucca_font_t;
#include "felucca_font.h" /* FONT_S (8 x 16, ASCII), FONT_L (16 x 32, ' '..'_') */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define GROOVE_H /* (themes.h wants only this of fomajam's groove.h) */
#define TRACKS 9
#include "themes.h"

#define SCR_H 40u /* the tallest strip: the header */
static uint16_t scr_px[240u * SCR_H]; /* byte-swapped (the panel takes MSB first) */
static uint32_t scr_w, scr_h;
static uint8_t scr_small[8];
static const theme_t *th = &THEMES[0];

static void scr_cmd(uint8_t c)
{
  fm1_lcd_send_cmd(c);
  fm1_lcd_wait();
  fm1_lcd_deselect();
}

static void scr_data(const void *p, uint32_t n) /* n > 0 */
{
  fm1_lcd_send_data(p, n);
  fm1_lcd_wait();
  fm1_lcd_deselect();
}

static void scr_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
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
static uint16_t scr_mix(uint16_t a, uint16_t b, uint32_t t)
{
  uint32_t r = ((a >> 11) * t + (b >> 11) * (15u - t)) / 15u;
  uint32_t g = (((a >> 5) & 63u) * t + ((b >> 5) & 63u) * (15u - t)) / 15u;
  uint32_t l = ((a & 31u) * t + (b & 31u) * (15u - t)) / 15u;
  return (uint16_t)(r << 11 | g << 5 | l);
}

/* a w x h strip, all bg; then drawn into, then scr_blit */
static void scr_begin(uint32_t w, uint32_t h, uint16_t bg)
{
  uint32_t i;
  scr_w = w;
  scr_h = h > SCR_H ? SCR_H : h;
  for (i = 0; i < scr_w * scr_h; i++)
    scr_px[i] = scr_swap(bg);
}

static void scr_blit(uint32_t x, uint32_t y)
{
  scr_window(x, y, x + scr_w - 1u, y + scr_h - 1u);
  scr_data(scr_px, scr_w * scr_h * 2u);
}

/* a rectangle in the strip, its corners rounded by r (0: square) */
static void scr_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
  int32_t i, j;
  uint16_t s = scr_swap(c);
  for (j = 0; j < h; j++) {
    int32_t in = 0, d = j < r ? r - 1 - j : j >= h - r ? j - (h - r) : -1;
    if (d >= 0) /* (in from the side: where the corner's circle is, this row) */
      while (in < r && (r - in) * (r - in) + d * d > r * r)
        in++;
    for (i = x + in; i < x + w - in; i++)
      if ((uint32_t)i < scr_w && (uint32_t)(y + j) < scr_h)
        scr_px[(uint32_t)(y + j) * scr_w + (uint32_t)i] = s;
  }
}

static uint32_t scr_glyph(const felucca_font_t *f, uint32_t ch)
{
  if (ch >= 'a' && ch <= 'z' && f->last < 'a')
    ch -= 32u;
  if (ch < f->first || ch > f->last)
    ch = '?';
  return ch - f->first;
}

static int32_t scr_text_w(const felucca_font_t *f, const char *s)
{
  int32_t w = 0;
  for (; *s; s++)
    w += f->adv[scr_glyph(f, (uint8_t)*s)];
  return w;
}

/* one glyph in the strip, its pen at x, y, in ramp's shades; returns the next pen */
static int32_t scr_char(int32_t x, int32_t y, const felucca_font_t *f, uint32_t gi, const uint16_t *ramp)
{
  uint32_t gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
  const uint8_t *gd = f->data + f->off[gi];
  for (gy = 0; gy < f->h; gy++)
    for (gx = 0; gx < w; gx++) {
      uint32_t v = gd[gy * bpr + gx / 2u], px = (uint32_t)(x - f->pad + (int32_t)gx), py = (uint32_t)(y + (int32_t)gy);
      v = (gx & 1u) ? (v & 15u) : (v >> 4);
      if (v && px < scr_w && py < scr_h)
        scr_px[py * scr_w + px] = ramp[v];
    }
  return x + f->adv[gi];
}

/* text in the strip at x, y, on bg, max px wide at most (cut short with ".." if it's wider); returns its end */
static int32_t scr_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t fg, uint16_t bg,
                        int32_t max)
{
  uint16_t ramp[16];
  uint32_t a, dot = scr_glyph(f, '.');
  int cut = scr_text_w(f, s) > max;
  int32_t end = x + max - (cut ? 2 * f->adv[dot] : 0);
  for (a = 0; a < 16u; a++)
    ramp[a] = scr_swap(scr_mix(fg, bg, a));
  for (; *s; s++) {
    uint32_t gi = scr_glyph(f, (uint8_t)*s);
    if (x + f->adv[gi] > end)
      break;
    x = scr_char(x, y, f, gi, ramp);
  }
  if (cut) {
    x = scr_char(x, y, f, dot, ramp);
    x = scr_char(x, y, f, dot, ramp);
  }
  return x;
}

/* text centered across the strip */
static void scr_center(int32_t y, const felucca_font_t *f, const char *s, uint16_t fg, uint16_t bg)
{
  int32_t w = scr_text_w(f, s);
  scr_text(w < (int32_t)scr_w ? ((int32_t)scr_w - w) / 2 : 0, y, f, s, fg, bg, (int32_t)scr_w);
}

/* a whole area in one color, a strip at a time */
static void scr_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
  scr_begin(w, SCR_H, c);
  for (; h; h -= scr_h, y += scr_h) {
    scr_h = h < SCR_H ? h : SCR_H;
    scr_blit(x, y);
  }
}

static void scr_init(void)
{
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
