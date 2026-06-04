// Stack-chan electronics pedestal  (v3 - two-part, removable bottom)
// Bolts UNDER the existing printed StackChan-Base; the base is the top lid.
// Houses a PCA9685 servo driver + wiring inside a regular-octagon body.
//
// TWO PARTS:
//   body  - regular-octagon shell, OPEN at the bottom, top plate carries the
//           base bolt pattern. 4 internal corner bosses take the bottom screws.
//   plate - removable bottom; the PCA9685 mounts to standoffs on it, then the
//           plate (with board) screws up into the body with 4x M2 screws.
// Set `part` to render/export "body", "plate", or "both" (assembled preview).
// Units: mm.  Origin = center, +Y = back, z=0 = body/plate joint.

part        = "both";   // "body" | "plate" | "both"

/* ---------- Base interface (measured from StackChan-Base.stl) ---------- */
base_w      = 48.0;
base_d      = 56.0;
base_cham   = 7.0;
holes       = [[-15.4,23.1],[15.4,23.0],[-15.8,-23.8],[15.8,-23.8]];

/* ---------- PCA9685 board + connectors (CONFIRM/measure yours) --------- */
board_along = "Y";      // "X" = long edge side-to-side, "Y" = long edge front-to-back
pca_l       = 62.5;     // PCB long edge
pca_w       = 25.4;     // PCB short edge
pin_over    = 8.0;      // pins/terminal overhang on the long end(s)
hdr_up      = 22.0;     // clearance above PCB (20mm board+plugged-servo stack + air)
pcb_t       = 1.6;
// Screw-down mounting (Adafruit PCA9685 pattern: 0.75" x 2.20" = 19.05 x 55.88 mm).
mount_dx    = 55.88;    // hole spacing along PCB long axis
mount_dy    = 19.05;    // hole spacing along PCB short axis
standoff_h  = 4.0;      // board sits this high off the plate
standoff_d  = 5.0;
standoff_pilot = 2.1;   // M2.5 self-tap pilot

/* ---------- Comfort clearances ---------- */
clr_long    = 6.0;
clr_short   = 9.0;

/* ---------- Shell ---------- */
wall        = 2.5;
top_t       = 3.0;      // top plate thickness
bot_t       = 2.5;      // bottom plate thickness
recess_d    = 2.0;      // base nests this deep into the top
boss_d      = 7.5;      // base-bolt boss (in top plate)
boss_pilot  = 1.7;      // M2 self-tap (base bolts)

/* ---------- Bottom-plate screws (M2) ---------- */
pscrew_xy   = 29.0;     // plate screw pos at the 4 diagonal corners (+-xy, +-xy)
pboss_d     = 7.0;      // body corner boss outer dia
pboss_h     = 12.0;     // body corner boss height
pscrew_pilot= 1.6;      // M2 self-tap into body boss
pscrew_clear= 2.4;      // M2 shaft clearance in the plate
pscrew_csk  = 4.4;      // countersink dia on plate underside

/* ---------- Power: panel-mount DC barrel jack in back (+Y) wall ---------- */
jack_hole_d = 8.0;
jack_z_off  = 4.0;
wire_slot_w = 12; wire_slot_h = 8;
$fn         = 72;

/* ---------- Derived sizes ---------- */
along_y    = (board_along == "Y");
long_need  = pca_l + 2*pin_over + 2*clr_long;
short_need = pca_w + 2*clr_short;
// Square footprint -> REGULAR octagon (all 8 sides equal); fits board either way.
sq    = max(long_need, short_need, base_w + 6, base_d + 6);
in_l  = sq; in_w = sq;                       // internal X/Y
in_h  = standoff_h + pcb_t + hdr_up;         // internal height (cavity)
out_l = in_l + 2*wall; out_w = in_w + 2*wall;
H     = in_h + top_t;                        // body height (open bottom, no floor)
jack_z = standoff_h + pcb_t + jack_z_off;    // jack center above the joint
function reg_cham(s) = s / (2 + sqrt(2));

pscrew_pts  = [[pscrew_xy,pscrew_xy],[-pscrew_xy,pscrew_xy],
               [pscrew_xy,-pscrew_xy],[-pscrew_xy,-pscrew_xy]];
m_x = along_y ? mount_dy/2 : mount_dx/2;
m_y = along_y ? mount_dx/2 : mount_dy/2;
mount_pts = [[m_x,m_y],[-m_x,m_y],[m_x,-m_y],[-m_x,-m_y]];

/* ---------- Helpers ---------- */
module octa(w,d,ch,o=0){
  polygon([[-w/2+o,d/2-ch+o],[-w/2+ch+o,d/2-o],[w/2-ch-o,d/2-o],[w/2-o,d/2-ch+o],
           [w/2-o,-d/2+ch-o],[w/2-ch-o,-d/2+o],[-w/2+ch+o,-d/2+o],[-w/2+o,-d/2+ch-o]]);
}

/* ====================  BODY (open bottom)  ==================== */
module body(){
  union(){
    difference(){
      linear_extrude(H) octa(out_l,out_w,reg_cham(out_l));                 // solid octagon
      translate([0,0,-0.1]) linear_extrude(in_h+0.1) octa(in_l,in_w,reg_cham(in_l)); // cavity, open bottom
      translate([0,0,H-recess_d]) linear_extrude(recess_d+0.2) octa(base_w,base_d,base_cham,-0.4); // base recess
      translate([0,out_w/2-wall/2, jack_z]) rotate([90,0,0]) cylinder(d=jack_hole_d,h=wall+2,center=true); // jack
      translate([-out_l/2-0.6,-wire_slot_w/2, 3]) cube([wall+1.2,wire_slot_w,wire_slot_h]);  // wire slot
      for(h=holes) translate([h[0],h[1],H-top_t-0.1]) cylinder(d=boss_pilot,h=top_t+0.2);    // base bolts
    }
    // 4 corner bosses for the bottom-plate screws (merge into the diagonal walls)
    for(p=pscrew_pts) translate([p[0],p[1],0]) difference(){
      cylinder(d=pboss_d,h=pboss_h);
      translate([0,0,-0.1]) cylinder(d=pscrew_pilot,h=pboss_h+0.2);
    }
  }
}

/* ====================  BOTTOM PLATE (removable)  ==================== */
// standoffs built from z=0 (placed on the plate top)
module standoffs(){
  for(p=mount_pts) translate([p[0],p[1],0]) difference(){
    cylinder(d=standoff_d,h=standoff_h);
    translate([0,0,-0.1]) cylinder(d=standoff_pilot,h=standoff_h+0.2);
  }
  pl2=pca_l/2; pw2=pca_w/2;   // anti-rotation nubs along one PCB edge
  if (along_y) for(sy=[-1,1]) translate([pw2,sy*pl2,0]) cube([1.5,sy>0?-1.5:1.5,standoff_h+pcb_t+1.5]);
  else         for(sx=[-1,1]) translate([sx*pl2,pw2,0]) cube([sx>0?-1.5:1.5,1.5,standoff_h+pcb_t+1.5]);
}
module plate(){            // z=0 = bottom (desk) face, standoffs on top
  difference(){
    linear_extrude(bot_t) octa(out_l,out_w,reg_cham(out_l));
    for(p=pscrew_pts){
      translate([p[0],p[1],-0.1]) cylinder(d=pscrew_clear,h=bot_t+0.2);          // shaft clearance
      translate([p[0],p[1],-0.01]) cylinder(d1=pscrew_csk,d2=pscrew_clear,h=1.8);// countersink on underside
    }
  }
  translate([0,0,bot_t]) standoffs();
}

/* ====================  PLACEMENT  ==================== */
if      (part=="body")  body();
else if (part=="plate") plate();
else { body(); translate([0,0,-bot_t]) plate(); }   // assembled preview

echo(str("BODY octagon ", out_l, " mm across x ", H, " mm tall (open bottom) | ",
         "PLATE ", out_l, " mm x ", bot_t, " mm | cavity h ", in_h, " mm"));
