#!/usr/bin/env bash
# Fast serial capture for the brief window the app is alive after boot.
# Kernel log proves this board is HWCDC (303a:1001), always ttyACM0 — but we
# glob ttyACM* anyway as insurance against a re-enumeration under a new name.
# The instant a port appears, dump everything to a log until it vanishes
# (the SoC powers off -> HWCDC USB drops -> cat exits). Captures the boot
# sequence + whatever [TRIP]/[AXP]/panic precedes the power-off.
set -u
OUT=/tmp/stkchan_boot.log
: > "$OUT"
echo "fast-capture armed at $(date '+%H:%M:%S'); polling /dev/ttyACM* every 0.2s ..."
for i in $(seq 1 9000); do          # ~30 min
  PORT=$(ls /dev/ttyACM* 2>/dev/null | head -1)
  if [ -n "$PORT" ] && [ -e "$PORT" ]; then
    echo ">>> PORT $PORT UP at $(date '+%H:%M:%S.%3N')" | tee -a "$OUT"
    stty -F "$PORT" 115200 raw -echo 2>/dev/null
    # Long window: run until the port actually DROPS (device powers off), so we
    # catch the death moment + last uptime — not just a fixed timeout.
    timeout 3600 cat "$PORT" >> "$OUT" 2>&1
    if [ -e "$PORT" ]; then
      echo ">>> CAT TIMED OUT but PORT STILL UP at $(date '+%H:%M:%S.%3N') — device ALIVE (re-arm to continue)" | tee -a "$OUT"
    else
      echo ">>> PORT DROPPED at $(date '+%H:%M:%S.%3N') (SoC power-off / USB gone)" | tee -a "$OUT"
    fi
    last=$(grep -a "up=" "$OUT" | tail -1 | grep -oE "up=[0-9]+[ms]+")
    echo ">>> LAST UPTIME SEEN: ${last:-none}  | total lines: $(wc -l < "$OUT")" | tee -a "$OUT"
    exit 0
  fi
  sleep 0.2
done
echo ">>> timed out; port never appeared"
