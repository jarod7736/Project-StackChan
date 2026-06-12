#!/usr/bin/env python3
"""Generate the MG90S2 ("taller bracket") part set from the vendored stack-chan
v1.0 SG90 case parts.

Why this exists: MG90S (user-measured: 24L x 12.5W body; 20 below-tab + 3 tab +
1 shelf + 6 head + 4 spline) cannot fit the stock bracket — see FITMENT_NOTES.md
and analysis/mg90s_no_fit.png. The fix authorized 2026-06-12: grow the bracket
+7.55mm along Z (pan axis) in two prismatic stretch zones, rework both servo
slots, pass the tilt body through the left wall into a shroud that re-roots the
left head-pivot boss, and relieve the feet horn recess for the longer spline.
The shell is untouched: its three 8mm-pitch pivot holes absorb the height change
(mount one hole lower than before).

Inputs : bracket_SG90.step, feet_SG90.step  (originals, untouched)
Outputs: bracket_MG90S2.step, bracket_MG90S2_f.stl, bracket_MG90S2_b.stl,
         feet_MG90S2.step, feet_MG90S2.stl

Run with the CAD venv (pyenv 3.11 + build123d, see memory/cad-editing-toolchain):
    python make_mg90s2.py
"""
import os
import sys
from build123d import (
    import_step, export_step, export_stl, Box, Pos, Cylinder, Compound,
)

HERE = os.path.dirname(os.path.abspath(__file__))
BIG = 500.0

# ---------------------------------------------------------------- MG90S dims
# (user's dimension drawing, 2026-06-12)
TAB_T = 3.0        # mounting tab thickness
BELOW_TAB = 20.0   # case bottom -> tab underside (motor section)
HEAD_H = 7.0       # tab head-side face -> head/case top (1 shelf + 6 drum)
SPLINE = 4.0       # boss+spline beyond head top
BODY_L = 24.0      # body length
BODY_W = 12.5      # body width
TAB_SPAN = 34.0
TAB_W = 6.2        # tab width (across the short axis), generous
SHAFT_OFF = 6.0    # shaft axis to nearest body end face (nominal; window has
                   # +/-0.5 slack so 5.5..6.5 clones still register on the horn)

SLOT = 3.15        # slot width for the 3.0 tab
WIN_L = 25.0       # body window length  (24.0 body + offset-tolerance slack)
WIN_W = 12.8       # body window width   (12.5 body + 0.3)

# ------------------------------------------------------- measured SG90 frame
# pan: shaft -Z at (X -0.73, Y -4.7); slot Z 3.27..5.95; pocket floor -0.83;
#      skin -2.79..-0.83; window X[-6.92,5.46] Y[-10.77,12.31]
# tilt: shaft +X at (Y -4.7, Z 28.15); slot X 6.88..9.56; pocket to 13.66;
#      skin 13.66..15.62; window Z[21.92,34.30]; left wall X[-10.88,-8.88];
#      pivot boss rod ~Ø5.2 on the tilt axis, X -24.74..approx -12
PAN_AX_X, PAN_AX_Y = -0.73, -4.7
SKIN_TOP = -0.83
TILT_SKIN_IN = 13.66
TILT_FLOOR_OLD = 21.92
TILT_AX_Z_OLD = 28.15

# ------------------------------------------------------------- derived plan
PAN_FLANGE = SKIN_TOP + HEAD_H            # 6.17 pan slot floor (final)
S1 = PAN_FLANGE - 3.27                    # +2.90 stretch in pocket zone
PAN_TOP = PAN_FLANGE + TAB_T + BELOW_TAB  # 29.17 pan body top (final)
TILT_FLOOR = PAN_TOP + 0.30               # 29.47 tilt cavity floor (final)
S2 = TILT_FLOOR - TILT_FLOOR_OLD - S1     # +4.65 stretch in channel zone
S_TOTAL = S1 + S2                         # +7.55
TILT_AX_Z = TILT_AX_Z_OLD + S_TOTAL       # 35.70

PAN_WIN_X = (PAN_AX_X - WIN_W / 2, PAN_AX_X + WIN_W / 2)      # (-7.13, 5.67)
WIN_Y = (PAN_AX_Y - SHAFT_OFF - (WIN_L - BODY_L) / 2,         # (-11.20, 13.80)
         PAN_AX_Y - SHAFT_OFF - (WIN_L - BODY_L) / 2 + WIN_L)
PAN_SLOT = (PAN_FLANGE, PAN_FLANGE + SLOT)                    # (6.17, 9.32)

TILT_FLANGE = TILT_SKIN_IN - HEAD_H                           # 6.66
TILT_SLOT = (TILT_FLANGE - SLOT, TILT_FLANGE)                 # (3.51, 6.66)
TILT_BODY_END = TILT_FLANGE - TAB_T - BELOW_TAB               # -16.34
TILT_WIN_Z = (TILT_FLOOR, TILT_FLOOR + WIN_W)                 # (29.47, 42.27)

# shroud around the protruding tilt body (re-roots the pivot boss)
SHR_X = (TILT_BODY_END - 1.86, -10.88)    # (-18.20, -10.88) outer box
SHR_VOID_X = (TILT_BODY_END - 0.41, -10.7)  # (-16.75, ...) servo passage
SHR_Y = (WIN_Y[0] - 1.6, WIN_Y[1] + 1.6)
SHR_Z = (TILT_FLOOR - 1.57, TILT_WIN_Z[1] + 1.33)             # (27.90, 43.60)


def box(x0, x1, y0, y1, z0, z1):
    return Pos((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2) * Box(
        x1 - x0, y1 - y0, z1 - z0)


def shp(x):
    """Coerce boolean results (ShapeList etc.) to a single Shape/Compound."""
    if hasattr(x, "volume"):
        return x
    return Compound(list(x))


def cut(solid, tool, label, min_dv=0.5):
    v0 = solid.volume
    out = shp(solid - tool)
    dv = v0 - out.volume
    assert dv > min_dv, f"{label}: cut removed only {dv:.3f}mm^3 (silent no-op?)"
    print(f"  {label}: -{dv:.1f}mm^3")
    return out


def fuse(solid, tool, label, min_dv=0.5):
    v0 = solid.volume
    out = shp(solid + tool)
    dv = out.volume - v0
    assert dv > min_dv, f"{label}: fuse added only {dv:.3f}mm^3"
    print(f"  {label}: +{dv:.1f}mm^3")
    return out


def stretch(solid, zc, s, label):
    """Insert +s of material at the prismatic plane z=zc (translate the upper
    part up by s and fill the gap by extruding the cross-section)."""
    from build123d import extrude, Plane
    v0 = solid.volume
    lower = shp(solid & box(-BIG, BIG, -BIG, BIG, -BIG, zc + 0.015))
    upper = shp(solid & box(-BIG, BIG, -BIG, BIG, zc, BIG))
    slab = shp(solid & box(-BIG, BIG, -BIG, BIG, zc - 0.01, zc + 0.01))
    secs = [f for f in slab.faces()
            if f.geom_type.name == "PLANE"
            and abs(f.normal_at(None).Z - 1) < 1e-6
            and abs(f.center().Z - (zc + 0.01)) < 1e-3]
    assert secs, f"{label}: no cross-section faces at z={zc}"
    area = sum(f.area for f in secs)
    band = None
    for f in secs:
        e = extrude(f, amount=s + 0.02)  # top faces, normal +Z -> upward
        band = e if band is None else shp(band + e)
    out = shp(shp(lower + band) + Pos(0, 0, s) * upper)
    dv = out.volume - v0
    assert abs(dv - area * s) < area * 0.06 + 2.0, (
        f"{label}: dV {dv:.1f} != area*s {area * s:.1f}")
    out = out.clean()
    assert len(out.solids()) == 1, f"{label}: not a single solid after stretch"
    print(f"  {label}: +{dv:.1f}mm^3 (section {area:.1f}mm^2 x {s}mm)")
    return out


def main():
    print(f"plan: S1=+{S1:.2f} S2=+{S2:.2f} total=+{S_TOTAL:.2f}  "
          f"pan slot Z{PAN_SLOT}  tilt slot X{TILT_SLOT}  "
          f"tilt axis Z={TILT_AX_Z:.2f}")

    halves = import_step(os.path.join(HERE, "bracket_SG90.step")).solids()
    halves = sorted(halves, key=lambda s: s.center().Y)  # [front, back]
    names = ["front", "back"]
    out = []
    for half, name in zip(halves, names):
        print(f"-- {name} half")
        h = stretch(half, 11.0, S2, f"{name}: stretch channel zone (z=11)")
        h = stretch(h, 1.5, S1, f"{name}: stretch pocket zone (z=1.5)")

        # pan: window enlarge (pocket+slot+channel, punches tilt floor within
        # footprint like the stock design did)
        h = cut(h, box(*PAN_WIN_X, WIN_Y[0], 14.40, SKIN_TOP, TILT_FLOOR + 0.13),
                f"{name}: pan window", min_dv=0.0)
        # pan: open the full slot thickness along the tab wing path (the stock
        # wing recess is narrower/shallower than the MG90S 34mm-span 3mm tabs)
        h = cut(h, box(PAN_AX_X - TAB_W / 2 - 0.4, PAN_AX_X + TAB_W / 2 + 0.4,
                       -BIG, BIG, PAN_FLANGE + 0.03, PAN_SLOT[1]),
                f"{name}: pan slot wings+widen", min_dv=0.0)
        # pan: cable notch in front wall rides up with the stretch; extend it
        # so the lead (exits 2-6mm above the MG90S case bottom = Z 23-27) clears
        if name == "front":
            h = cut(h, box(-5.90, -2.36, -15.5, -10.7, 22.74, 27.5),
                    "front: pan cable notch extend", min_dv=1.0)

        # tilt: new slot plane (window part + tab wing band through walls)
        h = cut(h, box(*TILT_SLOT, WIN_Y[0], WIN_Y[1],
                       TILT_FLOOR + 0.03, TILT_WIN_Z[1]),
                f"{name}: tilt slot (window)", min_dv=0.0)
        h = cut(h, box(*TILT_SLOT, -BIG, BIG,
                       TILT_AX_Z - TAB_W / 2 - 0.4, TILT_AX_Z + TAB_W / 2 + 0.4),
                f"{name}: tilt slot (wings)", min_dv=0.0)
        # tilt: open old-slot web + enlarge window (Y walls, Z ceiling)
        h = cut(h, box(TILT_FLANGE - 0.04, TILT_SKIN_IN, WIN_Y[0], 14.40,
                       TILT_FLOOR + 0.03, TILT_WIN_Z[1]),
                f"{name}: tilt pocket merge", min_dv=0.0)
        h = cut(h, box(-8.93, TILT_FLANGE, WIN_Y[0], 14.40,
                       TILT_FLOOR + 0.03, TILT_WIN_Z[1]),
                f"{name}: tilt channel enlarge", min_dv=0.0)
        # tilt: passage through left wall (severs boss root; re-rooted below)
        h = cut(h, box(SHR_VOID_X[0], -8.8, WIN_Y[0], WIN_Y[1],
                       TILT_FLOOR - 0.02, TILT_WIN_Z[1] - 0.17),
                f"{name}: left wall passage", min_dv=0.0)

        # shroud piece for this half (C-shaped: open at the parting band)
        y0, y1 = (SHR_Y[0], 4.23) if name == "front" else (6.31, SHR_Y[1])
        shroud = box(SHR_X[0], SHR_X[1] + 0.2, y0, y1, *SHR_Z) - box(
            SHR_VOID_X[0], SHR_VOID_X[1] + 1.0, WIN_Y[0], WIN_Y[1],
            TILT_FLOOR - 0.02, TILT_WIN_Z[1] - 0.17)
        # tilt cable window through each side rib (lead exits near the case's
        # -X end on a +/-Y face; both sides opened so clone variants route)
        rib = (SHR_Y[0] - 0.1, WIN_Y[0] + 0.1) if name == "front" else (
            WIN_Y[1] - 0.1, SHR_Y[1] + 0.1)
        shroud = shroud - box(-16.6, -12.3, rib[0], rib[1],
                              TILT_AX_Z - 3.2, TILT_AX_Z + 3.2)
        h = fuse(h, shroud, f"{name}: shroud")
        h = h.clean()
        assert len(h.solids()) == 1, f"{name}: disjoint after shroud"
        out.append(h)
    front, back = out

    # boss survives: rod beyond the passage + embedded in the shroud plate
    boss_probe = shp(front & box(-24.0, SHR_VOID_X[0] - 0.2, PAN_AX_Y - 2,
                                 PAN_AX_Y + 2, TILT_AX_Z - 2, TILT_AX_Z + 2))
    assert boss_probe.volume > 80, f"pivot boss lost ({boss_probe.volume:.0f}mm^3)"
    print(f"  pivot boss remnant probe: {boss_probe.volume:.0f}mm^3 OK")

    # ------------------------------------------------ virtual servo fit test
    def pan_servo(shrink=0.0):
        s = shrink
        body = box(PAN_AX_X - BODY_W / 2 + s, PAN_AX_X + BODY_W / 2 - s,
                   PAN_AX_Y - SHAFT_OFF + s, PAN_AX_Y - SHAFT_OFF + BODY_L - s,
                   SKIN_TOP + s, PAN_TOP - s)
        tabs = box(PAN_AX_X - TAB_W / 2 + s, PAN_AX_X + TAB_W / 2 - s,
                   PAN_AX_Y - SHAFT_OFF - (TAB_SPAN - BODY_L) / 2 + s,
                   PAN_AX_Y - SHAFT_OFF + BODY_L + (TAB_SPAN - BODY_L) / 2 - s,
                   PAN_FLANGE + s, PAN_FLANGE + TAB_T - s)
        spline = Pos(PAN_AX_X, PAN_AX_Y, SKIN_TOP - SPLINE / 2) * Cylinder(
            2.4 - s, SPLINE)
        return shp(shp(body + tabs) + spline)

    def tilt_servo(shrink=0.0):
        s = shrink
        body = box(TILT_BODY_END + s, TILT_FLANGE - s,
                   PAN_AX_Y - SHAFT_OFF + s, PAN_AX_Y - SHAFT_OFF + BODY_L - s,
                   TILT_FLOOR + s, TILT_FLOOR + BODY_W - s)
        tabs = box(TILT_SLOT[0] + 0.15 + s, TILT_FLANGE - s,
                   PAN_AX_Y - SHAFT_OFF - (TAB_SPAN - BODY_L) / 2 + s,
                   PAN_AX_Y - SHAFT_OFF + BODY_L + (TAB_SPAN - BODY_L) / 2 - s,
                   TILT_AX_Z - TAB_W / 2 + s, TILT_AX_Z + TAB_W / 2 - s)
        head = box(TILT_FLANGE + s, TILT_SKIN_IN - s,
                   PAN_AX_Y - 6.25 + s, PAN_AX_Y + 6.25 - s,
                   TILT_AX_Z - 6.25 + s, TILT_AX_Z + 6.25 - s)
        from build123d import Rot
        spline = Pos(TILT_SKIN_IN + SPLINE / 2, PAN_AX_Y, TILT_AX_Z) * Rot(
            0, 90, 0) * Cylinder(2.4 - s, SPLINE)
        return shp(shp(shp(body + tabs) + head) + spline)

    pan, tilt = pan_servo(0.05), tilt_servo(0.05)
    def ivol(a, b):
        r = a.intersect(b)
        if r is None:
            return 0.0
        if hasattr(r, "volume"):
            return r.volume
        return sum(x.volume for x in r)

    for sv, sname in ((pan, "pan"), (tilt, "tilt")):
        for hh, hname in ((front, "front"), (back, "back")):
            inter = ivol(sv, hh)
            if inter >= 0.05:
                r = sv.intersect(hh)
                pieces = [r] if hasattr(r, "volume") else list(r)
                for p in pieces:
                    bb = p.bounding_box()
                    print(f"    HIT {p.volume:.2f}mm^3 at "
                          f"X {bb.min.X:.2f}..{bb.max.X:.2f} "
                          f"Y {bb.min.Y:.2f}..{bb.max.Y:.2f} "
                          f"Z {bb.min.Z:.2f}..{bb.max.Z:.2f}")
            assert inter < 0.05, f"{sname} servo hits {hname} half: {inter:.2f}mm^3"
    assert ivol(pan, tilt) < 0.01, "servos intersect each other"
    print(f"  virtual fit: pan & tilt MG90S clear both halves and each other OK")

    # ------------------------------------------------------------- feet relief
    feet = import_step(os.path.join(HERE, "feet_SG90.step")).solids()[0]
    feet2 = cut(feet, Pos(0, 0, 6.2) * Cylinder(2.7, 4.0),
                "feet: spline relief (Ø5.4 to Z4.2)")

    # ---------------------------------------------------------------- exports
    export_step(Compound([front, back]), os.path.join(HERE, "bracket_MG90S2.step"))
    export_stl(front, os.path.join(HERE, "bracket_MG90S2_f.stl"),
               tolerance=0.05, angular_tolerance=0.3)
    export_stl(back, os.path.join(HERE, "bracket_MG90S2_b.stl"),
               tolerance=0.05, angular_tolerance=0.3)
    export_step(feet2, os.path.join(HERE, "feet_MG90S2.step"))
    export_stl(feet2, os.path.join(HERE, "feet_MG90S2.stl"),
               tolerance=0.05, angular_tolerance=0.3)
    for n, s in (("front", front), ("back", back), ("feet", feet2)):
        bb = s.bounding_box()
        print(f"  {n}: vol={s.volume:.0f} bbox Z {bb.min.Z:.2f}..{bb.max.Z:.2f}")
    print("done")


if __name__ == "__main__":
    sys.exit(main())
