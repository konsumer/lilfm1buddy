// The lilfm1buddy case: snap-fit, no screws, no supports. Four printed pieces:
//
//   shell    the top and the walls, printed top face down. Rails under the top hold the board's edges down; the
//            USB-C gets a window with room for a plug's moulding; the LED shows through a hole (the Zero) or through
//            the buttons (the XIAOs, whose LEDs sit right next to their switches)
//   base     snaps into the shell's open bottom (a ridge on each long side clicks into a groove in the walls, and pulls
//            the base up against them). Its lips push the board up onto the rails with a springy strip: no rattle
//   buttons  BOOT (bigger, and it stands out) and RESET (flush: no paperclip, and it isn't pressed by accident). Each
//            goes into its hole from inside: a flange under the top keeps it there, and a peg presses the switch
//
//   part     preview (the case closed, with a stand-in for the board), shell, base, buttons (to print), view_* (the
//            page's pieces), fit_* (the checks: tools/cases.sh --check)
//   board    zero, xiao, xiaoplus: everything about the board is in its row of BOARDS below
//   battery  room for a flat LiPo under the board (the XIAOs: they charge it). bat = [width, length, thickness]
//
// openscad -D 'part="shell"' -D 'board="zero"' -o shell.stl case/lilfm1buddy-case.scad   (tools/cases.sh makes them all)
//
// The board's frame: seen from above, components up, USB-C at the top. x across it from its left edge, y along it from
// the far end (its USB-C end is y = L), z up from its top face (its underside is z = -PCB).

/* [What] */
part = "preview"; // [preview, shell, base, buttons, view_shell, view_base, view_buttons, view_board, view_battery]
board = "zero"; // [zero, xiao, xiaoplus]
battery = false;
bat = [12, 30, 4]; // the battery's room: across, along, thick (a 401230 cell, with a little room)
antenna = true; // the XIAOs: a channel down the far wall for the antenna's cable (the board has no antenna of its own)
section = false; // preview: cut in half across the buttons, to see the inside

/* [USB-C] */
usb_room = [12.6, 6.8]; // the window: the plug's moulding goes into it (wide, tall)

/* [Buttons] */
boot_out = 1.2; // how far BOOT stands out of the top (RESET is flush)

/* [Fit] */
wall = 1.6;
plate = 1.6; // the top
floor_t = 1.2; // the base's floor
gap = 0.25; // around the board
fit = 0.15; // between the base's lips and the walls
snap = 0.5; // how far the base's ridges stand out (into the walls' grooves)
preload = 0.1; // how hard the ridges pull the base up against the walls
clear = 1.25; // over the tallest part, to the top's underside: a pressed button's flange still clears it
under = 3.0; // room under the board without a battery (its pins' solder, the USB-C's legs)
under_bat = 1.5; // between the board and the battery (the leads)
lip_t = 1.2;
spring = 0.35; // how far the lips' bumps press the board up (the strip under them gives up to 0.8)
press_gap = 0.15; // between a button's peg and its switch, at rest
btn_clear = 0.2; // around a button in its hole
led_d = 1.6; // the LED's hole in the top (the Zero)
light_d = 1.2; // the LED's hole through a button (the XIAOs)

$fn = 48;
eps = 0.01;
SHELL_COLOR = "#ff8a3d";
BASE_COLOR = "#36b37e";
BUTTON_COLOR = "#f4f4f4";

// ------------------------------------------------------------------------------------------- boards ---
// Measured from each maker's own CAD, not guessed:
//   zero      Waveshare's STEP model (ESP32-S3-Zero_v2): the PCB 18.02 x 23.52 x 1.6, its parts' boxes, the USB-C's
//             mouth 0.09 .. 3.25 over the top face and 1.22 past the edge. Its switches are 3 x 2.5, 1.6 high with
//             their actuators, either side of the LED: BOOT right, RESET left (a print on a real board says so: its
//             pinout picture has them the other way round). Its own drawing: 18 x 23.5
//   xiao      Seeed's STEP model (XIAO-ESP32S3 v2): 17.78 x 20.95, PCB 1.25; the USB-C's mouth 0 .. 3.21, 1.53 past the
//             edge. R (RESET) is left of the USB-C and B (BOOT) right, 0.73 high; the two LEDs (charge, and the user
//             LED) 2.2 below them, from its KiCad (v1.5)
//   xiaoplus  Seeed's KiCad (XIAO ESP32S3 Plus v1.1): Edge.Cuts 17.78 x 21.14, K1 (RESET) and K2 (BOOT), CHG0 and D3
//             (the LEDs); its parts' heights are the XIAO's (the same switches, the same USB-C)
// Each row: [name, value]. size: W, L, PCB. usb: its mouth's x0, x1, z0, z1, how far past the edge, how long it is.
// boot, reset: the switches' centres; sw: their top, and the peg's diameter. leds: the LEDs' centres. caps: each button
// [x, y, diameter] (the peg is under the switch, the cap may sit beside it). rails: the y range of the rails along the
// left and right edges (clear of the flanges). parts: boxes [x0, y0, x1, y1, height] to keep out of. ufl: the antenna's
// socket. light: the LEDs show through the buttons (or through holes of their own).
BOARDS = [
  ["zero", [
    ["size", [18.02, 23.52, 1.6]],
    ["usb", [4.54, 13.48, 0.09, 3.25, 1.22, 7.6]],
    ["boot", [13.23, 14.85]], ["reset", [4.88, 14.85]], ["sw", [1.6, 1.4]],
    ["leds", [[9.10, 14.83]]],
    ["boot_cap", [13.55, 14.85, 4.8]], ["reset_cap", [4.88, 14.85, 3.4]],
    ["light", false],
    ["rails", [[1.5, 22.0], [1.5, 22.0]]],
    ["parts", [
      [3.95, 2.69, 14.06, 12.80, 1.0], // the ESP32-S3
      [2.99, 0.22, 9.99, 2.22, 1.0], // its antenna
      [12.70, 10.98, 15.59, 12.17, 1.2], [2.75, 10.99, 4.35, 12.99, 0.6],
      [8.35, 13.93, 9.85, 15.73, 1.0], // the RGB LED
      [3.38, 13.60, 6.38, 16.10, 1.6], [11.73, 13.60, 14.73, 16.10, 1.6] // BOOT, RESET
    ]],
    ["ufl", undef]
  ]],
  ["xiao", [
    ["size", [17.78, 20.95, 1.25]],
    ["usb", [4.41, 13.35, 0.0, 3.21, 1.53, 7.3]],
    ["boot", [14.81, 19.36]], ["reset", [2.96, 19.37]], ["sw", [0.73, 1.2]],
    ["leds", [[2.96, 17.16], [14.86, 17.15]]],
    ["boot_cap", [14.45, 17.65, 5.0]], ["reset_cap", [3.0, 18.15, 4.2]],
    ["light", true],
    ["rails", [[1.5, 15.0], [1.5, 14.0]]],
    ["parts", [
      [2.59, 3.64, 15.19, 14.24, 2.0], // the shield
      [2.0, 0.0, 6.4, 3.9, 2.6], // the antenna's socket, with its plug in
      [2.16, 18.08, 3.76, 20.68, 0.73], [14.01, 18.06, 15.61, 20.66, 0.73] // RESET, BOOT
    ]],
    ["ufl", [4.16, 1.72]]
  ]],
  ["xiaoplus", [
    ["size", [17.78, 21.14, 1.25]],
    ["usb", [4.42, 13.36, 0.0, 3.21, 1.50, 7.35]],
    ["boot", [14.73, 19.48]], ["reset", [3.05, 19.48]], ["sw", [0.73, 1.2]],
    ["leds", [[3.05, 17.27], [14.78, 17.27]]],
    ["boot_cap", [14.45, 17.8, 5.0]], ["reset_cap", [3.05, 18.25, 4.2]],
    ["light", true],
    ["rails", [[1.5, 15.0], [1.5, 14.3]]],
    ["parts", [
      [2.6, 3.6, 15.2, 14.3, 2.0], // the ESP32-S3 (as if shielded)
      [7.3, 0.25, 14.8, 3.25, 1.2], // the board-to-board connector
      [2.0, 0.0, 6.4, 3.9, 2.6], // the antenna's socket, with its plug in
      [2.25, 18.18, 3.85, 20.78, 0.73], [13.93, 18.18, 15.53, 20.78, 0.73] // RESET, BOOT
    ]],
    ["ufl", [4.04, 1.73]]
  ]]
];

function row(t, k) = t[search([k], t)[0]][1];
B = row(BOARDS, board);
function b(k) = row(B, k);

W = b("size")[0];
L = b("size")[1];
PCB = b("size")[2];
USB = b("usb");
SW_TOP = b("sw")[0];
PEG = b("sw")[1];
LIGHT = b("light");
UFL = b("ufl");

// ------------------------------------------------------------------------------------------- heights ---
TALL = max(USB[3], SW_TOP, max([for (p = b("parts")) p[4]]));
ZP = TALL + clear; // the top's underside
ZTOP = ZP + plate;
ZB = -PCB; // the board's underside
ZF = ZB - max(battery ? under_bat + bat.z : under, 3.0); // the floor's top (and the walls' bottom edge)
ZFB = ZF - floor_t;

// the cavity: the board's outline and the gap; longer at the far end if the battery needs it
inner_l = L + 2 * gap - 2 * (fit + lip_t) - 0.6; // between the base's end lips, with a little room
EXT = battery ? max(0, bat.y - inner_l) : 0;
C0 = [-gap, -gap - EXT];
C1 = [W + gap, L + gap];
O0 = C0 - [wall, wall];
O1 = C1 + [wall, wall];

// the base's ridges and the walls' grooves: the groove sits a little higher, so the ridge pulls the base up
Z_RIDGE = ZF + 1.0;
GROOVE = snap + 0.2;
Z_GROOVE = Z_RIDGE + fit + 0.2 + preload;
RIDGE_LEN = (C1.y - C0.y) * 0.5;
RIDGE_Y = (C0.y + C1.y) / 2;

// the USB-C's window
USB_C = [(USB[0] + USB[1]) / 2, (USB[2] + USB[3]) / 2];
USB_WIN = [max(usb_room.x, USB[1] - USB[0] + 1.0), max(usb_room.y, USB[3] - USB[2] + 1.0)];

// ------------------------------------------------------------------------------------------- shapes ---
module rrect(p0, p1, r) {
  r = min(r, (p1.x - p0.x) / 2 - eps, (p1.y - p0.y) / 2 - eps);
  translate([p0.x + r, p0.y + r]) offset(r) square([p1.x - p0.x - 2 * r, p1.y - p0.y - 2 * r]);
}

// a rounded box, its bottom and top edges chamfered by cb, ct
module rbox(p0, p1, z0, z1, r, cb = 0, ct = 0) {
  hull() {
    translate([0, 0, z0]) linear_extrude(eps) rrect(p0 + [cb, cb], p1 - [cb, cb], r - cb);
    translate([0, 0, z0 + cb]) linear_extrude(z1 - z0 - cb - ct) rrect(p0, p1, r);
    translate([0, 0, z1 - eps]) linear_extrude(eps) rrect(p0 + [ct, ct], p1 - [ct, ct], r - ct);
  }
}

// a 45 degree ridge along y, centred on [x, y, z], reaching d to each side
module ridge(x, y, z, len, d) {
  translate([x, y, z]) rotate([90, 0, 0]) linear_extrude(len, center = true) rotate(45) square(d * sqrt(2), center = true);
}

// a button's outline: round, or with a flat on its +y side (a cap whose peg or light hole is off its centre must
// not turn)
function keyed(c, sw) = LIGHT || norm([c.x, c.y] - sw) > 0.05;
module cap_2d(r, flat) {
  if (flat == undef) circle(r);
  else intersection() {
    circle(r);
    translate([-r - 1, -r - 1]) square([2 * r + 2, r + 1 + flat]);
  }
}

// ---------------------------------------------------------------------------------------------- shell ---
module button_hole(c, sw) {
  rc = c[2] / 2;
  rh = rc + btn_clear;
  rf = rc + 0.8;
  translate([c.x, c.y, 0]) {
    translate([0, 0, ZP - 1]) linear_extrude(plate + 2) cap_2d(rh, keyed(c, sw) ? rc - 0.6 + btn_clear : undef);
    // its seat: the flange's cone fits it (running on into the hole, and turned half a facet: so the two don't meet
    // edge on edge)
    translate([0, 0, ZP - eps]) rotate(180 / 48) cylinder(r1 = rf + eps, r2 = rh - 0.3, h = rf - rh + 0.3 + 2 * eps);
  }
}

module shell() {
  difference() {
    union() {
      difference() {
        rbox(O0, O1, ZF, ZTOP, 2.5, 0, 0.5);
        translate([0, 0, ZF - 1]) linear_extrude(ZP - ZF + 1) rrect(C0, C1, 0.6);
      }
      // the rails: down onto the board's edges (its castellated pads), from the top
      for (s = [0, 1]) {
        r = b("rails")[s];
        translate([s == 0 ? C0.x - eps : W - 1.0, r[0], 0]) cube([1.0 + gap + eps, r[1] - r[0], ZP - 0 + eps]);
      }
      // a battery longer than the board: a wall across the cavity stops the board at its far end
      if (EXT > 0) translate([C0.x, -gap - 1.2, ZB + 0.3]) cube([C1.x - C0.x, 1.2, ZP - ZB - 0.3 + eps]);
    }
    // the USB-C's window, through its wall
    translate([USB_C.x, C1.y - 0.2, USB_C.y]) rotate([-90, 0, 0]) linear_extrude(wall + 1)
      rrect([-USB_WIN.x / 2, -USB_WIN.y / 2], [USB_WIN.x / 2, USB_WIN.y / 2], 1.2);
    button_hole(b("boot_cap"), b("boot"));
    button_hole(b("reset_cap"), b("reset"));
    if (!LIGHT) for (p = b("leds")) translate([p.x, p.y, ZP - 1]) cylinder(d = led_d, h = plate + 2);
    // the grooves the base's ridges click into, and a lead-in at the walls' bottom edge
    // (0.1 into the wall, and 0.1 down to keep the same pull: its corners mustn't sit on the wall's face)
    for (x = [C0.x - 0.1, C1.x + 0.1]) ridge(x, RIDGE_Y, Z_GROOVE - 0.1, RIDGE_LEN, GROOVE);
    hull() {
      translate([0, 0, ZF - eps]) linear_extrude(eps) rrect(C0 - [0.5, 0.5], C1 + [0.5, 0.5], 1.1);
      translate([0, 0, ZF + 0.5]) linear_extrude(eps) rrect(C0, C1, 0.6);
    }
    // a notch at the far end, to lift the base out with a fingernail
    translate([W / 2 - 3, O0.y - 1, ZF - 1]) cube([6, wall + 1.5, 1.6]);
    // the antenna's cable, from its socket down past the board's far end
    if (UFL != undef && antenna) {
      if (EXT > 0) translate([UFL.x - 2.5, -gap - 2, ZB]) cube([5, 3, ZP - ZB + 1]);
      else translate([UFL.x - 2.5, C0.y - 1.0, ZF - 1]) cube([5, 1.0 + eps, ZP - ZF + 1]);
    }
  }
}

// ----------------------------------------------------------------------------------------------- base ---
module lip_side(x0, bump = true) {
  y0 = C0.y + fit + 0.4; // (clear of the cavity's rounded corners)
  y1 = C1.y - fit - 0.4;
  difference() {
    union() {
      hull() {  // (its top's outer edge chamfered: it leads in past the walls' bottom edge)
        translate([x0, y0, ZF - eps]) cube([lip_t, y1 - y0, ZB - ZF - 0.5 + eps]);
        translate([x0 + (x0 < W / 2 ? 0.5 : 0), y0, ZF]) cube([lip_t - 0.5, y1 - y0, ZB - ZF]);
      }
      // the ridge, on its outer face
      intersection() {
        ridge(x0 < W / 2 ? x0 : x0 + lip_t, RIDGE_Y, Z_RIDGE, RIDGE_LEN, snap);
        translate([x0 < W / 2 ? x0 - snap - 1 : x0 + lip_t - 0.1, C0.y, ZF]) cube([snap + 1.1, C1.y - C0.y, ZB - ZF]);  // (into the lip a little: one piece)
      }
      // the bump that presses the board up, in the middle of the springy strip
      if (bump) hull() {
        translate([x0, L / 2 - 1.5, ZB - eps]) cube([lip_t, 3, eps]);
        translate([x0, L / 2 - 0.6, ZB]) cube([lip_t, 1.2, spring]);
      }
    }
    // the slot under the strip: it's a 0.7 bridge between the slot's ends, and gives
    translate([x0 - 1, 2.0, ZB - 0.7 - 0.8]) cube([lip_t + 2, L - 4.0, 0.8]);
  }
}

module base(bump = true) {
  rbox(O0, O1, ZFB, ZF, 2.5, 0.4, 0);
  lip_side(C0.x + fit, bump);
  lip_side(C1.x - fit - lip_t, bump);
  // the ends' lips: low, they only keep the base square in the walls
  difference() {
    for (y = [C0.y + fit, C1.y - fit - lip_t]) translate([C0.x + fit + 0.4, y, ZF - eps]) cube([C1.x - C0.x - 2 * fit - 0.8, lip_t, 1.6 + eps]);
    if (UFL != undef && antenna && EXT == 0) translate([UFL.x - 2.5, C0.y - 1, ZF]) cube([5, lip_t + 2, 2]);
  }
}

// --------------------------------------------------------------------------------------------- buttons ---
// in the case's frame, at rest: its flange 0.1 under its seat, its peg press_gap over its switch. dz: pressed down
module button(c, sw, out, dz = 0, peg = true, flange = true) {
  rc = c[2] / 2;
  rf = rc + 0.8;
  flat = keyed(c, sw) ? rc - 0.6 : undef;
  z_top = ZTOP + out;
  translate([0, 0, -dz]) difference() {
    union() {
      if (flange) translate([c.x, c.y, 0]) intersection() {
        union() {
          translate([0, 0, ZP - 0.1]) cylinder(r1 = rf, r2 = rc - 0.3, h = rf - rc + 0.3);
          translate([0, 0, ZP - 0.1]) cylinder(r = rc, h = z_top - ZP + 0.1);
          translate([0, 0, ZP - 0.7]) cylinder(r = rf, h = 0.6 + eps);
        }
        // a keyed cap's flat, through the hole; under it, where the seat is round, a 45 degree face down to the
        // flange's edge (printed cap down, a flat cut there would leave the flange's rim hanging in the air)
        if (flat != undef) union() {
          translate([0, 0, ZP - 1]) linear_extrude(z_top - ZP + 2) cap_2d(rf, flat);
          translate([0, flat, ZP + 0.6]) rotate([45, 0, 0]) translate([-50, -100, -50]) cube(100);
        }
        else translate([-50, -50, -50]) cube(100);
      }
      if (peg) translate([sw.x, sw.y, SW_TOP + press_gap]) cylinder(d = PEG, h = ZP - 0.7 - SW_TOP - press_gap + eps);
    }
    if (LIGHT) for (p = b("leds")) if (norm([p.x, p.y] - [c.x, c.y]) < rc) translate([p.x, p.y, ZP - 2]) cylinder(d = light_d, h = z_top - ZP + 3);
  }
}

module buttons(dz = 0, peg = true, flange = true) {
  button(b("boot_cap"), b("boot"), boot_out, dz, peg, flange);
  button(b("reset_cap"), b("reset"), 0, dz, peg, flange);
}

// to print: each cap down on the bed
module buttons_print() {
  for (i = [0, 1]) {
    c = i == 0 ? b("boot_cap") : b("reset_cap");
    out = i == 0 ? boot_out : 0;
    translate([i * 10, 0, ZTOP + out]) mirror([0, 0, 1]) translate([-c.x, -c.y, 0]) button(c, i == 0 ? b("boot") : b("reset"), out);
  }
}

// ------------------------------------------------------------------------------------- the stand-ins ---
// the board, as boxes: its PCB, its parts, its USB-C (inside its outline it sits on the top face; past the edge, its
// mouth), and a thin layer of small parts everywhere but its edges' pads. s: shrink, for the checks
module board_in(s = 0) {
  color("#26262b") translate([s, s, ZB + s]) linear_extrude(PCB - 2 * s) rrect([0, 0], [W - 2 * s, L - 2 * s], 1);
  color("#555") {
    for (p = b("parts")) translate([p[0] + s, p[1] + s, s]) cube([p[2] - p[0] - 2 * s, p[3] - p[1] - 2 * s, p[4] - 2 * s]);
    translate([1.2 + s, 1.2 + s, s]) cube([W - 2.4 - 2 * s, L - 2.4 - 2 * s, 0.6 - 2 * s]);
  }
  color("#aaa") {
    translate([USB[0] + s, L - USB[5] + s, s]) cube([USB[1] - USB[0] - 2 * s, USB[5] - 2 * s, USB[3] - 2 * s]);
    translate([USB[0] + s, L - eps, USB[2] + s]) cube([USB[1] - USB[0] - 2 * s, USB[4] - s, USB[3] - USB[2] - 2 * s]);
  }
}

module switches_in() {
  for (p = [b("boot"), b("reset")]) translate([p.x - PEG / 2, p.y - PEG / 2, 0]) cube([PEG, PEG, SW_TOP]);
}

// the plug: its moulding up to the receptacle's mouth (12 x 6.5, a common one), its metal in it
module plug_in() {
  translate([USB_C.x, L + USB[4], USB_C.y]) rotate([-90, 0, 0]) {
    linear_extrude(15) rrect([-6, -3.25], [6, 3.25], 1.5);
    translate([0, 0, -6.5]) linear_extrude(6.5 + eps) rrect([-4.15, -1.2], [4.15, 1.2], 1.1);
  }
}

module battery_in() {
  y0 = C0.y + fit + lip_t + 0.3;
  if (battery) color("silver") translate([W / 2 - bat.x / 2, y0, ZF]) cube(bat);
}

// --------------------------------------------------------------------------------------------- make ---
module cut() {
  if (section) intersection() {
    children();
    translate([-50, b("boot")[1], -50]) cube(100);
  }
  else children();
}

cut() {
  if (part == "shell") translate([0, 0, ZTOP]) mirror([0, 0, 1]) shell();
  if (part == "base") translate([0, 0, -ZFB]) base();
  if (part == "buttons") buttons_print();
  if (part == "preview") {
    color(SHELL_COLOR) shell();
    color(BASE_COLOR) base();
    color(BUTTON_COLOR) buttons();
    board_in();
    battery_in();
  }
  if (part == "view_shell") shell();
  if (part == "view_base") base();
  if (part == "view_buttons") buttons();
  if (part == "view_board") board_in();
  if (part == "view_battery") battery_in();

  // the checks (tools/cases.sh --check): each must come out empty, but fit_reach, which must not
  if (part == "fit_board") intersection() {  // the case at rest, against the board
    union() {
      shell();
      base(false);
      buttons();
    }
    board_in(0.02);
  }
  if (part == "fit_buttons") intersection() {  // the buttons in their holes, at rest and pressed all the way
    shell();
    union() {
      buttons();
      buttons(press_gap + 0.3);
    }
  }
  if (part == "fit_press") intersection() {  // a button pressed all the way: its flange clears the board's parts
    buttons(press_gap + 0.3, peg = false);
    board_in(0.02);
  }
  if (part == "fit_reach") intersection() {  // a little past press_gap, each peg is on its switch
    buttons(press_gap + 0.05, flange = false);
    switches_in();
  }
  if (part == "fit_base") intersection() {  // the base in the shell (but its ridges, which are meant to press, and
    shell();                                  // the floor's top, which meets the walls' bottom edge)
    translate([-50, -50, ZF + 0.02]) cube(100);
    difference() {
      base(false);
      for (x = [C0.x - snap - 1, C1.x - 1]) translate([x, C0.y, ZF + eps]) cube([snap + 1 + fit + 0.05, C1.y - C0.y, ZB - ZF]);
    }
  }
  if (part == "fit_usb") intersection() {  // the plug's way in
    shell();
    plug_in();
  }
  if (part == "fit_battery") intersection() {
    union() {
      shell();
      base();
    }
    battery_in();
  }
}

// ------------------------------------------------------------------------------------------- refusals ---
assert(!(battery && board == "zero"), "the Zero has no charger: a battery is for the XIAOs");
assert(!battery || bat.x <= C1.x - C0.x - 2 * (fit + lip_t) - 0.4, str("the battery is too wide: its room can be ", C1.x - C0.x - 2 * (fit + lip_t) - 0.4, " mm across at most"));
assert(!battery || (bat.y > 0 && bat.z > 0), "the battery needs a length and a thickness");
assert(USB_C.y + USB_WIN.y / 2 <= ZTOP - 0.8, "the USB-C's window reaches the top");
assert(USB_C.y - USB_WIN.y / 2 >= ZF + 0.8, "the USB-C's window reaches the floor");
assert(USB_WIN.x <= C1.x - C0.x - 2, "the USB-C's window is wider than the case");
for (i = [0, 1]) {
  c = i == 0 ? b("boot_cap") : b("reset_cap");
  sw = i == 0 ? b("boot") : b("reset");
  rf = c[2] / 2 + 0.8;
  // its seat and flange inside the cavity, clear of the rails, and over its peg
  assert(c.x - rf >= C0.x + 0.1 && c.x + rf <= C1.x - 0.1 && c.y - rf >= C0.y + 0.1 && c.y + rf <= C1.y - 0.1, "a button's flange reaches the walls");
  assert(norm([c.x, c.y] - sw) + PEG / 2 <= rf - 0.3, "a button's peg isn't under its flange");
  for (s = [0, 1]) {
    r = b("rails")[s];
    rx = s == 0 ? 1.0 : W - 1.0;
    near = s == 0 ? c.x - rf - rx : rx - (c.x + rf);
    assert(near >= 0.2 || c.y + rf <= r[0] - 0.2 || c.y - rf >= r[1] + 0.2, "a button's flange reaches a rail");
  }
  // the XIAOs: the LED shows through its button, inside its cap (and its flat)
  if (LIGHT) for (p = b("leds")) if (norm([p.x, p.y] - [c.x, c.y]) < c[2] / 2) assert(norm([p.x, p.y] - [c.x, c.y]) + light_d / 2 <= c[2] / 2 - 0.4 && p.y - c.y + light_d / 2 <= c[2] / 2 - 0.6 - 0.3, "an LED's hole isn't inside its button");
  if (LIGHT) assert(len([for (p = b("leds")) if (norm([p.x, p.y] - [c.x, c.y]) < c[2] / 2) 1]) == 1, "a button isn't over its LED");
  if (!LIGHT) for (p = b("leds")) assert(norm([p.x, p.y] - [c.x, c.y]) >= rf + led_d / 2 + 0.3, "a button's seat reaches the LED's hole");
}
assert(norm([b("boot_cap").x, b("boot_cap").y] - [b("reset_cap").x, b("reset_cap").y]) >= b("boot_cap")[2] / 2 + b("reset_cap")[2] / 2 + 1.6 + 0.6, "the buttons' flanges meet");
