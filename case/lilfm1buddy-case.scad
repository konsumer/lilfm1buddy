// The lilfm1buddy case: the board that plugs into the FM-1's USB port, in a box you can pick up. Snap-fit, no screws,
// three pieces: the base, the lid, and the BOOT button's own insert (so the button prints without the lid's overhang,
// and can be another colour). Printed without supports: the base open side up, the lid top face down, the button
// flange down.
//
//   part    base, lid, button, check (a thin plate with the two holes: lay it on the board, before the case, to see
//           where they land), preview (the case closed, with a stand-in for the board), view_* (the page's pieces)
//   board   the preset (xiaoplus, xiao, zero, devkit): its outline, and how far its USB-C sticks out
//   battery room for a flat LiPo under the board
//   boot    the BOOT switch's centre: from the USB-C end, and from the left wall (looking at the board's top with its
//   reset   USB-C to the left). The presets' are guesses: measure yours and put the numbers in. RESET gets a hole for
//           a paperclip.
//
// openscad -D 'part="lid"' -D 'board="zero"' -o lid.stl case/lilfm1buddy-case.scad   (tools/cases.sh makes them all)
//
// x from the USB-C end of the board, y across it, z up from the floor's inside. The board lies flat, its buttons up, its
// battery pads down.

/* [What] */
part = "preview"; // [preview, base, lid, button, check, view_lid, view_button, view_board, view_battery]
board = "xiaoplus"; // [xiaoplus, xiao, zero, devkit]
battery = false;
section = false;  // preview: cut in half, to see the inside

/* [The board] */
board_l = 0;  // 0: the preset's (mm); yours, if it's different
board_w = 0;
boot = [3.2, 4.8];  // BOOT's centre: from the USB-C end, from the left wall
reset = [3.2, 13.0];
btn_d = 8;  // the BOOT button's cap: the lid's hole is this wide (and it must not reach RESET's switch)
reset_d = 2.5;  // the paperclip's hole

/* [Battery] */
bat = [35, 30, 5];  // room under the board: a 503035 (500 mAh) is 35 x 30 x 5

/* [Fit] */
wall = 1.6;
floor_t = 1.2;
lid_t = 1.4;
gap = 0.4;  // around the board
fit = 0.2;  // between the lid's lip and the walls
snap = 0.5;  // how far the lid's ridges reach into the walls' grooves
preload = 0.1;  // how much the ridges pull the lid down onto the walls (no rattle)
clear = 0.3;  // between the board and the lid's ribs

$fn = 48;
LID_COLOR = "#ff8a3d";
BASE_COLOR = "#36b37e";
PCB = 1.6;  // every board here
ABOVE = 1.4;  // its BOOT and RESET switches, above the PCB
UNDER = 1.0;  // its tallest parts underneath (pads, flash)
USB_H = 3.4;  // its USB-C, around the PCB's middle
eps = 0.01;

// --------------------------------------------------------------------------------------------- boards ---
// name, length (the USB-C end first), width, the USB-C's width, how far it sticks out past the PCB
BOARDS = [["xiaoplus", 21, 17.8, 9.0, 0.0], ["xiao", 21, 17.8, 9.0, 0.0], ["zero", 23.5, 18, 9.0, 1.3], ["devkit", 52, 25.4, 9.0, 0.0]];
B = [for (b = BOARDS) if (b[0] == board) b][0];
USB_OUT = B[4];
L = (board_l > 0 ? board_l : B[1]) + USB_OUT;  // (a connector that reaches past the board's edge)
W = board_w > 0 ? board_w : B[2];
USB_W = B[3];

// the heights: the board's underside, its top, and the lid's underside
UNDER_GAP = 0.6;  // between the board's parts and what's under them
shelf_h = (battery ? bat.z + 0.8 : 0) + UNDER + UNDER_GAP;
board_top = shelf_h + PCB;
inner_h = board_top + ABOVE + clear;
rib = ABOVE + clear - 0.2;  // the lid's ribs: down to just over the board

// ------------------------------------------------------------------------------------------- frames ---
// the board's corner (its USB-C end, its left long edge) is [0, 0]; the cavity around it, after the gap
b0 = [0, 0];
b1 = [L, W];
k0 = b0 - [gap, gap];
k1 = b1 + [gap, gap];

// ------------------------------------------------------------------------------------------- shapes ---
module rbox(p0, p1, r = 1.5) {
  s = p1 - p0;
  translate([p0.x + r, p0.y + r, p0.z]) linear_extrude(s.z) offset(r) square([s.x - 2 * r, s.y - 2 * r]);
}

// a hole through the x = 0 wall, centered on c: w across (y), h up (z)
module wall_hole(c, w, h) {
  translate([c.x - wall - 1, c.y - w / 2, c.z - h / 2]) cube([wall + 2, w, h]);
}

// a 45 degree ridge (or groove) along x, centered on c, reaching d either side of it
module ridge(c, len, d) {
  translate(c) rotate([0, 90, 0]) linear_extrude(len, center = true) rotate(45) square(d * sqrt(2), center = true);
}

// ---------------------------------------------------------------------------------------- the base ---
groove_d = snap + 0.35;
groove_z = inner_h - 3.2;  // (the lid's ridges, and the grooves they take)
module base() {
  difference() {
    union() {
      difference() {
        rbox([k0.x - wall, k0.y - wall, -floor_t], [k1.x + wall, k1.y + wall, inner_h], 3);
        rbox([k0.x, k0.y, 0], [k1.x, k1.y, inner_h + 1], 1.2);
      }
      // the shelf the board rests on, 1.4 wide, around the inside
      difference() {
        rbox([k0.x, k0.y, shelf_h - 1.4], [k1.x, k1.y, shelf_h], 1.2);
        rbox([k0.x + 1.4, k0.y + 1.4, shelf_h - 2], [k1.x - 1.4, k1.y - 1.4, shelf_h + 1], 1);
        if (battery) translate([k0.x - 1, W / 2 - 3, shelf_h - 2]) cube([3.6, 6, 2.2]);  // (the battery's leads)
        // room under the USB-C's shell, which reaches below the board at its end (the shelf holds the board's edges)
        translate([k0.x - 1, W / 2 - USB_W / 2 - 0.8, shelf_h - UNDER - 0.4]) cube([3.4, USB_W + 1.6, UNDER + 0.6]);
      }
    }
    // the snap grooves, in the outside of both long walls (the lid's skirt clicks into them)
    for (y = [k0.y - wall, k1.y + wall]) ridge([(k0.x + k1.x) / 2, y, groove_z], (k1.x - k0.x) * 0.55, groove_d);
    // the USB-C, through its end
    wall_hole([k0.x, W / 2, shelf_h + PCB / 2], USB_W + 0.6, USB_H);
    // paperclip holes up through the floor, if asked for
  }
}

// ----------------------------------------------------------------------------------------- the lid ---
module lid() {
  translate([0, 0, inner_h]) difference() {
    union() {
      difference() {
        union() {
          rbox([k0.x - wall - 0.3, k0.y - wall - 0.3, 0], [k1.x + wall + 0.3, k1.y + wall + 0.3, lid_t], 3);  // the plate
          difference() {  // the skirt, outside the walls
            rbox([k0.x - wall - 0.3, k0.y - wall - 0.3, -4.2], [k1.x + wall + 0.3, k1.y + wall + 0.3, 0], 3);
            rbox([k0.x - wall + fit, k0.y - wall + fit, -5], [k1.x + wall - fit, k1.y + wall - fit, 1], 2.6);
          }
        }
        // ribs, down to just over the board's top (they hold it down on its shelf)
        for (y = [b0.y + 0.6, b1.y - 0.6]) translate([b0.x + 2.5, y - 1.6, -rib]) cube([L - 5, 3.2, rib]);
      }
      // the ridges the grooves take: on the skirt's inside, its bottom edge chamfered so it starts over the walls
      for (y = [k0.y - wall + fit, k1.y + wall - fit]) intersection() {
        ridge([(k0.x + k1.x) / 2, y, -2.4], (k1.x - k0.x) * 0.45, fit + snap);
        translate([k0.x - wall, y < W / 2 ? y - 5 : y, -4.2]) cube([k1.x - k0.x + 2 * wall, 5, 4.2]);
      }
    }
    // BOOT's hole, with the groove its fingers snap into; RESET's, for a paperclip
    translate([boot.x, boot.y, -1]) cylinder(d = btn_d, h = lid_t + 2);
    translate([boot.x, boot.y, -FINGER - 0.05]) cylinder(d = btn_d + 2.2, h = FINGER + 0.05);
    translate([reset.x, reset.y, -1]) cylinder(d = reset_d, h = lid_t + 2);
    translate([reset.x, reset.y, -1]) cylinder(d = reset_d + 1.0, h = 0.45);
  }
}

// -------------------------------------------------------------------------------------- the button ---
// in the case's own frame: a flange on the lid, a plug through its hole, four fingers in the recess under it (clear of
// the switch, which is small), and a stem that comes down onto the switch. Printed flange down (z 0: the bed)
FLANGE = 1.0;
FINGER = 0.6;
lid_top = inner_h + lid_t;
module button_case() {
  translate([boot.x, boot.y, 0]) union() {
    translate([0, 0, lid_top]) cylinder(d = btn_d + 2.6, h = FLANGE);  // on the lid, to press
    translate([0, 0, inner_h]) cylinder(d = btn_d - 0.2, h = lid_t);  // the plug, through the hole
    translate([0, 0, inner_h - FINGER]) difference() {  // the fingers, in the recess (they bend in on the way through)
      cylinder(d = btn_d + 1.6, h = FINGER);
      translate([0, 0, -eps]) cylinder(d = btn_d - 0.8, h = FINGER + 2 * eps);
      for (a = [0, 90]) rotate([0, 0, a]) translate([-0.7, -btn_d, -eps]) cube([1.4, 2 * btn_d, FINGER + 2 * eps]);
    }
    translate([0, 0, board_top + ABOVE]) cylinder(d = 3.4, h = inner_h - board_top - ABOVE + 0.15);  // onto the switch
  }
}

// printed flange down: the same, flipped about the flange's top
module button_print() {
  translate([0, 0, lid_top + FLANGE]) mirror([0, 0, 1]) button_case();
}

// a check plate: lay it on the board, and see whether the two holes are where its switches are
module check() {
  difference() {
    rbox([k0.x - wall, k0.y - wall, 0], [k1.x + wall, k1.y + wall, 1.2], 3);
    translate([boot.x, boot.y, -1]) cylinder(d = btn_d, h = 3);
    translate([reset.x, reset.y, -1]) cylinder(d = reset_d, h = 3);
  }
}

// ----------------------------------------------------------------------------------------- preview ---
module stand_in(shrink = 0) {
  translate([shrink, shrink, shelf_h + shrink]) color("#222") cube([L - 2 * shrink, W - 2 * shrink, PCB]);
  for (b = [boot, reset]) translate([b.x, b.y, board_top + (ABOVE - 0.1) / 2]) color("#666") cube([4.4, 4.4, ABOVE - 0.1], center = true);
  color("#aaa") translate([-USB_OUT + 0.1, W / 2 - USB_W / 2 + 0.1, shelf_h + PCB / 2 - USB_H / 2 + 0.1]) cube([3, USB_W - 0.2, USB_H - 0.2]);
  if (battery) translate([k0.x + 0.4, k0.y + 0.4, 0.4]) color("silver") cube([min(bat.x, L + 2 * gap - 0.8), min(bat.y, W + 2 * gap - 0.8), bat.z]);
}

// printed flange down: the same, flipped about the flange's top
module button_print() {
  translate([0, 0, lid_top + FLANGE]) mirror([0, 0, 1]) button_case();
}

module cut() {
  if (section) intersection() {
    children();
    translate([-200, W / 2, -200]) cube(400);
  }
  else children();
}

cut() {
  if (part == "base") base();
  if (part == "lid") translate([0, 0, inner_h + lid_t]) mirror([0, 0, 1]) lid();
  if (part == "button") button_print();
  if (part == "check") check();
  if (part == "preview") {
    color(BASE_COLOR) base();
    stand_in();
    color(LID_COLOR) lid();
    color(LID_COLOR) button_case();
  }
  if (part == "view_lid") color(LID_COLOR) lid();
  if (part == "view_button") color(LID_COLOR) button_case();
  if (part == "fit") intersection() {  // (nothing: it must be empty. The board shrunk, so resting on it doesn't count)
    union() {
      base();
      lid();
      button_case();
    }
    stand_in(0.08);
  }
  if (part == "fit_base") intersection() {
    base();
    stand_in(0.08);
  }
  if (part == "fit_lid") intersection() {
    lid();
    stand_in(0.08);
  }
  if (part == "fit_button") intersection() {
    button_case();
    stand_in(0.08);
  }
  if (part == "view_board") stand_in();
  if (part == "view_battery") if (battery) translate([k0.x + 1, k0.y + 1, 0.4]) color("silver") cube([min(bat.x, L + 2 * gap - 2), min(bat.y, W + 2 * gap - 2), bat.z]);
}

assert(!battery || (bat.x <= L + 2 * gap - 0.4 && bat.y <= W + 2 * gap - 0.4), "that battery is too big for this board's case: it goes under the board, inside its walls");
assert(!battery || bat.z > 0, "the battery needs a thickness");
assert(boot.x > 0 && boot.x < L && boot.y > 0 && boot.y < W, "BOOT isn't on the board");
assert(reset.x > 0 && reset.x < L && reset.y > 0 && reset.y < W, "RESET isn't on the board");
assert(btn_d > 5 && btn_d < min(L, W), "the button's cap doesn't fit the board");
// the cap's fingers, and RESET's switch (4.4 square: 2.2 from its centre), mustn't touch: pressing BOOT must not press
// RESET. (The switches are the board's; moving BOOT or RESET, or a smaller cap, is the fix.)
assert(norm(boot - reset) >= (btn_d + 1.6) / 2 + 2.2 + 0.3, "the BOOT button's cap would reach RESET's switch: move one of them, or make the cap smaller");
