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
#define PK_LINKED() (usb.config != 0)
static void pk_wire(const uint8_t* m, uint32_t n) { ota_wire_send(m, n); }
#include "menu.c"

static int32_t pk_batt_raw; /* the device's battery: smoothed ADC (0: not read yet) */

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

static void pk_main(void) {
  pk_keys_t k;
  fm1_wdt_arm(0x0D); /* ~8 s, fed by ldr_poll */
  fm1_time_init();
  scr_init();
  fm1_input_init();
  fm1_adc_init();
  pk_begin();
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
    }
    ota_service();   /* handshake: who it is */
    usb.ota_req = 0; /* (M-UPGRADE: not here) */
    pk_batt_poll(now);
    pk_input(&k);
    pk_glow(pk_lit());
    pk_tick(now, &k);
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
