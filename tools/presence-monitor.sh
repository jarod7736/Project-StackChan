#!/usr/bin/env bash
# Gate 1 unit#2 death detector (serial is unreliable on unit #2 due to USB re-enum,
# so detect power-off by SUSTAINED port loss). Re-enum gaps are ~1s; a real AXP
# soft-off drops USB permanently. Flag death if port absent >120s continuous.
set -u
start=$(date +%s); absent=0
echo "Gate1 unit#2 monitor armed at $(date '+%H:%M:%S') (t=0)"
for i in $(seq 1 1500); do            # 1500*3s = 75 min cap
  if ls /dev/ttyACM* >/dev/null 2>&1; then absent=0
  else
    absent=$((absent+1))
    if [ "$absent" -ge 40 ]; then     # 120s continuous absence = death
      el=$(( ($(date +%s) - start) / 60 ))
      echo ">>> LIKELY DEATH: USB gone >120s continuous at ~${el} min ($(date '+%H:%M:%S'))"
      exit 0
    fi
  fi
  sleep 3
done
el=$(( ($(date +%s) - start) / 60 ))
echo ">>> SURVIVED ~${el} min — unit #2 did NOT power off (port stable, ignoring brief re-enums)"
