# SG90 head case — fitment notes + edit plan

## What's in this directory

Vendored copy of the **official stack-chan v1.0 SG90 case** (the editable source for
the head), pulled from
`stack-chan/stack-chan` @ branch `dev/v1.0`, path `case/case_SG90/`.

| File | What |
|---|---|
| `shell_SG90.step` / `.stl` | head shell (face housing) |
| `bracket_SG90.step`, `bracket_SG90_f.stl`, `bracket_SG90_b.stl` | **servo bracket — a SEPARATE part** (front + back halves) |
| `feet_SG90.step` / `.stl` | base interface (nests into our `../enclosure/pedestal-sg90` base) |
| `UPSTREAM_README.md` | upstream readme (print settings, assembly) |

**License/attribution:** upstream `stack-chan/stack-chan` is **Apache-2.0**. These files
are redistributed under that license; credit to the stack-chan project. Source:
https://github.com/stack-chan/stack-chan/tree/dev/v1.0/case/case_SG90

## The problem (assembly finding, 2026-06-10)

On the current (older/Takao) head the **servo mount is integrated into the head mesh**,
so the CoreS3 + battery and the servo mount fight for the same front space — they don't
both fit. A fixed STL can't be reshaped cleanly. **This v1.0 SG90 case is the way
forward because its servo bracket is a *separate part*** — you can move/modify the
bracket without touching the shell.

## Options (pick by effort)

**B — no CAD, fastest (do this first): move the battery to the rear.**
Mount the LiPo on the *back* of the head (printed clip or velcro). This frees the front
cavity for the CoreS3 alone AND counterweights the heavy face so the SG90 pitch servo
isn't fighting gravity (no droop / nose-dive — which is also why we clipped pitch-down to
-12 in firmware). Often enough on its own.

**A — CAD change (the real fix), in Fusion 360.** STEP imports as a B-rep solid *body*
with no timeline history, so you edit the body directly:
- **Shift the servo mount back:** open `bracket_SG90.step`, select the bracket body, use
  **Modify → Move/Copy** to translate it rearward (−Y / toward the back) by the clearance
  you need; re-check it still mates to the shell and feet. (Bracket is separate → low risk.)
- **Or deepen the shell:** open `shell_SG90.step`, **Modify → Press Pull** (or **Offset
  Face**) on the front/back face to add depth to the cavity.
- Move/Copy, Press Pull, Offset Face, Align, Combine all work on imported no-history
  bodies. Docs:
  - Solid modify tools: https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/SLD-MODIFY-SOLID-BODY.htm
  - Press Pull: https://help.autodesk.com/cloudhelp/ENU/Fusion-Model/files/GUID-02F9ADA3-7556-42A9-8AD1-552728D537AB.htm
  - (Imported bodies w/o history are listed as a Scale/Press-Pull use case in the Modify docs.)

## Target regardless of path

Get the **combined CoreS3 + battery center of mass roughly over the pitch servo shaft.**
That's what keeps the weak SG90 from straining and the head from nose-diving. "Battery to
rear" and "bracket back + battery rear" both aim at this; "deeper head" lets you place the
face so the CG lands on the pivot.

## Status

- Files vendored + this plan recorded (2026-06-10).
- The actual geometry edit is a Fusion task on your machine (no CAD tooling in the dev env).
- Electronics/firmware side is DONE and unaffected (see `2026-06-09-servo-bringup-handoff.md`).

## MG90S — DOES NOT FIT THIS BRACKET (2026-06-12, supersedes 2026-06-11)

The 2026-06-11 `bracket_MG90S` variant (slots translated 2mm along shaft axes) was
**wrong and has been deleted** (recoverable from git history, commit 2485899).
Physical fit failed; full geometric analysis of `bracket_SG90.step` shows **no slot
position works** — see `analysis/mg90s_no_fit.png`.

**Measured bracket facts** (coordinates of the vendored STEP):

- Pan servo (shaft −Z): flange slot Z 3.27–5.95 (2.68mm), head pocket 4.1mm deep
  (floor Z −0.83 = skin top), body channel X[−6.92, 5.46] × Y[−10.77, 12.31]
  (12.38 × 23.08). Body top pokes through a wall at Z 19.96–21.92.
- Tilt servo (shaft +X): mirror-image of pan: slot X 6.88–9.56, pocket to 13.66,
  skin to 15.62. **The tilt cavity floor (Z 21.92) is the pan cavity ceiling** —
  an SG90 pan body (15.7mm below tab) ends at Z 21.65, 0.27mm under the tilt servo.
- Pan shaft at (X −0.73, Y −4.7); tilt axis at (Y −4.7, Z 28.15).
- **Left head-pivot boss**: Ø5.2 tube on the tilt axis, X −25.5 to −12.87, with a
  screw bore. The shell's left side hinges on it. It cannot move (shell interface)
  and the tilt axis passes through any tilt servo's body.

**Why MG90S (user-measured drawing: 24L × 12.5W; 20 below-tab + 3 tab + 1 shelf +
6 head + 4 spline) cannot fit, in either position:**

- Pan: vertical budget from feet-horn plane (Z −0.83) to tilt cavity floor (21.92)
  is **22.75mm**; MG90S case needs **30mm** → crashes 7.25mm into the tilt servo.
- Tilt: horizontal budget from right skin (13.66) to left wall (−8.88) is
  **22.54mm** (26.5 if the wall is cut — but the pivot boss root starts there);
  same 30mm need → ≥3.5mm short, into the head's own hinge.
- Independent minor blockers: 3mm tabs vs 2.68 slots; 24 × 12.5 body vs
  23.08 × 12.38 channels.
- Slot translation trades horn position 1:1 and fixes nothing; the 06-11 edit also
  left the horns ~4mm out of design position.

**Max servo envelope this bracket accepts** (horn at design position; caliper any
candidate servo against this BEFORE buying/printing):

| dimension | max |
|---|---|
| body length × width | 23.0 × 12.3 |
| below-tab body length | 15.7 |
| tab thickness | 2.6 (3.0 possible with +0.5 slot-widen edit) |
| tab span | ≤32.5 (longer pokes out the open slot bands) |
| tab-underside → case top (shaft side) | 4.1 |
| spline | 21T, ~3.2mm engagement |

i.e. true SG90/MS18-envelope servos only. Anything taller below the tab or above
it does not fit, full stop.

**Paths to metal gears:** (a) find a metal-gear servo inside the envelope above
(verify with calipers — "MG90S" clone dimensions vary wildly and the common claim
that MG90S = SG90 size is false along the shaft axis); (b) redesign bracket + feet
+ pedestal around bigger servos — a new-body project, not a bracket edit (upstream's
RS30X/SCS0009 cases show the scale of that); (c) keep MS18-class and manage load in
firmware (current approach: pitch clipped to −12).

→ Path (b) was executed 2026-06-12 as the **MG90S2 taller-bracket set** below.

## MG90S2 — taller bracket set that DOES fit MG90S (2026-06-12)

User authorized growing the bracket. Generated by `make_mg90s2.py` (committed;
re-run it to regenerate after dimension tweaks). All booleans volume-asserted;
STLs watertight; **virtual MG90S pan+tilt solids verified non-intersecting with
both halves and each other.** Verification renders: `analysis/MG90S2_*.png`.

**Print/replace:** `bracket_MG90S2_f.stl` + `bracket_MG90S2_b.stl` (instead of
the SG90 bracket) and `feet_MG90S2.stl` (instead of `feet_SG90.stl`).
`shell_SG90` is UNCHANGED.

What changed vs stock (all derived from the user's MG90S drawing:
24L × 12.5W; 20 below-tab + 3 tab + 1+6 head + 4 spline; 34 tab span):

- **+7.55mm taller** via two prismatic stretches (+2.90 in the pan pocket zone →
  pocket depth exactly 7.0; +4.65 in the pan channel zone → tilt cavity floor
  at Z 29.47, clearing the pan body top 29.17 by 0.3).
- Pan slot now Z 6.17–9.32 (3.15 for the 3.0 tab); both body windows enlarged to
  25.0 × 12.8 (the 0.5 length slack tolerates shaft-offset clone variance
  5.5–6.5mm); tab wing paths opened for the 34mm span.
- Tilt slot moved inboard to X 3.51–6.66; the body passes through the left wall
  and protrudes 5.5mm into a **shroud box** which **re-roots the left pivot
  boss** — boss axis, screw end (X −24.74) and bearing length (covers the
  shell's |X| 19.9–26.1 hub) are unchanged.
- Pan + tilt horn/spline planes are at the stock design positions; cable
  notches extended (pan front wall) / added (shroud side ribs, both sides).
- Feet: Ø5.4 relief, 2mm deeper at recess center, for the 0.8mm-longer spline.

**Assembly notes:**

- The tilt axis is now at Z 35.70 (was 28.15). The shell has THREE pivot holes
  at 8mm pitch — **mount the shell one hole lower than before**; net face height
  change ≈ −0.45mm. Everything else about shell mounting is unchanged.
- No snap nubs in the new slots (stock nubs were SG90-specific): servos are held
  by slot clamping once the halves are screwed — normal for this design.
- Cosmetic: the 34mm tab spans poke 0.4–1.2mm out of the open slot bands; the
  tilt servo's motor end is enclosed by the new shroud on the left.
- First assembly: check the horn cups (feet + shell) accept the 0.8mm-extra
  spline depth; the feet relief covers the pan side, shell cup depth unverified.
