// The lilfm1buddy case: the board that plugs into the FM-1's USB port, in a box you can pick up. Snap-fit, no screws,
// three pieces: the base, the lid, and the BOOT button (a flange on the lid, a neck through its hole, and a lip under
// the plate so it stays). Printed without supports: the base open side up, the lid skirt up, the button flange down.
//
//   part    base, lid, button, preview (the case closed, with a stand-in for the board), view_* (the page's pieces),
//           fit / fit_usb (the checks: each must come out empty)
//   board   one of BOARDS below: its outline, its USB-C, its switches, from its maker's own CAD
//
// The board lies flat, buttons up, its battery pads down. x from its USB-C end, y across it (0 at the long edge that
// is at the bottom of a top view with the USB-C on the left), z up from the floor's inside.
//
// openscad -D 'part="lid"' -D 'board="zero"' -o lid.stl case/lilfm1buddy-case.scad   (tools/cases.sh makes them all)

/* [What] */
part = "preview"; // [preview, base, lid, button, view_lid, view_button, view_board, view_battery, fit, fit_button, fit_usb]
board = "xiaoplus"; // [xiaoplus, xiao, zero]
battery = false;
section = false;  // preview: cut in half, to see the inside

/* [Battery] */
bat = [35, 30, 5];  // room under the board: a 503035 (500 mAh) is 35 x 30 x 5

/* [Name] */
name = true;  // the lid's top face: "lilbuddy" cut into it (Arial Bold's outlines: the browser's OpenSCAD has no fonts)
name_h = 2.2;  // its x-height

/* [Fit] */
wall = 1.4;
floor_t = 1.2;
lid_t = 1.4;
usb_inset = 4.0;  // how far the board sits back from the USB-C wall: room for the button's lip under the lid
gap = 0.35;  // around the board (a printed case for this board uses 0.3)
fit = 0.35;  // between the lid's skirt and the walls
snap = 0.3;  // how far the lid's ridges reach into the grooves
preload = 0.05;  // how much they pull the lid down (no rattle)
clear = 1.0;  // over the board's tallest part, to the lid
under = 0.4;  // between the board's underside and the floor (or the battery)

$fn = 48;
LID_COLOR = "#ff8a3d";
BASE_COLOR = "#36b37e";
eps = 0.01;


// ------------------------------------------------------------------------------------------- boards ---
// The numbers below are measured, not guessed. board: length along the USB-C axis, width, PCB thickness, the USB-C's
// width, how far its shell reaches past the board's edge, how high it stands above the board's top face, where the two
// switches are (their centres, from the USB-C end and from the long edge at the bottom of a top view with the USB-C on
// the left), how far the connector reaches in (its shell), its height, the switches' height and body size, the cap.
//   xiaoplus, xiao   Seeed's published KiCad of the XIAO ESP32S3 Plus (and of the XIAO): the outline is its Edge.Cuts,
//                    its USB-C's pads reach 10.2 mm in from the edge, K1 is RESET and K2 BOOT, 2.8 x 2.6 and 1.6 high. The one number the KiCad can't give is how high the
//                    USB-C stands above the board: 3.2 is what a 16-pin receptacle does (and it only has to be cleared).
//   zero             Waveshare's STEP model of the ESP32-S3-Zero, measured here: PCB 18.02 x 23.52 x 1.6, the USB-C
//                    8.94 wide, 1.22 past the edge and 3.25 above the top face (it is on the component side), its two
//                    switches 2.5 x 3.0 and 1.6 high with their actuator, at 8.67 mm from the USB-C end, 13.24 and
//                    4.89 mm across (BOOT is the far one: the owner's first print had its hole on the wrong side,
//                    mirrored across the board).
BOARDS = [
  ["xiaoplus", 21.15, 17.78, 1.6, 8.94, 0.0, 10.2, 3.2, 1.6, 2.8, 2.6, [1.65, 3.05], [1.65, 14.73], 7],
  ["xiao", 21.15, 17.78, 1.6, 8.94, 0.0, 10.2, 3.2, 1.6, 2.8, 2.6, [1.65, 3.05], [1.65, 14.73], 7],
  ["zero", 23.52, 18.02, 1.6, 8.94, 1.22, 6.4, 3.25, 1.6, 3.0, 2.5, [8.67, 13.24], [8.67, 4.89], 8]
];

// ------------------------------------------------------------------------------------------- numbers ---
B = [for (b = BOARDS) if (b[0] == board) b][0];
L = B[1];  // the board's own outline (its USB-C's overhang is outside it, inside the wall's slot)
W = B[2];
PCB = B[3];
USB_W = B[4];
USB_OUT = B[5];
USB_LEN = B[6];
USB_H = B[7];
SW_H = B[8];
SW_W = B[9];
SW_D = B[10];
boot = B[11];
reset = B[12];

// the heights: the board's underside, its top, the tallest thing on it, and the lid's underside
UNDER = (battery ? bat.z + 0.8 : 0) + under;          // the board's underside
SHELF_T = max(0.3, UNDER - (battery ? bat.z + 0.6 : 0));  // the ledge under it (clears the battery)
board_top = UNDER + PCB;
inner_h = board_top + max(USB_H, SW_H) + clear;
rib = inner_h - board_top - 0.2;  // the lid's ribs: down to just over the board

// the board's corner (its USB-C end, its long edge) is [0, 0]; the cavity around it, after the gap
b0 = [0, 0];
b1 = [L, W];
k0 = [b0.x - gap - usb_inset, b0.y - gap];
k1 = b1 + [gap, gap];

// the button: as big as the table says, or as the case allows. Its lip (under the lid's plate) has to stay inside
// the walls, and its flange inside the lid's edge
edge = min(boot.x + gap + usb_inset, L - boot.x + gap, boot.y + gap, W - boot.y + gap);  // to the walls' inside
btn_d = floor(min(B[13], 2 * (edge - 0.3) - 0.4, 2 * (edge + wall) - 3) * 2) / 2;
assert(btn_d >= 4.5, "no room for a BOOT button there");

USB_SLOT_W = USB_W + 3.6;  // the plug, and a little of the overmold that comes with it
USB_SLOT_Z0 = board_top - 0.9;  // the connector's shell reaches below the board's top face too
USB_SLOT_Z1 = board_top + USB_H + 0.5;

groove_d = snap + 0.35;
groove_z = inner_h - 3.2;
lid_top = inner_h + lid_t;

// ------------------------------------------------------------------------------------------- shapes ---
module rbox(p0, p1, r = 1.5) {
  s = p1 - p0;
  translate([p0.x + r, p0.y + r, p0.z]) linear_extrude(s.z) offset(r) square([s.x - 2 * r, s.y - 2 * r]);
}

// the USB-C's opening: through the wall, and on to the board (it sits usb_inset back). dz: the caller's frame
module usb_slot(x0, x1, dz = 0) {
  translate([x0, W / 2 - USB_SLOT_W / 2, USB_SLOT_Z0 - dz]) cube([x1 - x0, USB_SLOT_W, USB_SLOT_Z1 - USB_SLOT_Z0]);
}

// a 45 degree ridge (or groove) along x, centered on c, reaching d either side of it
module ridge(c, len, d) {
  translate(c) rotate([0, 90, 0]) linear_extrude(len, center = true) rotate(45) square(d * sqrt(2), center = true);
}

// "lilbuddy", as the outlines of Arial Bold, flattened and scaled to an x-height of 4: the browser's OpenSCAD has no
// fonts, and a real face beats any stroke alphabet. Per letter: the character, its advance, its outlines, its counters.
FONT = [
  ["l", 2.14, [[[0.55,0],[0.55,5.52],[1.61,5.52],[1.61,0],[0.55,0]]], []],
  ["i", 2.14, [[[0.55,4.54],[0.55,5.52],[1.61,5.52],[1.61,4.54],[0.55,4.54]],[[0.55,0],[0.55,4],[1.61,4],[1.61,0],[0.55,0]]], []],
  ["b", 4.71, [[[0.51,0],[0.51,5.52],[1.57,5.52],[1.57,3.53],[1.82,3.78],[2.1,3.95],[2.4,4.06],[2.73,4.09],[3.11,3.96],[3.51,3.58],[3.95,2.94],[4.41,2.41],[4.41,2.54],[4.41,2.41],[4.41,2.04],[4.31,1.52],[4,1],[3.47,0.46],[3,-0.09],[3.09,-0.09],[3,-0.09],[2.74,-0.09],[2.54,-0.05],[2.26,0.08],[1.91,0.29],[1.58,0.46],[1.61,0.42],[1.58,0.46],[1.49,0.59],[1.49,0],[0.51,0]]], [[[1.56,2.09],[1.57,1.8],[1.61,1.55],[1.67,1.34],[1.76,1.17],[1.9,0.98],[2.08,0.85],[2.27,0.77],[2.49,0.75],[2.67,0.82],[2.87,1.06],[3.09,1.44],[3.33,1.75],[3.33,1.67],[3.33,1.75],[3.33,1.99],[3.27,2.33],[3.11,2.65],[2.83,2.97],[2.59,3.28],[2.64,3.28],[2.59,3.28],[2.44,3.28],[2.25,3.21],[2.03,2.98],[1.8,2.61],[1.56,2.31],[1.56,2.39],[1.56,2.31],[1.56,2.09]]]],
  ["u", 4.71, [[[3.19,0],[3.19,0.6],[3.02,0.44],[2.75,0.27],[2.36,0.09],[2.01,-0.09],[2.06,-0.09],[2.01,-0.09],[1.86,-0.09],[1.63,0.01],[1.33,0.3],[0.96,0.79],[0.64,1.02],[0.67,0.87],[0.64,1.02],[0.53,1.26],[0.53,1.19],[0.53,1.26],[0.53,1.47],[0.53,4],[1.59,4],[1.59,2.16],[1.63,1.76],[1.76,1.38],[1.96,1.03],[2.05,0.8],[1.98,0.83],[2.05,0.8],[2.16,0.72],[2.13,0.72],[2.16,0.72],[2.25,0.72],[2.41,0.82],[2.6,1.11],[2.84,1.61],[3.05,1.83],[3.03,1.66],[3.05,1.83],[3.11,1.99],[3.11,1.88],[3.11,1.99],[3.11,2.31],[3.11,4],[4.17,4],[4.17,0],[3.19,0]]], []],
  ["d", 4.71, [[[4.22,0],[3.24,0],[3.24,0.59],[3.07,0.42],[2.8,0.25],[2.44,0.08],[2.12,-0.09],[2.16,-0.09],[2.12,-0.09],[1.99,-0.09],[1.63,0.04],[1.23,0.44],[0.79,1.09],[0.32,1.64],[0.32,1.52],[0.32,1.64],[0.32,2.02],[0.42,2.53],[0.74,3.04],[1.27,3.56],[1.73,4.09],[1.64,4.09],[1.73,4.09],[2,4.09],[2.33,4.06],[2.63,3.95],[2.91,3.78],[3.16,3.53],[3.16,5.52],[4.22,5.52],[4.22,0]]], [[[1.4,2.09],[1.41,1.79],[1.44,1.53],[1.5,1.33],[1.57,1.16],[1.72,0.98],[1.88,0.85],[2.07,0.77],[2.29,0.75],[2.48,0.82],[2.69,1.06],[2.92,1.44],[3.17,1.75],[3.17,1.68],[3.17,1.75],[3.17,1.98],[3.11,2.32],[2.95,2.65],[2.67,2.97],[2.43,3.28],[2.48,3.28],[2.43,3.28],[2.28,3.28],[2.09,3.21],[1.87,2.98],[1.64,2.61],[1.4,2.31],[1.4,2.38],[1.4,2.31],[1.4,2.09]]]],
  ["y", 4.29, [[[0.05,4],[1.18,4],[2.14,1.16],[3.07,4],[4.17,4],[2.75,0.15],[2.5,-0.55],[2.35,-0.74],[2.06,-0.99],[1.61,-1.28],[1.42,-1.49],[1.56,-1.44],[1.42,-1.49],[1.29,-1.58],[1.39,-1.56],[1.29,-1.58],[1.11,-1.62],[1.15,-1.62],[1.11,-1.62],[1,-1.62],[0.85,-1.62],[0.7,-1.61],[0.55,-1.59],[0.4,-1.56],[0.31,-0.73],[0.43,-0.75],[0.55,-0.77],[0.65,-0.78],[0.76,-0.78],[0.95,-0.73],[1.15,-0.59],[1.36,-0.35],[1.54,-0.14],[1.53,-0.18],[1.54,-0.14],[1.57,-0.01],[0.05,4]]], []],
];

WORD = "lilbuddy";
function letter(c) = [for (g = FONT) if (g[0] == c) g][0];
function letter_adv(c) = letter(c)[1];
function at(i) = i == 0 ? 0 : at(i - 1) + letter_adv(WORD[i - 1]);
word_len = at(len(WORD) - 1) + letter_adv(WORD[len(WORD) - 1]);  // in the letters' own units (x-height 4)
word_w = word_len * name_h / 4;
word_h = 6.6 * name_h / 4;  // (ascender to descender: about 6.6 units)

module letter_2d(c) {
  difference() {
    for (o = letter(c)[2]) polygon(o);
    for (h = letter(c)[3]) polygon(h);
  }
}

module name_shape() {  // on the baseline at y = 0, from x = 0
  for (i = [0 : len(WORD) - 1]) translate([at(i), 0]) letter_2d(WORD[i]);
}

// cut into the lid's top face: the lid prints top face down, so this is a recess in the bed's side, and reads from above.
// Across the lid if the word fits beside the button; turned along it if not (a small case)
module name_cut() {
  free_x = k1.x - (boot.x + (btn_d + 2) / 2 + 2.5) - 1.5;
  along = free_x >= word_w + 2.5;
  cx = along ? (boot.x + (btn_d + 2) / 2 + 2.5 + k1.x - 1.5) / 2 : k1.x - wall - 1.5 - word_h / 2;
  cy = (k0.y + k1.y) / 2;
  translate([cx, cy, lid_t - 0.6]) {
    if (along) translate([-word_w / 2, -word_h / 2 + 1.1]) scale(name_h / 4) linear_extrude(1.6 / (name_h / 4)) name_shape();
    else rotate([0, 0, 90]) translate([-word_w / 2, -word_h / 2 + 1.1]) scale(name_h / 4) linear_extrude(1.6 / (name_h / 4)) name_shape();
  }
}


// -------------------------------------------------------------------------------------------- base ---
module base() {
  difference() {
    union() {
      difference() {
        rbox([k0.x - wall, k0.y - wall, -floor_t], [k1.x + wall, k1.y + wall, inner_h], 3);
        rbox([k0.x, k0.y, 0], [k1.x, k1.y, inner_h + 1], 1.2);
      }
      // the shelf the board rests on, 2 wide around the inside (thin enough to clear the battery, if there is one)
      difference() {
        rbox([k0.x, k0.y, UNDER - SHELF_T], [k1.x, k1.y, UNDER], 1.2);
        rbox([k0.x + 2, k0.y + 2, UNDER - SHELF_T - 1], [k1.x - 2, k1.y - 2, UNDER + 1], 1);
        if (battery) translate([k0.x - 1, W / 2 - 3, UNDER - SHELF_T - 1]) cube([4.5, 6, SHELF_T + 1.2]);  // (leads)
        // room under the USB-C's shell, which reaches below the board's top face at its end
        translate([k0.x - 1, W / 2 - USB_W / 2 - 1, UNDER - SHELF_T - 1]) cube([4.5, USB_W + 2, SHELF_T + 1.2]);
      }
    }
    // the snap grooves, in the outside of both long walls (the lid's skirt clicks into them)
    for (y = [k0.y - wall, k1.y + wall]) ridge([(k0.x + k1.x) / 2, y, groove_z], (k1.x - k0.x) * 0.55, groove_d);
    // the USB-C, through its end (a tunnel, to the board)
    usb_slot(k0.x - wall - 1, k0.x + 1);
  }
}

// --------------------------------------------------------------------------------------------- lid ---
module lid() {
  translate([0, 0, inner_h]) difference() {
    union() {
      difference() {
        union() {
          rbox([k0.x - wall - 0.3, k0.y - wall - 0.3, 0], [k1.x + wall + 0.3, k1.y + wall + 0.3, lid_t], 3);  // plate
          difference() {  // the skirt, outside the walls, its bottom edge chamfered to start over them
            rbox([k0.x - wall - 0.3, k0.y - wall - 0.3, -4.2], [k1.x + wall + 0.3, k1.y + wall + 0.3, 0], 3);
            rbox([k0.x - wall + fit, k0.y - wall + fit, -4.2 - 1], [k1.x + wall - fit, k1.y + wall - fit, 1], 2.6);
            translate([k0.x - wall - 0.4, k0.y - wall - 0.4, -4.2 - 1])
              rbox([k0.x - wall + fit + 0.8, k0.y - wall + fit + 0.8, -4.2 - 1], [k1.x + wall - fit - 0.8, k1.y + wall - fit - 0.8, -4.2 + 0.8], 2.6);
          }
        }
        // ribs, down to just over the board's top (they hold it down on its shelf)
        for (y = [b0.y + 0.6, b1.y - 0.6]) translate([b0.x + 2.5, y - 1.6, -rib]) cube([L - 5, 3.2, rib]);
      }
      // the ridges the grooves take, on the skirt's inside
      for (y = [k0.y - wall + fit, k1.y + wall - fit]) intersection() {
        ridge([(k0.x + k1.x) / 2, y, -2.6], (k1.x - k0.x) * 0.45, fit + snap);
        translate([k0.x - wall, y < W / 2 ? y - 5 : y, -4.2]) cube([k1.x - k0.x + 2 * wall, 5, 4.2]);
      }
    }
    // BOOT's hole (chamfered to take the button's lip); RESET's, for a paperclip
    translate([boot.x, boot.y, -1]) cylinder(d = btn_d, h = lid_t + 2);
    translate([boot.x, boot.y, -0.5]) cylinder(d1 = btn_d + 0.6, d2 = btn_d, h = 0.5);
    translate([reset.x, reset.y, -1]) cylinder(d = 2.5, h = lid_t + 2);
    translate([reset.x, reset.y, -1]) cylinder(d = 3.4, h = 0.5);
    // the same USB-C slot as the base's: the skirt wraps the walls, and would cover it (this frame is inner_h up)
    usb_slot(k0.x - wall - 1, k0.x + 2, inner_h);
    if (name) name_cut();
  }
}

// ------------------------------------------------------------------------------------------ button ---
// the flange on the lid, the neck through its hole, the lip under the plate (so it can't fall out, and doesn't need a
// press fit), and the stem onto the switch. Printed flange down: z 0 is the bed
FLANGE = 1.0;
NECK = 0.3;  // clearance each side, through the hole
LIP = 0.7;  // the lip's height, under the plate
LIP_OUT = 0.6;  // how much wider than the hole it is, in all (so it clips in and stays)
SLITS = 4;  // through the lip, so it can squeeze in

module button_body() {  // the neck, the lip and the stem: everything under the flange
  translate([boot.x, boot.y, 0]) {
    translate([0, 0, inner_h - eps]) cylinder(d = btn_d - 2 * NECK, h = lid_t + 2 * eps);  // the neck
    translate([0, 0, inner_h - LIP - 0.02]) {  // the lip, just under the plate
      difference() {
        union() {
          cylinder(d = btn_d + LIP_OUT, h = LIP);
          cylinder(d1 = btn_d - 2 * NECK, d2 = btn_d + LIP_OUT, h = 0.5);  // (the lead-in, under it)
        }
        translate([0, 0, -eps]) cylinder(d = btn_d - 2 * NECK - 1.8, h = LIP + 2 * eps);
        for (a = [0 : SLITS - 1]) rotate([0, 0, 360 * a / SLITS]) translate([-0.45, btn_d / 2 - 0.1, -eps])
          cube([0.9, 10, LIP + 2 * eps]);
      }
    }
    translate([0, 0, board_top + SW_H + 0.1]) cylinder(d = 2.4, h = inner_h - LIP - board_top - SW_H - 0.1);  // to just over the switch
  }
}

module button_case() {  // in the case's frame: its flange on the lid's top face
  translate([boot.x, boot.y, lid_top]) cylinder(d = btn_d + 3, h = FLANGE);  // on the lid, to press
  button_body();
}

// printed flange down: the same, flipped about the flange's top
module button_print() {
  translate([0, 0, lid_top + FLANGE]) mirror([0, 0, 1]) button_case();
}


// ----------------------------------------------------------------------------------------- preview ---
module stand_in(shrink = 0) {
  translate([shrink, shrink, UNDER + shrink]) color("#222") cube([L - 2 * shrink, W - 2 * shrink, PCB]);
  for (b = [boot, reset]) translate([b.x, b.y, board_top + SW_H / 2]) color("#666") cube([SW_D, SW_W, SW_H], center = true);
  color("#aaa") translate([-USB_OUT + 0.1, W / 2 - USB_W / 2 + 0.1, board_top + 0.1]) cube([USB_LEN + USB_OUT - 0.1, USB_W - 0.2, USB_H - 0.2]);
  if (battery) translate([k0.x + 0.4, k0.y + 0.4, 0.4]) color("silver") cube([min(bat.x, L + 2 * gap - 0.8), min(bat.y, W + 2 * gap - 0.8), bat.z]);
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
  if (part == "preview") {
    color(BASE_COLOR) base();
    stand_in();
    color(LID_COLOR) lid();
    color(LID_COLOR) button_case();
  }
  if (part == "view_lid") color(LID_COLOR) lid();
  if (part == "view_button") color(LID_COLOR) button_case();
  if (part == "view_board") stand_in();
  if (part == "view_battery") if (battery) translate([k0.x + 0.4, k0.y + 0.4, 0.4]) color("silver") cube([min(bat.x, L + 2 * gap - 0.8), min(bat.y, W + 2 * gap - 0.8), bat.z]);
  if (part == "fit") intersection() {  // (nothing: the case must not touch the board)
    union() {
      base();
      lid();
      button_case();
    }
    stand_in(0.08);
  }
  if (part == "fit_button") intersection() {  // (nothing: everything under the flange, and the lid)
    lid();
    button_body();
  }
  if (part == "fit_usb") intersection() {  // (nothing: the USB-C plug's way in)
    union() {
      base();
      lid();
    }
    translate([k0.x - wall - 0.5, W / 2 - USB_W / 2, board_top - 0.8]) cube([wall + usb_inset + gap + 1, USB_W, USB_H + 0.8]);
  }
}

// the model refuses what can't work
assert(!battery || (bat.x <= L + 2 * gap - 0.4 && bat.y <= W + 2 * gap - 0.4), "that battery is too big for this board's case");
assert(!battery || bat.z > 0, "the battery needs a thickness");
assert(boot.x > 0 && boot.x < L && boot.y > 0 && boot.y < W, "BOOT isn't on the board");
assert(reset.x > 0 && reset.x < L && reset.y > 0 && reset.y < W, "RESET isn't on the board");
// the button's cap must not reach RESET's switch (4.4 square: 2.2 from its centre) or hang over the case's edge
assert(norm(boot - reset) >= (btn_d + 3) / 2 + 2.2 + 0.3, "the BOOT button's cap would reach RESET's switch");
