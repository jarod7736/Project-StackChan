#!/usr/bin/env python3
"""Render the firmware's fixed phrases to MP3 clips for LittleFS.

Extracts phrases from the C++ headers (single source of truth), renders each
via the SAME provider/voice/model the device uses (request shape mirrors
src/net/TtsClient.cpp), and writes data/clips/<fnv1a32-hex>.mp3 + manifest.

The hash MUST match src/services/ClipId.cpp (FNV-1a 32-bit over UTF-8 bytes).

Usage:
  OPENAI_API_KEY=... python3 tools/render_speeches.py          # render missing
  python3 tools/render_speeches.py --list                      # phrases+hashes
  python3 tools/render_speeches.py --check                     # CI drift gate
  python3 tools/render_speeches.py --force                     # re-render all
  python3 tools/render_speeches.py --selftest                  # hash vectors
  ELEVENLABS_API_KEY=... python3 tools/render_speeches.py \\
      --provider eleven --voice <voice_id> --model eleven_multilingual_v2

Spec: docs/superpowers/specs/2026-06-11-stock-clips-design.md
"""
import argparse
import json
import os
import re
import sys
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = ROOT / "src" / "config.h"
GREETINGS_H = ROOT / "src" / "prompts" / "greetings.h"
CLIPS_DIR = ROOT / "data" / "clips"
MANIFEST = CLIPS_DIR / "manifest.json"

CONST_RE = re.compile(
    r'constexpr\s+const\s+char\*\s+(k\w+)\s*=\s*"((?:[^"\\]|\\.)*)"')


def unescape(s: str) -> str:
    # ascii-strict: unicode_escape round-trips through latin-1 and would
    # silently mojibake non-ASCII phrases → wrong hash → clip never matches
    # on-device. Fail loudly at extraction time instead.
    return s.encode("ascii", errors="strict").decode("unicode_escape")


def fnv1a32(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def clip_name(text: str) -> str:
    return f"{fnv1a32(text.encode('utf-8')):08x}.mp3"


def extract_phrases() -> dict:
    """name → text for every spoken fixed phrase. Empty strings skipped."""
    phrases = {}
    cfg = CONFIG_H.read_text()
    for name, raw in CONST_RE.findall(cfg):
        if name.startswith("kErr") or name in ("kLowBattMsg", "kWakeAck"):
            text = unescape(raw)
            if text:
                phrases[name] = text
    greet_src = GREETINGS_H.read_text()
    block = re.search(r"kGreetings\[\]\s*=\s*\{(.*?)\};", greet_src, re.S)
    if not block:
        sys.exit("ERROR: kGreetings[] block not found in greetings.h")
    block_text = re.sub(r"//[^\n]*", "", block.group(1))  # strip // comments
    for i, m in enumerate(re.finditer(r'"((?:[^"\\]|\\.)*)"', block_text)):
        phrases[f"kGreetings[{i}]"] = unescape(m.group(1))
    return phrases


def device_defaults() -> dict:
    cfg = CONFIG_H.read_text()
    vals = dict(CONST_RE.findall(cfg))
    return {
        "provider": vals.get("kDefaultTtsProv", "openai"),
        "voice": vals.get("kDefaultTtsVoice", "nova"),
        "model": vals.get("kDefaultTtsModel", "tts-1"),
    }


def render_openai(text: str, voice: str, model: str, key: str) -> bytes:
    # Mirrors buildOpenAiBody() in src/net/TtsClient.cpp.
    body = json.dumps({"model": model, "voice": voice, "input": text,
                       "response_format": "mp3"}).encode()
    req = urllib.request.Request(
        "https://api.openai.com/v1/audio/speech", data=body,
        headers={"Authorization": f"Bearer {key}",
                 "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def render_eleven(text: str, voice: str, model: str, key: str) -> bytes:
    # Mirrors buildElevenBody() in src/net/TtsClient.cpp.
    body = json.dumps({"text": text, "model_id": model,
                       "voice_settings": {"stability": 0.5,
                                          "similarity_boost": 0.75}}).encode()
    req = urllib.request.Request(
        f"https://api.elevenlabs.io/v1/text-to-speech/{voice}", data=body,
        headers={"xi-api-key": key, "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def selftest() -> None:
    # Must match test/test_clip_id/test_clip_id.cpp.
    assert fnv1a32(b"") == 0x811C9DC5
    assert fnv1a32(b"a") == 0xE40C292C
    assert fnv1a32(b"foobar") == 0xBF9CF968
    assert clip_name("foobar") == "bf9cf968.mp3"
    print("selftest OK")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--provider", default=None, help="openai|eleven")
    ap.add_argument("--voice", default=None)
    ap.add_argument("--model", default=None)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return 0

    phrases = extract_phrases()
    defaults = device_defaults()
    provider = args.provider or defaults["provider"]
    voice = args.voice or defaults["voice"]
    model = args.model or defaults["model"]

    if args.list:
        for name, text in phrases.items():
            print(f"{clip_name(text)}  {name:<16} {text!r}")
        return 0

    if args.check:
        missing = [n for n, t in phrases.items()
                   if not (CLIPS_DIR / clip_name(t)).exists()]
        expected = {clip_name(t) for t in phrases.values()}
        orphans = [p.name for p in CLIPS_DIR.glob("*.mp3")
                   if p.name not in expected]
        for n in missing:
            print(f"MISSING clip for {n}: {phrases[n]!r}")
        for o in orphans:
            print(f"ORPHAN clip {o} (phrase text changed or removed?)")
        if missing or orphans:
            return 1
        print(f"OK: {len(phrases)} phrases all have clips, no orphans")
        return 0

    # Render.
    if provider == "openai":
        key = os.environ.get("OPENAI_API_KEY", "")
        render = render_openai
    elif provider in ("eleven", "elevenlabs"):
        key = os.environ.get("ELEVENLABS_API_KEY", "")
        render = render_eleven
    else:
        sys.exit(f"ERROR: unsupported provider {provider!r}")
    if not key:
        sys.exit("ERROR: API key env var not set")

    CLIPS_DIR.mkdir(parents=True, exist_ok=True)
    manifest = {"provider": provider, "voice": voice, "model": model,
                "rendered_at": datetime.now(timezone.utc).isoformat(),
                "clips": {}}
    for name, text in phrases.items():
        fname = clip_name(text)
        out = CLIPS_DIR / fname
        manifest["clips"][fname] = {"name": name, "text": text}
        if out.exists() and not args.force:
            print(f"skip   {fname}  {name} (exists)")
            continue
        print(f"render {fname}  {name}  {text!r}")
        out.write_bytes(render(text, voice, model, key))
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False)
                        + "\n")
    print(f"wrote {MANIFEST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
