#!/usr/bin/env python3
"""periodogram_test.py — Phase 4 gap 5: H2 timing periodogram check.

Feeds on AX_TEST_LOG output (lines "<monotonic_ms> <event>"). Rebuilds the
connect/rotate event series, then FFTs the inter-arrival times. The right-
skewed jitter + bounded random-walk drift must leave NO dominant spectral
peak: max_bin / mean_bin must stay under --peak-ratio (default 5.0).

Usage:
  python3 periodogram_test.py --log /tmp/ax_rot_test.log [--peak-ratio 5.0]
  # report-only mode: --report  (never fail, just print)
"""
import sys


def load_series(path):
    ts = []
    for line in open(path):
        parts = line.split()
        if len(parts) == 2:
            try:
                ts.append((int(parts[0]), parts[1]))
            except ValueError:
                pass
    ts.sort()
    return ts


def main():
    args = sys.argv[1:]
    def opt(name, default):
        return args[args.index(name) + 1] if name in args else default
    log = opt("--log", "/tmp/ax_rot_test.log")
    ratio = float(opt("--peak-ratio", "5.0"))
    report_only = "--report" in args

    events = load_series(log)
    connects = [t for t, e in events if e in ("connect", "reconnect")]
    if len(connects) < 8:
        print("SKIP: only %d connect events in %s (need >=8)" % (len(connects), log))
        sys.exit(0 if report_only else 1)

    iv = [b - a for a, b in zip(connects, connects[1:])]
    try:
        import numpy as np
    except ImportError:
        print("SKIP: numpy not installed (pip3 install numpy)")
        sys.exit(0 if report_only else 1)

    # Detrend (subtract mean) + hann window before FFT
    x = np.array(iv, dtype=float)
    x = x - x.mean()
    w = np.hanning(len(x))
    sp = np.abs(np.fft.rfft(x * w)) ** 2
    peak = float(sp.max())
    mean = float(sp.mean())
    pr = peak / mean if mean > 0 else float("inf")

    # baseline: white noise has peak/mean ~ 3-4 for this window length;
    # a hard periodic component spikes far above that.
    print("events=%d mean_iv=%.1fms peak/mean=%.2f (limit %.1f)"
          % (len(connects), float(np.mean(iv)), pr, ratio))
    if pr > ratio and not report_only:
        print("FAIL: dominant spectral peak detected")
        sys.exit(1)
    print("PASS: no dominant periodicity" if pr <= ratio else "REPORT-ONLY: peak above limit")


if __name__ == "__main__":
    main()
