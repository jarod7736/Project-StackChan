#!/usr/bin/env bash
# Fast direct-esptool flasher for the AXP brown-out window.
# The device enumerates only briefly (~20s) before the fault drops it off the
# bus. PlatformIO's upload wastes that entire window on build/configure checks.
# This skips all of it: poll the port every 0.3s, and the INSTANT it appears,
# call esptool directly. esptool's --before default_reset pulls the chip into
# DOWNLOAD MODE, where the crashing app does not run — so if the drop is
# app-load-driven the flash completes; if it keeps failing even when caught
# instantly, the power cut is independent of the app == hardware fault.
set -u
PY=/home/jarod7736/.platformio/penv/bin/python
ESPTOOL=/home/jarod7736/.platformio/packages/tool-esptoolpy/esptool.py
BUILD=/home/jarod7736/projects/Project-StackChan/.pio/build/cores3_linux
BOOT_APP0=/home/jarod7736/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin
PORT=/dev/ttyACM0

echo "fast-flash armed at $(date '+%H:%M:%S'); polling $PORT every 0.3s ..."
for i in $(seq 1 6000); do          # ~30 min at 0.3s
  if [ -e "$PORT" ]; then
    echo ">>> PORT UP at $(date '+%H:%M:%S.%3N') — firing esptool immediately"
    "$PY" "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
      --before default_reset --after hard_reset write_flash -z \
      --flash_mode dio --flash_freq 80m --flash_size 16MB \
      0x0000 "$BUILD/bootloader.bin" \
      0x8000 "$BUILD/partitions.bin" \
      0xe000 "$BOOT_APP0" \
      0x10000 "$BUILD/firmware.bin"
    rc=$?
    echo ">>> esptool exit=$rc at $(date '+%H:%M:%S.%3N')"
    if [ $rc -eq 0 ]; then echo ">>> FLASH OK"; exit 0; fi
    echo ">>> flash failed (port likely dropped mid-write); re-arming poll"
  fi
  sleep 0.3
done
echo ">>> timed out; device never stayed up long enough"
