#!/usr/bin/env python3
"""Generate the 2c diagnostic MP3 clip: a synthetic broadband 'TTS phrase'.

Keeps sub-step 2b's syllable ENVELOPE (12 bursts, ~160 ms on + ~55 ms gap,
~2.6 s total) but replaces the pure sine tones with BAND-LIMITED NOISE so the
clip is spectrally rich — this exercises (a) real broadband amp-drive current
and (b) a realistic MP3 decode load, neither of which a pure-tone clip can.
Deterministic (fixed seed) so the embedded header is reproducible.

Pipe raw s16le @ 16 kHz mono to ffmpeg, band-pass to the speech band, encode MP3.
"""
import random, struct, sys

SR = 16000
SYL_MS = 160
GAP_MS = 55
N_SYL = 12
random.seed(20260606)  # reproducible

def burst(ms):
    n = SR * ms // 1000
    out = []
    # simple onset/offset ramp (5 ms) to avoid a click-only spectrum while
    # keeping a sharp-ish syllable onset (onset transients matter for current).
    ramp = SR * 5 // 1000
    for i in range(n):
        s = random.uniform(-1, 1)
        if i < ramp:
            s *= i / ramp
        elif i > n - ramp:
            s *= (n - i) / ramp
        out.append(int(max(-1, min(1, s)) * 32767))
    return out

def silence(ms):
    return [0] * (SR * ms // 1000)

samples = []
for _ in range(N_SYL):
    samples += burst(SYL_MS)
    samples += silence(GAP_MS)

sys.stdout.buffer.write(b"".join(struct.pack("<h", s) for s in samples))
