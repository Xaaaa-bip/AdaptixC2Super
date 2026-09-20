#!/usr/bin/env bash
# multiagent_test.sh — Phase 4 gap 3: N agents on one listener.
#
# Launch N agents on the same listener, assert all register, send getuid to
# each, and assert each response lands on its OWN console (no cross-talk).
#
# Usage: bash multiagent_test.sh [--listener TestHTTP] [--agents 5] [--timeout 150]
set -u
LISTENER="TestHTTP"
AGENTS=5
TIMEOUT=150
while [ $# -gt 0 ]; do case "$1" in
  --listener) LISTENER="$2"; shift 2;;
  --agents) AGENTS="$2"; shift 2;;
  --timeout) TIMEOUT="$2"; shift 2;;
  *) echo "unknown arg $1"; exit 2;;
esac; done

cd "$(dirname "$0")/.."
python3 - "$LISTENER" "$AGENTS" "$TIMEOUT" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import e2e
listener, n, timeout = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])

pids, aids = [], []
for i in range(n):
    p = e2e.gen_agent(listener, "ma%d" % i)
    pid = e2e.launch(p)
    pids.append(pid)
    aids.append(e2e.wait_registered(listener, pid, timeout))
print("all %d agents registered" % n, flush=True)

for i, aid in enumerate(aids):
    e2e.send_raw(aid, "getuid")
    msg = e2e.console_poll(aid, '"a_completed":true', timeout)
    d = __import__("json").loads(msg)
    assert d.get("a_id") == aid, "cross-talk: response for %s on %s" % (d.get("a_id"), aid)
    assert "uid=0(root)" in d.get("a_text", ""), "bad text: %r" % d.get("a_text")
    print("[%d/%d] %s roundtrip OK" % (i + 1, n, aid), flush=True)

for pid in pids:
    e2e.kill(pid)
print("PASS: %d agents, no cross-talk" % n)
EOF
