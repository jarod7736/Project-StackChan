#!/usr/bin/env bash
# Looping serial capture — re-attaches after every port drop / re-enumeration,
# so it catches each boot (and the SD crash-trail dump) across the USB flapping.
set -u
OUT=/tmp/stkchan_boot.log
end=$(( $(date +%s) + 1800 ))
echo "loop-capture armed $(date '+%H:%M:%S') -> $OUT"
while [ "$(date +%s)" -lt "$end" ]; do
  P=$(ls /dev/ttyACM* 2>/dev/null | head -1)
  if [ -n "$P" ] && [ -e "$P" ]; then
    echo ">>> ATTACH $P $(date '+%H:%M:%S')" >> "$OUT"
    stty -F "$P" 115200 raw -echo 2>/dev/null
    cat "$P" >> "$OUT" 2>/dev/null
    echo ">>> DROP $(date '+%H:%M:%S')" >> "$OUT"
  else
    sleep 0.2
  fi
done
echo "loop-capture done $(date '+%H:%M:%S')"
