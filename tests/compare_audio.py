#!/usr/bin/env python3
"""Compares two mono 16-bit WAVs (native synth vs Web Audio reference): loudness per window and per SFX slot."""
import array, math, sys, wave

def load(path):
    with wave.open(path) as w:
        data = array.array('h', w.readframes(w.getnframes()))
        return [v / 32768.0 for v in data], w.getframerate()

def rms_db(x):
    return 10 * math.log10(sum(v * v for v in x) / max(1, len(x)) + 1e-12)

a, sr = load(sys.argv[1]); b, _ = load(sys.argv[2])
music = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0
print("window        native   web   diff (dB)")
for t in list(range(0, int(music), 6)):
    s, e = int(t * sr), int((t + 6) * sr)
    da, db = rms_db(a[s:e]), rms_db(b[s:e])
    print(f"music {t:3d}-{t+6:<3d}s {da:6.1f} {db:6.1f} {da-db:+5.1f}")
names = ["lay", "crack", "perfect", "corn", "land", "squawk", "empty"]
for i, n in enumerate(names):
    s = int((music + 1.5 + i * 0.6) * sr); e = s + int(0.6 * sr)
    pa, pb = max(abs(v) for v in a[s:e]), max(abs(v) for v in b[s:e])
    print(f"sfx {n:8s} rms {rms_db(a[s:e]):6.1f} {rms_db(b[s:e]):6.1f} {rms_db(a[s:e])-rms_db(b[s:e]):+5.1f}   peak {pa:.2f} {pb:.2f}")
