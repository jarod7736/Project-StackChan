// ===================================================================
//  Stack-chan SG90 (Takao) — illustrated assembly scene
//  Imports the real Takao STLs + simple SG90 / CoreS3 stand-ins and
//  poses them per assembly stage. Drive headless, one PNG per stage:
//    openscad -D 'stage="exploded"' --camera=... -o out.png assembly.scad
//  stages:
//    part_shell | part_bracket | part_feet | part_hat   (single printed part)
//    sg90 | cores3                                       (single stand-in)
//    exploded                                            (all parts pulled apart)
//    step1..step6                                        (build sequence)
// ===================================================================

// stl_dir points at the upstream Takao SG90 STL set (mongonta0716/3DPrinter_Models,
// "stackchan_sg90_case_takao_version"). Not redistributed here — clone it and
// override with -D, e.g.  openscad -D 'stl_dir="/path/to/set"' ...
// tools/render-assembly.sh passes this for you via the STL_DIR env var.
stl_dir = "/home/jarod7736/cloned/stackchan-sg90-models/stackchan_sg90_case_takao_version";
stage   = "exploded";
$fn     = 56;

// ---- palette ----------------------------------------------------------
C_SHELL  = "#9fb8d6";   // head shell (resin grey-blue)
C_BRK    = "#e8927c";   // servo bracket (salmon)
C_FEET   = "#8fd49a";   // base feet (green)
C_HAT    = "#e9d27e";   // cat-ear hat (khaki)
C_SERVO  = "#3a3f4a";   // SG90 body (dark)
C_HORN   = "#e8ecf2";   // servo horn (white)
C_CORE   = "#23262b";   // CoreS3 body (dark)
C_SCREEN = "#39c5d8";   // CoreS3 screen (cyan)

// ---- assembly stack (mm; desk plane at Z=0) --------------------------
// The four STLs are each exported in their own frame; these constants nest
// them into one coherent robot (measured by probing each part).
PAN_XY     = [0, 32];   // pan axis (vertical) at the feet centre hub
FEET_TOP   = 10.6;      // top of feet (pan-horn seat)
PAN_BASE   = 11;        // pan-servo body base, just above the feet
SHELL_DZ   = 17;        // raise the head onto the neck so it clears the feet
HEAD_PIVOT = 8 + SHELL_DZ;   // tilt axis (native shell pivot ~8 + raise)
CORE_Y     = 8;         // CoreS3 screen front plane (just ahead of bracket)
CORE_Z     = SHELL_DZ - 5;   // CoreS3 vertical seat inside the head
BRK_DY     = 15.5;      // bracket Y-shift to match the shell/feet frame

// ======================  PRINTED PARTS (STL)  =========================
module shell()   color(C_SHELL) import(str(stl_dir,"/stackchan_takao_shell_v2_resin.stl"));
module bracket() color(C_BRK)   import(str(stl_dir,"/stackchan_takao_bracket_v2.5.stl"));
module feet()    color(C_FEET)  import(str(stl_dir,"/stackchan_takao_feet.stl"));
module hat()     color(C_HAT)   import(str(stl_dir,"/stackchan_takao_hat_cat_CoreS3.stl"));

// ======================  STAND-INS  ===================================
// SG90 micro servo: body 22.8(L,Y) x 12.6(W,X) x 22.5(H,Z); flange at 15.9;
// output shaft offset ~5.9 toward one end. Built shaft-up at local origin,
// body base at Z=0, shaft on +Z, centred on the OUTPUT SHAFT in X/Y.
module sg90(horn="round") {
    sx = 5.9;                       // shaft offset from body centre (+Y end)
    translate([0, -sx, 0]) {        // shift so shaft lands at local origin
        color(C_SERVO) {
            translate([-6.3, -11.4, 0]) cube([12.6, 22.8, 22.5]);     // body
            translate([-6.3, -16.1, 15.9]) cube([12.6, 32.2, 2.5]);   // mount flange
            translate([0, sx, 22.5]) cylinder(d=4.9, h=4.2);          // boss + shaft
        }
        // wires out the back (-Y)
        for (i=[-1:1]) color(i<0?"#d98a3a":i==0?"#cc3b3b":"#7a4b2b")
            translate([i*2.2, -11.4, 5]) rotate([90,0,0]) cylinder(d=1.4, h=7);
    }
    // horn sits on the shaft (local origin), centred
    color(C_HORN) translate([0,0,26.7]) horn_round();
}
module horn_round() {
    difference() {
        union() { cylinder(d=18, h=1.6); cylinder(d=7, h=3.2); }
        for (a=[0:90:359]) rotate(a) translate([7,0,-0.1]) cylinder(d=1.4, h=2);
        translate([0,0,-0.1]) cylinder(d=2.4, h=4);
    }
}

// M5Stack CoreS3: ~36.5 x 36.5 x 15.5; display on the FRONT face (-Y).
// Built with the front (screen) face on the Y=0 plane, centred X, base Z=0.
module cores3() {
    w=36.5; h=36.5; d=15.5;
    translate([-w/2, 0, 0]) {
        color(C_CORE) cube([w, d, h]);                                // body
        color(C_SCREEN) translate([3, -0.6, 3.2]) cube([w-6, 1.0, h-6.4]); // screen
    }
}

// ======================  PLACEMENTS  =================================
// pan servo: vertical, shaft DOWN into feet hub at PAN_XY
module pan_servo(z=0)
    translate([PAN_XY[0], PAN_XY[1], PAN_BASE + 22.5 + z])
        rotate([180,0,0]) sg90();          // flip so shaft + horn point -Z to feet

// tilt servo: horizontal, body in bracket pocket, shaft -> +X to head pivot
module tilt_servo(dx=0)
    translate([-8 + dx, PAN_XY[1], HEAD_PIVOT])
        rotate([0,90,0]) sg90();           // local +Z (shaft) -> +X

module place_bracket(z=0)
    translate([0, BRK_DY, z]) bracket();   // nest the bracket into the head frame

module place_shell(z=0)
    translate([0, 0, SHELL_DZ + z]) shell();   // raise the head onto the neck

module place_cores3(y=0, z=0)
    translate([0, CORE_Y + y, CORE_Z + z]) cores3();  // screen in the head front

module place_hat(z=0)
    translate([0, 16, (SHELL_DZ - 11.5) + z]) hat();  // seat the hat on the head top

// ======================  STAGE SELECTOR  =============================
if (stage=="part_shell")        shell();
else if (stage=="part_bracket") bracket();
else if (stage=="part_feet")    feet();
else if (stage=="part_hat")     hat();
else if (stage=="sg90")         sg90();
else if (stage=="cores3")       cores3();

else if (stage=="exploded") {
    feet();
    pan_servo(z=24);
    place_bracket(52);
    tilt_servo(dx=34);
    place_shell(z=104);
    place_cores3(y=-46, z=104);
    place_hat(z=92);
}

// build sequence -------------------------------------------------------
else if (stage=="step1") { // centre the servos (show the two SG90s alone)
    translate([-18,32,0]) sg90();
    translate([ 18,32,0]) sg90();
}
else if (stage=="step2") { feet(); pan_servo(); }            // pan servo -> feet
else if (stage=="step3") { feet(); pan_servo(); place_bracket(); tilt_servo(); } // tilt -> bracket
else if (stage=="step4") { feet(); pan_servo(); place_bracket(); tilt_servo(); place_shell(); } // head on
else if (stage=="step5") { feet(); pan_servo(); place_bracket(); tilt_servo(); place_shell(); place_cores3(); } // CoreS3 in
else if (stage=="step6") { feet(); pan_servo(); place_bracket(); tilt_servo(); place_shell(); place_cores3(); place_hat(); } // hat
