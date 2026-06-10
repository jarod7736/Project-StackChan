#!/usr/bin/env bash
# Render every image used by ASSEMBLY-ILLUSTRATED.md from scene/assembly.scad.
# Headless OpenSCAD (no display needed). Outputs PNGs to ../images/.
#
# Requires: openscad on PATH, and the upstream Takao SG90 STL set. Point STL_DIR
# at your clone of mongonta0716/3DPrinter_Models/stackchan_sg90_case_takao_version:
#   STL_DIR=~/cloned/stackchan-sg90-models/stackchan_sg90_case_takao_version \
#     hardware/enclosure/tools/render-assembly.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCENE="$HERE/../scene/assembly.scad"
OUT="$HERE/../images"
STL_DIR="${STL_DIR:-$HOME/cloned/stackchan-sg90-models/stackchan_sg90_case_takao_version}"
SCHEME="${SCHEME:-Tomorrow}"

if [ ! -f "$STL_DIR/stackchan_takao_shell_v2_resin.stl" ]; then
  echo "ERROR: STL set not found in '$STL_DIR'." >&2
  echo "Clone github.com/mongonta0716/3DPrinter_Models and set STL_DIR." >&2
  exit 1
fi
mkdir -p "$OUT"

# render <out-name> <stage> <W> <H> <camera-gimbal>
render() {
  echo "  -> $1.png"
  openscad -o "$OUT/$1.png" \
    -D "stl_dir=\"$STL_DIR\"" -D "stage=\"$2\"" \
    --imgsize="$3,$4" --colorscheme="$SCHEME" --camera="$5" \
    "$SCENE" 2>/dev/null
}

echo "Rendering Stack-chan SG90 assembly images -> $OUT"

# --- hero + exploded ---------------------------------------------------
render 00-hero       step6    640 720 "0,30,32,70,0,14,330"
render 01-exploded   exploded 640 800 "0,32,78,68,0,30,500"

# --- printed-part catalogue -------------------------------------------
render 10-part-feet     part_feet    600 600 "0,32,6.5,58,0,28,150"
render 11-part-bracket  part_bracket 600 600 "-6.8,16.5,19.5,62,0,40,150"
render 12-part-shell    part_shell   600 600 "0,32,9,60,0,35,160"
render 13-part-hat      part_hat     600 600 "0,15.7,62,70,0,30,140"

# --- stand-ins ---------------------------------------------------------
render 14-stand-sg90    sg90         600 600 "0,0,15,68,0,30,95"
render 15-stand-cores3  cores3       600 600 "0,8,18,66,0,40,120"

# --- build sequence ----------------------------------------------------
render 20-step1-center  step1 600 560 "0,32,15,60,0,30,180"
render 21-step2-pan     step2 600 600 "0,32,16,62,0,30,230"
render 22-step3-tilt    step3 600 620 "0,32,20,62,0,30,250"
render 23-step4-head    step4 600 680 "0,32,30,62,0,30,330"
render 24-step5-core    step5 600 700 "0,30,30,58,0,40,330"
render 25-step6-hat     step6 600 720 "0,30,34,58,0,32,340"

echo "Done. $(ls "$OUT"/*.png | wc -l) images in $OUT"
