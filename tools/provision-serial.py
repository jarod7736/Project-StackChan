#!/usr/bin/env python3
"""Provision Stack-chan WiFi (and optional keys) over USB serial.

v2 dropped the captive-portal hotspot. First-time WiFi setup happens over
the same USB cable you flash on: the firmware, when it has no saved creds,
waits for a single line of JSON on the serial port. This script sends it.

Usage (from the machine the device is plugged into — e.g. Windows):

    python tools/provision-serial.py --port COM16 --ssid MyNet --psk secret

    # also set the OpenAI key + OTA password in the same shot:
    python tools/provision-serial.py --port COM16 --ssid MyNet --psk secret \
        --oai-key sk-... --ota-pass changeme

    # or hand it raw JSON:
    python tools/provision-serial.py --port COM16 \
        --json '{"ssid":"MyNet","psk":"secret"}'

After it sends, the device persists to NVS and reboots onto your LAN; the
control panel is then at http://stackchan.local/ in any browser.

Dependency: pyserial  (pip install pyserial)
"""

import argparse
import json
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed — run: pip install pyserial")


def build_payload(args) -> dict:
    if args.json:
        return json.loads(args.json)
    if not args.ssid:
        sys.exit("--ssid is required (or pass --json)")
    payload = {"ssid": args.ssid, "psk": args.psk or ""}
    if args.ssid2:
        payload["ssid2"] = args.ssid2
        payload["psk2"] = args.psk2 or ""
    if args.oai_key:
        payload["oai_key"] = args.oai_key
    if args.ota_pass:
        payload["ota_pass"] = args.ota_pass
    if args.chat_host:
        payload["chat_host"] = args.chat_host
    if args.tts_voice:
        payload["tts_voice"] = args.tts_voice
    if args.tts_provider:
        payload["tts_provider"] = args.tts_provider
    return payload


def main():
    ap = argparse.ArgumentParser(description="Provision Stack-chan over USB serial.")
    ap.add_argument("--port", required=True, help="Serial port (e.g. COM16 or /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ssid")
    ap.add_argument("--psk", default="")
    ap.add_argument("--ssid2")
    ap.add_argument("--psk2", default="")
    ap.add_argument("--oai-key")
    ap.add_argument("--ota-pass")
    ap.add_argument("--chat-host")
    ap.add_argument("--tts-voice")
    ap.add_argument("--tts-provider")
    ap.add_argument("--json", help="Raw JSON payload (overrides the individual flags)")
    args = ap.parse_args()

    payload = build_payload(args)
    line = json.dumps(payload) + "\n"

    # Opening the port toggles DTR/RTS, which resets the ESP32 — so the
    # device reboots when we connect and takes ~2 s to reach the point where
    # it reads serial. We resend the line a few times over ~15 s and watch
    # for the "saved … rebooting" confirmation.
    masked = dict(payload)
    for k in ("psk", "psk2", "oai_key", "ota_pass"):
        if masked.get(k):
            masked[k] = "***"

    print(f"Opening {args.port} @ {args.baud} ...")
    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()
        print(f"Sending: {json.dumps(masked)}  (will retry while the device boots)")

        deadline = time.time() + 15
        last_send = 0.0
        while time.time() < deadline:
            now = time.time()
            if now - last_send >= 2.5:          # (re)send every 2.5 s
                ser.write(line.encode("utf-8"))
                ser.flush()
                last_send = now
            chunk = ser.readline().decode("utf-8", "replace").rstrip()
            if chunk:
                print(f"  device: {chunk}")
                low = chunk.lower()
                if "saved" in low and "reboot" in low:
                    print("Provisioned. Device is rebooting onto your LAN.")
                    print("Control panel: http://stackchan.local/")
                    return
                if "handshake_timeout" in low or "reason: 15" in low:
                    print("\n!! WiFi 4-way handshake timeout = WRONG PASSWORD "
                          "(SSID is fine). Re-run with the correct --psk.\n"
                          "   In PowerShell, wrap a password with special "
                          "chars in SINGLE quotes: --psk 'p@ss$word'\n")
    print("Timed out. If you saw a handshake-timeout the password is wrong; "
          "otherwise re-check --port and that the device is powered.")


if __name__ == "__main__":
    main()
