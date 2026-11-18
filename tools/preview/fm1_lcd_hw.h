/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 David Konsumer */
/* The FM-1's panel, for tools/preview.c: what screen.c sends it (a window, then its pixels) kept in a picture */
#pragma once
#include <stdint.h>

static uint16_t lcd_fb[240 * 240];
static uint32_t lcd_x0, lcd_x1, lcd_y0, lcd_y1, lcd_at;
static uint8_t lcd_cmd;

static void fm1_lcd_hw_init(void) {}
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static void fm1_lcd_wait(void) {}
static void fm1_lcd_deselect(void) {}
static void fm1_lcd_send_cmd(uint8_t c) {
  lcd_cmd = c;
  lcd_at = 0;
}

static void fm1_lcd_send_data(const void* p, uint32_t n) {
  const uint8_t* b = (const uint8_t*)p;
  uint32_t i;
  if (lcd_cmd == 0x2A) {
    lcd_x0 = (uint32_t)b[0] << 8 | b[1];
    lcd_x1 = (uint32_t)b[2] << 8 | b[3];
  } else if (lcd_cmd == 0x2B) {
    lcd_y0 = (uint32_t)b[0] << 8 | b[1];
    lcd_y1 = (uint32_t)b[2] << 8 | b[3];
  } else if (lcd_cmd == 0x2C)
    for (i = 0; i + 1u < n; i += 2u, lcd_at++) {
      uint32_t w = lcd_x1 - lcd_x0 + 1u, x = lcd_x0 + lcd_at % w, y = lcd_y0 + lcd_at / w;
      if (x < 240u && y < 240u && y <= lcd_y1)
        lcd_fb[y * 240u + x] = (uint16_t)(b[i] << 8 | b[i + 1u]);
    }
}
