#!/usr/bin/env bash
# reconnect_storm_test.sh — Phase 4 gap 1: reconnect storm / backoff.
#
# Kill the teamserver mid-session; the agent must retry at the configured
# timeout cadence (NOT a 10Hz flood) and reconnect cleanly once the server
# returns. Retry attempts are observed via AX_TEST_LOG "reconnect" events
# (wired in main.c attempt>0 branches; the https-loop K marker only exists
# after a successful connect, so it cannot observe outage-time retries).
#
# Usage: bash reconnect_storm_test.sh [--listener TestHTTP] [--server-down 40] [--timeout 150]
set -u
LISTENER="TestHTTP"
DOWN=40
TIMEOUT=150
while [ $# -gt 0 ]; do case "$1" in
  --listener) LISTENER="$2"; shift 2;;
  --server-down) DOWN="$2"; shift 2;;
  --timeout) TIMEOUT="$2"; shift 2;;
  *) echo "unknown arg $1"; exit 2;;
esac; done

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE/.."

echo "== [1] generate + start agent (AX_TEST_LOG) =="
BIN=$(python3 - "$LISTENER" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import e2e
print(e2e.gen_agent(sys.argv[1], "storm"))
EOF
)
[ -n "$BIN" ] || { echo "FAIL: agent generate failed"; exit 1; }
rm -f /tmp/ax_dbg /tmp/ax_storm_test.log
AX_TEST_LOG=/tmp/ax_storm_test.log "$BIN" & AGENT_PID=$!
sleep 8

echo "== [2] register + baseline roundtrip =="
python3 - "$LISTENER" "$TIMEOUT" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import e2e
aid = e2e.wait_registered_latest(sys.argv[1], int(sys.argv[2]))
e2e.send_raw(aid, "getuid")
e2e.console_poll(aid, '"a_completed":true', 90)
print("baseline roundtrip OK", flush=True)
EOF

echo "== [3] kill server for ${DOWN}s, sample retry cadence =="
pkill -x adaptixserver
sleep "$DOWN"
python3 "$(dirname "$0")/storm_sample.py" /tmp/ax_storm_test.log "$DOWN"

echo "== [4] restart server, assert clean reconnect =="
cd /root/cc/run && (nohup ./adaptixserver -web -profile central.profile > server.log 2>&1 < /dev/null &)
cd "$HERE/.."
python3 - "$LISTENER" "$TIMEOUT" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import e2e
aid = e2e.wait_registered_latest(sys.argv[1], int(sys.argv[2]))
e2e.send_raw(aid, "getuid")
msg = e2e.console_poll(aid, '"a_completed":true', 90)
print("PASS: clean reconnect + roundtrip:", msg[:90])
EOF
RC=$?
kill $AGENT_PID 2>/dev/null
exit $RC
