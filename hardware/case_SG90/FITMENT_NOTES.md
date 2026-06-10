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
