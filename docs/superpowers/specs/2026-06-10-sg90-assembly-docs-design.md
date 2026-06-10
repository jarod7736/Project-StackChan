# SG90 Stack-chan — Illustrated Assembly Docs & Images

**Date:** 2026-06-10
**Status:** Built

## Goal

Produce an illustrated, picture-by-picture assembly guide for the **SG90 / Takao**
Stack-chan build, rendered from the actual 3D models, to complement the existing
text-only [`hardware/enclosure/ASSEMBLY.md`](../../../hardware/enclosure/ASSEMBLY.md).

## Decisions (from brainstorming)

- **Depth:** full illustrated guide — a hero shot, an exploded view, a per-part
  catalogue, and one render for each of the 6 assembly steps (~14 images).
- **Stand-ins:** the SG90 servos and the CoreS3 are not STL parts, so model
  simple proxies for them and include them in the renders so fitment is visible.
- **Output:** a *new* doc (`ASSEMBLY-ILLUSTRATED.md`); leave the hand-written
  `ASSEMBLY.md` intact and cross-link the two.

## Approach

Rendering uses **OpenSCAD headless** (the only renderer available on the box;
confirmed it exports PNG with no display, ~0.16 s per STL). One parameterized
scene file imports the real Takao STLs and adds the proxies; a `stage` variable
selects what's drawn; a shell script drives OpenSCAD once per view.

Why this over alternatives: the STLs are the source of truth, OpenSCAD is already
present, renders are near-instant, and committing the generator means the whole
image set regenerates with one command after any tweak.

### Coordinate findings (measured by probing each STL)

The four STLs are each exported in their **own frame** — they do not share a
common origin:

- `shell` & `feet` share X-centre 0 and Y-centre 32; **shell Z origin ≈ tilt axis**.
- `bracket` and `hat` are offset in Y (centres ~16) and must be shifted +15.5 Y
  to nest into the head.
- The head must be **raised onto a short neck** (`SHELL_DZ`) so its front aperture
  clears the feet; otherwise the base crosses the display in the head-on view.

These offsets live as named constants at the top of the scene so they are easy to
re-tune.

## Components

| File | Purpose |
|------|---------|
| `hardware/enclosure/scene/assembly.scad` | STL imports + SG90/CoreS3 proxies; `stage` switch + assembly-stack constants |
| `hardware/enclosure/tools/render-assembly.sh` | Renders all ~14 PNGs headless into `images/`; STL set via `STL_DIR` env |
| `hardware/enclosure/images/*.png` | The rendered guide images |
| `hardware/enclosure/ASSEMBLY-ILLUSTRATED.md` | The illustrated doc embedding the images |
| `hardware/enclosure/ASSEMBLY.md` | Existing text guide — unchanged except a link banner |

## Constraints / notes

- The upstream Takao STLs are **not redistributed** in this repo (third-party,
  Takao Akaki / `mongonta0716`). `STL_DIR` points at a local clone; the committed
  PNGs are the durable artifact.
- The servo/CoreS3 proxies are **representative**, not exact CAD — sized to real
  SG90 / CoreS3 dimensions and placed to read clearly, not for interference checks.

## Out of scope

- Animated / turntable renders, photoreal materials, or wiring-harness diagrams.
- The K151 / DYNAMIXEL XL330 design (incompatible; covered in `ASSEMBLY.md`).
