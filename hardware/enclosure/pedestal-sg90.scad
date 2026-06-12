// Stack-chan SG90 electronics base  (two-part, removable bottom)
// A stationary base for the SG90 stack-chan: the printed FEET (feet_SG90M,
// 48 x 37 mm, measured from the vendored feet_SG90.step) nest in the top
// recess (rotationally keyed against pan torque) and screw down with 4x M2x8
// driven from inside the cavity; the PCA9685 + barrel jack live inside.
//
// PARTS: set `part` to "body" | "plate" | "both".
//   body  - regular-octagon shell, open bottom, top recess for the feet +
//           center wire pass-through + back wire slot under the feet's cable
//           slot. 4 internal M2 bosses for the plate, 4 feet-screw holes.
//   plate - removable bottom; PCA9685 mounts to its standoffs, then screws
//           up into the body with 4x M2.
// Units: mm.  Origin = center, +Y = back (front of bot faces -Y display side).
//
// FEET ORIENTATION: feet go in rotated 180 deg from their native STEP frame so
// their cable slot exits at the back (+Y, barrel-jack side). The pan-axis hub
// sits at the pedestal origin; the feet bbox center is 4.5 mm behind it, hence
// the asymmetric recess (which also keys the orientation).

part        = "both";

/* ---------- SG90 feet interface (measured from feet_SG90.step) ---------- */
feet_w      = 48.0;     // feet X footprint
feet_d      = 37.0;     // feet Y footprint
feet_clear  = 0.4;      // recess oversize so the printed feet drop in
feet_recess = 3.0;      // how deep the feet nest into the top (keys rotation)
feet_y_off  = 4.5;      // recess center offset behind the hub (feet rot 180)
feet_r      = 2.2;      // recess corner radius (feet corners r2.0 + clearance)
wire_hole_d = 22.0;     // center pass-through for servo cables / horn clearance
/* feet screws: M2x8 pan-head from inside the cavity, through the 2 mm recess
   floor, into 6.5 mm pilot holes in the feet pads (feet_SG90M variant) */
fscrew_pts  = [[12.5,18],[-12.5,18],[12.5,-10],[-12.5,-10]];
/* back wire slot through the recess floor, under the feet's 2 mm cable slot */
fslot_w     = 10.0;  fslot_y0 = 6.0;  fslot_y1 = 22.0;

/* ---------- PCA9685 board + connectors (CONFIRM/measure yours) --------- */
board_along = "Y";
pca_l       = 62.5;  pca_w = 25.4;
pin_over    = 8.0;   hdr_up = 22.0;  pcb_t = 1.6;
mount_dx    = 55.88; mount_dy = 19.05;        // Adafruit/clone hole pattern
standoff_h  = 4.0;   standoff_d = 5.0;  standoff_pilot = 2.1;  // M2.5

/* ---------- Comfort clearances ---------- */
clr_long    = 6.0;   clr_short = 9.0;

/* ---------- Shell ---------- */
/* top_t = recess depth + 2 mm floor (the feet sit on, and screw through, the
   floor; at top_t == feet_recess the recess would be a through-hole) */
wall = 2.5;  top_t = feet_recess + 2.0;  bot_t = 2.5;

/* ---------- Bottom-plate screws (M2) ---------- */
pscrew_xy = 29.0;  pboss_d = 7.0;  pboss_h = 12.0;
pscrew_pilot = 1.6;  pscrew_clear = 2.4;  pscrew_csk = 4.4;

/* ---------- Power: DC barrel jack in back (+Y) wall ---------- */
jack_hole_d = 8.0;  jack_z_off = 4.0;
wire_slot_w = 12;   wire_slot_h = 8;
$fn = 72;

/* ---------- Derived ---------- */
along_y    = (board_along == "Y");
long_need  = pca_l + 2*pin_over + 2*clr_long;
short_need = pca_w + 2*clr_short;
sq    = max(long_need, short_need, feet_w + 8, feet_d + 8);   // square -> regular octagon
in_l  = sq; in_w = sq;
in_h  = standoff_h + pcb_t + hdr_up;
out_l = in_l + 2*wall; out_w = in_w + 2*wall;
H     = in_h + top_t;
jack_z = standoff_h + pcb_t + jack_z_off;
function reg_cham(s) = s / (2 + sqrt(2));

pscrew_pts = [[pscrew_xy,pscrew_xy],[-pscrew_xy,pscrew_xy],
              [pscrew_xy,-pscrew_xy],[-pscrew_xy,-pscrew_xy]];
m_x = along_y ? mount_dy/2 : mount_dx/2;
m_y = along_y ? mount_dx/2 : mount_dy/2;
mount_pts = [[m_x,m_y],[-m_x,m_y],[m_x,-m_y],[-m_x,-m_y]];

/* ---------- Helpers ---------- */
module octa(w,d,ch,o=0){
  polygon([[-w/2+o,d/2-ch+o],[-w/2+ch+o,d/2-o],[w/2-ch-o,d/2-o],[w/2-o,d/2-ch+o],
           [w/2-o,-d/2+ch-o],[w/2-ch-o,-d/2+o],[-w/2+ch+o,-d/2+o],[-w/2+o,-d/2+ch-o]]);
}
module rrect(l,w,r){ hull() for(sx=[-1,1],sy=[-1,1]) translate([sx*(l/2-r),sy*(w/2-r)]) circle(r=r); }

/* ====================  BODY  ==================== */
module body(){
  union(){
    difference(){
      linear_extrude(H) octa(out_l,out_w,reg_cham(out_l));                          // octagon shell
      translate([0,0,-0.1]) linear_extrude(in_h+0.1) octa(in_l,in_w,reg_cham(in_l)); // cavity, open bottom
      // feet recess in the top (rounded rect; keys the feet against pan torque;
      // offset +Y so the pan-axis hub lands on the pedestal origin)
      translate([0,feet_y_off,H-feet_recess]) linear_extrude(feet_recess+0.2)
        rrect(feet_w+feet_clear, feet_d+feet_clear, feet_r);
      // center wire pass-through for servo cables / horn-screw driver access
      translate([0,0,H-top_t-0.1]) cylinder(d=wire_hole_d, h=top_t+0.2);
      // back wire slot through the recess floor: cables leave the feet's cable
      // slot at the back edge and drop straight into the cavity
      translate([-fslot_w/2,fslot_y0,H-top_t-0.1])
        cube([fslot_w,fslot_y1-fslot_y0,top_t+0.2]);
      // 4 clearance holes for the feet screws (M2x8 driven from inside)
      for(p=fscrew_pts) translate([p[0],p[1],H-top_t-0.1])
        cylinder(d=pscrew_clear,h=top_t+0.2);
      // barrel jack
      translate([0,out_w/2-wall/2,jack_z]) rotate([90,0,0]) cylinder(d=jack_hole_d,h=wall+2,center=true);
      // side wire slot (extra routing)
      translate([-out_l/2-0.6,-wire_slot_w/2,3]) cube([wall+1.2,wire_slot_w,wire_slot_h]);
    }
    // 4 corner bosses for the bottom-plate screws
    for(p=pscrew_pts) translate([p[0],p[1],0]) difference(){
      cylinder(d=pboss_d,h=pboss_h);
      translate([0,0,-0.1]) cylinder(d=pscrew_pilot,h=pboss_h+0.2);
    }
  }
}

/* ====================  BOTTOM PLATE  ==================== */
module standoffs(){
  for(p=mount_pts) translate([p[0],p[1],0]) difference(){
    cylinder(d=standoff_d,h=standoff_h);
    translate([0,0,-0.1]) cylinder(d=standoff_pilot,h=standoff_h+0.2);
  }
  pl2=pca_l/2; pw2=pca_w/2;
  if (along_y) for(sy=[-1,1]) translate([pw2,sy*pl2,0]) cube([1.5,sy>0?-1.5:1.5,standoff_h+pcb_t+1.5]);
  else         for(sx=[-1,1]) translate([sx*pl2,pw2,0]) cube([sx>0?-1.5:1.5,1.5,standoff_h+pcb_t+1.5]);
}
module plate(){
  difference(){
    linear_extrude(bot_t) octa(out_l,out_w,reg_cham(out_l));
    for(p=pscrew_pts){
      translate([p[0],p[1],-0.1]) cylinder(d=pscrew_clear,h=bot_t+0.2);
      translate([p[0],p[1],-0.01]) cylinder(d1=pscrew_csk,d2=pscrew_clear,h=1.8);
    }
  }
  translate([0,0,bot_t]) standoffs();
}

/* ====================  PLACEMENT  ==================== */
if      (part=="body")  body();
else if (part=="plate") plate();
else { body(); translate([0,0,-bot_t]) plate(); }

echo(str("SG90 BASE octagon ", out_l, " mm across x ", H, " mm | feet recess ",
         feet_w, "x", feet_d, " @ +Y", feet_y_off, " | floor ", top_t-feet_recess,
         " mm | cavity h ", in_h, " mm"));
