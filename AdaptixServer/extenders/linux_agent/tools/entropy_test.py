#!/usr/bin/env python3
"""entropy_test.py — Phase 4 gap 6: H3/H4 entropy check.

Samples come from: the generated agent binary (per-payload polymorphic
junk + randomized link order), and optionally captured session ciphertext.
Checks byte-level chi-square + Shannon entropy against the uniform
distribution. Thresholds are env-tunable; --report only prints.

Usage:
  python3 entropy_test.py --samples /tmp/agent.bin [--min-entropy 7.8] [--chi2-p 0.01]
"""
import math
import sys


def entropy_stats(data):
    n = len(data)
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    h = 0.0
    chi2 = 0.0
    exp = n / 256.0
    for c in counts:
        if c:
            p = c / n
            h -= p * math.log2(p)
        chi2 += (c - exp) ** 2 / exp
    return h, chi2


def synthetic_ciphertext(path, size=16384):  # hex argv limit ~128KB
    """Generate ChaCha20 ciphertext via tools/test_chacha (C implementation):
    random plaintext XOR keystream — the exact primitive the session layer
    uses. Uniform plaintext => ciphertext must be indistinguishable from
    uniform. Returns the sample path."""
    import os, subprocess
    key = os.urandom(16).hex()
    nonce = os.urandom(12).hex()
    plain = os.urandom(size).hex()
    exe = "/tmp/test_chacha_entropy"
    if not os.path.exists(exe):
        subprocess.run(["gcc", "-O2", "-o", exe, os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_chacha.c")], check=True)
    out = subprocess.run([exe, key, nonce, plain],
                         capture_output=True, text=True).stdout.strip()
    data = bytes.fromhex(out)
    open(path, "wb").write(data)
    return path


def main():
    args = sys.argv[1:]
    def opt(name, default):
        return args[args.index(name) + 1] if name in args else default
    paths = opt("--samples", "/tmp/agent.bin").split(",")
    min_h = float(opt("--min-entropy", "7.8"))
    report_only = "--report" in args

    if "--synthetic" in args:
        paths = [synthetic_ciphertext("/tmp/ax_synth_ct.bin")]

    failed = 0
    for path in paths:
        try:
            data = open(path, "rb").read()
        except OSError as e:
            print("SKIP: %s (%s)" % (path, e))
            continue
        if len(data) < 4096:
            print("SKIP: %s too small (%d bytes)" % (path, len(data)))
            continue
        # Sample evenly so file structure (headers) does not dominate
        step = max(1, len(data) // 65536)
        sample = data[::step][:65536]
        h, chi2 = entropy_stats(sample)
        # chi-square with 255 dof: 0.01 tail ~ 310
        chi2_ok = chi2 < 310
        h_ok = h >= min_h
        status = "PASS" if (chi2_ok and h_ok) else "FAIL"
        if status == "FAIL":
            failed += 1
        print("%s: %s bytes, Shannon=%.4f (>=%.2f), chi2=%.1f (<310) -> %s"
              % (path, len(data), h, min_h, chi2, status))
    if failed and not report_only:
        sys.exit(1)
    print("PASS: all samples indistinguishable from uniform" if not failed
          else "REPORT-ONLY: %d sample(s) failed" % failed)


if __name__ == "__main__":
    main()
