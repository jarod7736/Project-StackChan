#!/usr/bin/env python3
"""Provision Stack-chan credentials via the captive-portal HTTP API.

Connect your laptop to the Stack-chan AP (SSID: "Stackchan-XXXX", no password),
then run this script.  It will:
  1. Add one or more WiFi credentials to the device (POST /api/wifi/add).
  2. Push any non-WiFi NVS keys (chat, STT, TTS, OTA, persona) to the device
     (POST /api/config).
  3. Optionally exit the captive-portal so the device reboots into normal mode
     (POST /api/exit).

Usage:
    python3 tools/provision-stackchan.py [options]

Examples:
    # Interactive — prompts for every field you haven't passed on the CLI:
    python3 tools/provision-stackchan.py

    # Load all keys from a JSON file (same shape as --from-file):
    python3 tools/provision-stackchan.py --from-file ./my-config.json

    # Skip the interactive prompts, only set a specific key:
    python3 tools/provision-stackchan.py --no-wifi --only-keys chat_host,chat_model

    # Point at a non-default AP address:
    python3 tools/provision-stackchan.py --host 192.168.4.1

--from-file JSON shape (all keys optional):
    {
        "wifi": [
            {"ssid": "MyNetwork", "password": "hunter2"},
            {"ssid": "Backup",    "password": ""}
        ],
        "chat_host":    "http://192.168.1.178:8080",
        "chat_model":   "my-model",
        "stt_url":      "https://api.openai.com",
        "stt_model":    "whisper-1",
        "oai_key":      "sk-...",
        "tts_provider": "openai",
        "tts_voice":    "alloy",
        "tts_model":    "tts-1",
        "el_key":       "...",
        "ota_pass":     "changeme",
        "persona":      "You are a helpful desktop companion..."
    }

Dependencies: standard library + requests (pip install requests).
"""

import argparse
import getpass
import json
import sys
from urllib.parse import urljoin

# ---------------------------------------------------------------------------
# NVS key definitions — exactly the 17 Stack-chan spec keys.
# WiFi slots are handled separately via /api/wifi/add (not /api/config).
# ---------------------------------------------------------------------------

# Config keys (sent to POST /api/config)
CONFIG_FIELDS = [
    # key           label                       category   sensitive
    ("chat_host",   "Chat Host URL",             "chat",    False),
    ("chat_model",  "Chat Model",                "chat",    False),
    ("stt_url",     "STT Endpoint URL",          "stt",     False),
    ("stt_model",   "STT Model",                 "stt",     False),
    ("oai_key",     "OpenAI API Key",            "stt",     True),
    ("tts_provider","TTS Provider (openai/eleven/voicevox)", "tts", False),
    ("tts_voice",   "TTS Voice ID",              "tts",     False),
    ("tts_model",   "TTS Model",                 "tts",     False),
    ("el_key",      "ElevenLabs Key",            "tts",     True),
    ("ota_pass",    "OTA Password",              "ota",     True),
    ("persona",     "Persona Prompt",            "persona", False),
]

# Keys whose values should be redacted in console output
SENSITIVE_KEYS = {f[0] for f in CONFIG_FIELDS if f[3]}
SENSITIVE_KEYS.add("password")  # WiFi password

MAX_WIFI_SLOTS = 3


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

def _get(session, base: str, path: str) -> dict:
    """GET base+path, return parsed JSON or raise."""
    import requests
    url = urljoin(base, path)
    r = session.get(url, timeout=10)
    r.raise_for_status()
    return r.json()


def _post(session, base: str, path: str, payload: dict) -> dict:
    """POST JSON payload to base+path, return parsed JSON or raise."""
    url = urljoin(base, path)
    r = session.post(url, json=payload, timeout=10)
    r.raise_for_status()
    return r.json()


def _post_empty(session, base: str, path: str) -> dict:
    """POST with an empty body (e.g. /api/exit)."""
    import requests
    url = urljoin(base, path)
    r = session.post(url, timeout=10)
    r.raise_for_status()
    return r.json()


# ---------------------------------------------------------------------------
# High-level provisioning steps
# ---------------------------------------------------------------------------

def probe_device(session, base: str) -> bool:
    """Check reachability; return True if device responds."""
    try:
        info = _get(session, base, "/api/status")
        ap_ssid = info.get("ap_ssid", "(unknown)")
        free    = info.get("free_heap", "?")
        print(f"  Connected to Stack-chan AP: {ap_ssid}  (free heap: {free} bytes)")
        return True
    except Exception as e:
        print(f"  error: cannot reach device at {base}: {e}", file=sys.stderr)
        return False


def provision_wifi(session, base: str, wifi_entries: "list[dict]") -> bool:
    """Push WiFi credentials. wifi_entries: [{"ssid": ..., "password": ...}, ...]"""
    ok = True
    for entry in wifi_entries:
        ssid = entry.get("ssid", "").strip()
        if not ssid:
            print("  warn: skipping WiFi entry with empty SSID", file=sys.stderr)
            continue
        payload = {"ssid": ssid, "password": entry.get("password", "")}
        try:
            resp = _post(session, base, "/api/wifi/add", payload)
            saved = resp.get("saved", False)
            print(f"  WiFi '{ssid}': {'saved' if saved else 'response=' + str(resp)}")
        except Exception as e:
            print(f"  error: POST /api/wifi/add for '{ssid}': {e}", file=sys.stderr)
            ok = False
    return ok


def provision_config(session, base: str, config: dict) -> bool:
    """Push non-WiFi NVS keys via POST /api/config."""
    if not config:
        return True
    try:
        resp = _post(session, base, "/api/config", config)
        updated = resp.get("updated", "?")
        redacted = {k: ("<set>" if k in SENSITIVE_KEYS else v) for k, v in config.items()}
        print(f"  Config: {updated} key(s) updated — {json.dumps(redacted)}")
        return True
    except Exception as e:
        print(f"  error: POST /api/config: {e}", file=sys.stderr)
        return False


def trigger_exit(session, base: str) -> None:
    """Ask the device to exit the captive portal (triggers reboot to normal mode)."""
    try:
        resp = _post_empty(session, base, "/api/exit")
        if resp.get("exiting"):
            print("  Device exiting captive portal — it will reconnect to WiFi.")
        else:
            print(f"  Unexpected exit response: {resp}")
    except Exception as e:
        # A connection-reset error here is normal if the device reboots immediately.
        print(f"  (exit posted; connection reset is normal: {e})")


# ---------------------------------------------------------------------------
# Interactive prompting
# ---------------------------------------------------------------------------

def prompt_wifi(max_slots: int = MAX_WIFI_SLOTS) -> "list[dict]":
    """Interactively collect up to max_slots WiFi credentials."""
    entries = []
    print(f"\nWiFi credentials (up to {max_slots} networks; blank SSID to stop):")
    for i in range(max_slots):
        ssid = input(f"  SSID [{i+1}]: ").strip()
        if not ssid:
            break
        pw = getpass.getpass(f"  Password [{i+1}] (blank = open network): ")
        entries.append({"ssid": ssid, "password": pw})
    return entries


def prompt_config(only_keys=None) -> dict:
    """Interactively collect non-WiFi config values.

    Blank input skips the field (it is not sent to the device).
    """
    config: dict[str, str] = {}
    print("\nNon-WiFi configuration (blank to skip any field):")
    for key, label, _cat, sensitive in CONFIG_FIELDS:
        if only_keys and key not in only_keys:
            continue
        if key == "persona":
            # Persona can be long — let them type it as a single line or paste.
            print(f"  {label} (persona): ", end="", flush=True)
            val = input("").strip()
        elif sensitive:
            val = getpass.getpass(f"  {label} ({key}): ").strip()
        else:
            val = input(f"  {label} ({key}): ").strip()
        if val:
            config[key] = val
    return config


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> int:
    p = argparse.ArgumentParser(
        description="Provision Stack-chan credentials via the captive-portal HTTP API.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Dependencies:")[0],
    )
    p.add_argument(
        "--host",
        default="http://192.168.4.1",
        help="Base URL of the Stack-chan captive portal (default: http://192.168.4.1)",
    )
    p.add_argument(
        "--no-wifi",
        action="store_true",
        help="Skip WiFi credential prompts / entries from --from-file",
    )
    p.add_argument(
        "--no-config",
        action="store_true",
        help="Skip non-WiFi config prompts / entries from --from-file",
    )
    p.add_argument(
        "--only-keys",
        default="",
        metavar="KEY1,KEY2,...",
        help="Comma-separated subset of config keys to set (implies --no-wifi unless combined)",
    )
    p.add_argument(
        "--from-file",
        metavar="FILE",
        help="JSON file with wifi[] array and/or flat config keys (see --help for shape)",
    )
    p.add_argument(
        "--exit",
        action="store_true",
        dest="do_exit",
        help="After provisioning, POST /api/exit so the device leaves the captive portal",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would be sent without actually contacting the device",
    )
    args = p.parse_args()

    # Normalise base URL
    base = args.host.rstrip("/")
    if not base.startswith("http"):
        base = "http://" + base

    # Build only_keys set
    only_keys = None
    if args.only_keys:
        only_keys = {k.strip() for k in args.only_keys.split(",") if k.strip()}
        valid = {f[0] for f in CONFIG_FIELDS}
        unknown = only_keys - valid
        if unknown:
            print(f"error: unknown key(s): {', '.join(sorted(unknown))}", file=sys.stderr)
            print(f"       valid keys: {', '.join(f[0] for f in CONFIG_FIELDS)}", file=sys.stderr)
            return 2

    # ── Load from file (if given) ───────────────────────────────────────────
    file_wifi: list[dict] = []
    file_config: dict[str, str] = {}

    if args.from_file:
        try:
            with open(args.from_file, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except (OSError, json.JSONDecodeError) as e:
            print(f"error: could not read {args.from_file}: {e}", file=sys.stderr)
            return 2

        file_wifi = data.pop("wifi", [])
        # Remaining keys must all be recognised config keys
        valid_config = {f[0] for f in CONFIG_FIELDS}
        for k, v in data.items():
            if k not in valid_config:
                print(f"warn: ignoring unrecognised key '{k}' in {args.from_file}",
                      file=sys.stderr)
                continue
            file_config[k] = str(v)

    # ── Decide what to send ─────────────────────────────────────────────────
    wifi_entries: list[dict] = []
    config_payload: dict[str, str] = {}

    if not args.no_wifi:
        if file_wifi:
            wifi_entries = file_wifi[:MAX_WIFI_SLOTS]
        else:
            wifi_entries = prompt_wifi()

    if not args.no_config:
        if file_config:
            if only_keys:
                config_payload = {k: v for k, v in file_config.items() if k in only_keys}
            else:
                config_payload = file_config
        else:
            config_payload = prompt_config(only_keys=only_keys)

    if not wifi_entries and not config_payload:
        if not args.do_exit:
            print("error: nothing to send (all sections were empty or skipped)", file=sys.stderr)
            return 2

    # ── Dry-run mode ────────────────────────────────────────────────────────
    if args.dry_run:
        print("[dry-run] Would send to:", base)
        for entry in wifi_entries:
            r = {k: ("<set>" if k in SENSITIVE_KEYS else v) for k, v in entry.items()}
            print(f"  POST /api/wifi/add  {json.dumps(r)}")
        if config_payload:
            r2 = {k: ("<set>" if k in SENSITIVE_KEYS else v) for k, v in config_payload.items()}
            print(f"  POST /api/config    {json.dumps(r2)}")
        if args.do_exit:
            print("  POST /api/exit")
        return 0

    # ── Import requests (deferred so --dry-run / --help work without it) ───
    try:
        import requests
    except ImportError:
        print("error: 'requests' library not found. Install with: pip install requests",
              file=sys.stderr)
        return 1

    session = requests.Session()

    # ── Probe device ────────────────────────────────────────────────────────
    print(f"\nProbing {base} ...")
    if not probe_device(session, base):
        print("\nMake sure you are connected to the Stack-chan AP (Stackchan-XXXX).",
              file=sys.stderr)
        return 1

    # ── Send WiFi ────────────────────────────────────────────────────────────
    all_ok = True
    if wifi_entries:
        print("\nPushing WiFi credentials ...")
        if not provision_wifi(session, base, wifi_entries):
            all_ok = False

    # ── Send config ──────────────────────────────────────────────────────────
    if config_payload:
        print("\nPushing config keys ...")
        if not provision_config(session, base, config_payload):
            all_ok = False

    # ── Exit captive portal ──────────────────────────────────────────────────
    if args.do_exit:
        print("\nRequesting exit from captive portal ...")
        trigger_exit(session, base)

    print("\nDone." if all_ok else "\nDone (with errors — check output above).")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
