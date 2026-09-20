#!/usr/bin/env bash
# rotation_test.sh — Phase 4 gap 2: rotation boundary via AX_ROTATE_MS.
#
# AX_ROTATE_MS=5000 forces ~5s connection rotation (bypassing the 15-45 min
# clamp). The agent logs connect/rotate/reconnect events to AX_TEST_LOG;
# assert rotate events fire at ~5s + jitter and reconnects are immediate
# (attempt=-1, no backoff).
#
# Usage: bash rotation_test.sh [--rotate-ms 5000] [--rotations 3] [--listener TestHTTP]
set -u
ROTATE_MS=5000
ROTATIONS=3
LISTENER="TestHTTP"
while [ $# -gt 0 ]; do case "$1" in
  --rotate-ms) ROTATE_MS="$2"; shift 2;;
  --rotations) ROTATIONS="$2"; shift 2;;
  --listener) LISTENER="$2"; shift 2;;
  *) echo "unknown arg $1"; exit 2;;
esac; done

cd "$(dirname "$0")/.."
BIN=$(python3 - "$LISTENER" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import e2e
p = e2e.gen_agent(sys.argv[1], "rot")
print(p)
EOF
)
LOG=/tmp/ax_rot_test.log
[ -n "$BIN" ] || { echo "FAIL: agent generate failed"; exit 1; }
rm -f "$LOG"
AX_ROTATE_MS=$ROTATE_MS AX_TEST_LOG=$LOG "$BIN" & AGENT_PID=$!

WAIT=$(( ROTATE_MS * (ROTATIONS + 1) / 1000 + 15 ))
echo "waiting ${WAIT}s for $ROTATIONS rotations..."
sleep "$WAIT"
kill $AGENT_PID 2>/dev/null

python3 - "$LOG" "$ROTATE_MS" "$ROTATIONS" <<'EOF'
import sys
log, want_ms, need = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
events = []
for line in open(log):
    parts = line.split()
    if len(parts) == 2:
        events.append((int(parts[0]), parts[1]))
rotates = [t for t, e in events if e == "rotate"]
connects = [t for t, e in events if e == "connect"]
if len(rotates) < need:
    print("FAIL: only %d rotate events (need %d); events=%s" % (len(rotates), need, [e for _, e in events[:12]]))
    sys.exit(1)
# intervals between successive connects ≈ rotate_ms + jitter
ivs = [b - a for a, b in zip(connects, connects[1:])]
ok = [iv for iv in ivs if want_ms * 0.4 <= iv <= want_ms * 3.0]
print("connects=%d intervals=%s" % (len(connects), ivs))
print("PASS: %d/%d intervals within [%.1fs, %.1fs]" % (len(ok), len(ivs), want_ms*0.4/1000, want_ms*3.0/1000))
sys.exit(0 if len(ok) >= need - 1 else 1)
EOF
