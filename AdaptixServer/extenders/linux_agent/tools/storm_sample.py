#!/usr/bin/env python3
"""storm_sample.py — gap 1 retry-cadence analyzer (kept out of the shell
script to avoid nested-heredoc quoting)."""
import sys

path, down = sys.argv[1], int(sys.argv[2])
try:
    events = [(int(l.split()[0]), l.split()[1])
              for l in open(path) if len(l.split()) == 2]
except OSError:
    print("FAIL: no AX_TEST_LOG written")
    sys.exit(1)

retries = [t for t, e in events if e == "reconnect"]
print("reconnect attempts during outage: %d" % len(retries))
if len(retries) == 0:
    print("FAIL: no retry attempts seen")
    sys.exit(1)
ivs = [b - a for a, b in zip(retries, retries[1:])]
fast = [iv for iv in ivs if iv < 1000]
if len(fast) > len(ivs) // 2:
    print("FAIL: retry flood (%d of %d intervals < 1s)" % (len(fast), len(ivs)))
    sys.exit(1)
print("PASS: retry cadence %s ms (no flood)" % ivs[:8])
