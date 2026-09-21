#!/usr/bin/env bash
# leak_test.sh — Phase 4 gap 8: rotation memory leak check.
#
# AX_ROTATE_MS forces rapid rotations; sample VmRSS across N rotations and
# assert growth stays under the threshold (default 2MB).
#
# Usage: bash leak_test.sh [--rotations 20] [--rotate-ms 3000] [--threshold-mb 2] [--listener TestHTTP]
set -u
ROTATIONS=20
ROTATE_MS=3000
THRESHOLD_MB=2
LISTENER="TestHTTP"
while [ $# -gt 0 ]; do case "$1" in
  --rotations) ROTATIONS="$2"; shift 2;;
  --rotate-ms) ROTATE_MS="$2"; shift 2;;
  --threshold-mb) THRESHOLD_MB="$2"; shift 2;;
  --listener) LISTENER="$2"; shift 2;;
  *) echo "unknown arg $1"; exit 2;;
esac; done

cd "$(dirname "$0")/.."
BIN=$(python3 - "$LISTENER" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import e2e
print(e2e.gen_agent(sys.argv[1], "leak"))
EOF
)
LOG=/tmp/ax_leak_test.log
rm -f "$LOG"
AX_ROTATE_MS=$ROTATE_MS AX_TEST_LOG=$LOG "$BIN" & AGENT_PID=$!

rss() { awk '/VmRSS/{print $2}' "/proc/$AGENT_PID/status" 2>/dev/null; }
SAMPLES=/tmp/leak_samples.txt
: > "$SAMPLES"
for i in $(seq 1 $((ROTATIONS + 2))); do
  sleep $(( ROTATE_MS / 1000 + 1 ))
  R=$(rss)
  [ -n "$R" ] || { echo "FAIL: agent died at sample $i"; kill $AGENT_PID 2>/dev/null; exit 1; }
  echo "$R" >> "$SAMPLES"
done
kill $AGENT_PID 2>/dev/null

python3 - "$SAMPLES" "$THRESHOLD_MB" <<'EOF'
import sys
vals = [int(v) for v in open(sys.argv[1]) if v.strip().isdigit()]
limit_kb = int(sys.argv[2]) * 1024
first, last = vals[0], vals[-1]
growth = last - first
print("VmRSS first=%dKB last=%dKB growth=%dKB (limit %dKB)" % (first, last, growth, limit_kb))
if growth > limit_kb:
    print("FAIL: leak above threshold"); sys.exit(1)
print("PASS: rotation memory stable")
EOF
