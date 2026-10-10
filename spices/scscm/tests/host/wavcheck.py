#!/usr/bin/env python3
"""wavcheck -- assertions on a rendered 16-bit WAV, and a two-render compare.

  wavcheck.py FILE CHECK...       each CHECK is one of (times in seconds):
      pitch=HZ@T0-T1    zero-crossing pitch of channel 0 within 2% of HZ
      rms=V@T0-T1       RMS of channel 0 within 15% of V
      sound@T0-T1       RMS above 0.005
      silent@T0-T1      RMS below 0.001
  wavcheck.py --compare A B T0 T1
      A and B agree over [T0, T1] in 0.1 s windows: RMS within 5% (or both
      below 0.005), pitch within 2% where both windows sound.

Prints one line per check and exits with the number that failed. No
dependencies beyond the standard library; pitch is the mean period between
upward zero crossings, located to a fraction of a sample.
"""
import math
import struct
import sys
import wave


def load(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    if w.getsampwidth() != 2:
        raise SystemExit(f"{path}: expected 16-bit samples")
    data = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    return [data[i * ch] / 32768.0 for i in range(n)], sr


def span(samples, sr, t0, t1):
    return samples[int(t0 * sr):int(t1 * sr)]


def rms(seg):
    return math.sqrt(sum(x * x for x in seg) / len(seg)) if seg else 0.0


def pitch(seg, sr):
    ups = []
    for i in range(1, len(seg)):
        a, b = seg[i - 1], seg[i]
        if a < 0 <= b:
            ups.append(i - 1 + (-a / (b - a) if b != a else 0.0))
    if len(ups) < 3:
        return 0.0
    return (len(ups) - 1) * sr / (ups[-1] - ups[0])


def parse_window(text):
    t0, t1 = text.split("-")
    return float(t0), float(t1)


def check(samples, sr, spec):
    if "@" not in spec:
        return False, f"bad check {spec!r}"
    what, window = spec.split("@", 1)
    t0, t1 = parse_window(window)
    seg = span(samples, sr, t0, t1)
    r = rms(seg)
    if what.startswith("pitch="):
        want, got = float(what[6:]), pitch(seg, sr)
        return abs(got - want) <= 0.02 * want, f"pitch {got:.1f} Hz, want {want:g}"
    if what.startswith("rms="):
        want = float(what[4:])
        return abs(r - want) <= 0.15 * want, f"rms {r:.4f}, want {want:g}"
    if what == "sound":
        return r > 0.005, f"rms {r:.4f}, want > 0.005"
    if what == "silent":
        return r < 0.001, f"rms {r:.4f}, want < 0.001"
    return False, f"bad check {spec!r}"


def compare(a_path, b_path, t0, t1):
    a, sra = load(a_path)
    b, srb = load(b_path)
    if sra != srb:
        print(f"FAIL sample rates differ: {sra} vs {srb}")
        return 1
    fails, t = 0, t0
    while t + 0.1 <= t1 + 1e-9:
        sa, sb = span(a, sra, t, t + 0.1), span(b, srb, t, t + 0.1)
        ra, rb = rms(sa), rms(sb)
        ok = (ra < 0.005 and rb < 0.005) or abs(ra - rb) <= 0.05 * max(ra, rb)
        note = f"rms {ra:.4f} vs {rb:.4f}"
        if ok and ra >= 0.005 and rb >= 0.005:
            pa, pb = pitch(sa, sra), pitch(sb, srb)
            ok = pa > 0 and pb > 0 and abs(pa - pb) <= 0.02 * max(pa, pb)
            note += f", pitch {pa:.1f} vs {pb:.1f} Hz"
        if not ok:
            print(f"FAIL {t:.1f}-{t + 0.1:.1f}s {note}")
            fails += 1
        t += 0.1
    if fails == 0:
        print(f"ok   agree over {t0:g}-{t1:g}s")
    return fails


def main(argv):
    if len(argv) >= 1 and argv[0] == "--compare":
        return compare(argv[1], argv[2], float(argv[3]), float(argv[4]))
    samples, sr = load(argv[0])
    fails = 0
    for spec in argv[1:]:
        ok, msg = check(samples, sr, spec)
        print(("ok   " if ok else "FAIL ") + f"{spec}: {msg}")
        fails += 0 if ok else 1
    return fails


if __name__ == "__main__":
    sys.exit(min(main(sys.argv[1:]), 125))
