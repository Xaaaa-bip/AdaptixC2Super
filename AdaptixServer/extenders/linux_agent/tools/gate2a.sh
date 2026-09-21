#!/usr/bin/env bash
# gate2a.sh — Phase 2a verifier gate: load every module with bpftool against
# the live kernel, list the programs, then remove the test pins.
#
# Note on `type`: the tracepoint modules are loaded as `tracepoint`
# (BPF_PROG_TYPE_TRACEPOINT = 5), NOT as `tracing` (26, fentry/fexit, which
# needs a BTF attach_btf_id).  knock is `xdp` (6).

OUT=/root/cc/AdaptixServer/extenders/linux_agent/src_agent/bpf/out
LOG=/tmp/gate2a.log

: > "$LOG"
rm -f /sys/fs/bpf/test_* 2>/dev/null

try() {
    local mod="$1" type="$2"
    echo "=== $mod (type $type) ===" | tee -a "$LOG"
    if bpftool prog load "$OUT/$mod.o" "/sys/fs/bpf/test_$mod" \
            type "$type" 2>&1 | tail -40 | tee -a "$LOG"; then
        if [ -e "/sys/fs/bpf/test_$mod" ]; then
            echo "[PASS] load $mod" | tee -a "$LOG"
        else
            echo "[FAIL] load $mod (no pin created)" | tee -a "$LOG"
        fi
    else
        echo "[FAIL] load $mod (bpftool exited non-zero)" | tee -a "$LOG"
    fi
}

try hide        tracepoint
try bpf_filter  tracepoint
try tc_rewrite  tracepoint
try knock       xdp

echo "=== bpftool prog list (tail) ===" | tee -a "$LOG"
bpftool prog list 2>&1 | tail -30 | tee -a "$LOG"

echo "=== cleanup ===" | tee -a "$LOG"
rm -f /sys/fs/bpf/test_* 2>/dev/null
ls /sys/fs/bpf/ | tee -a "$LOG"
echo "GATE2A_DONE"
