#!/usr/bin/env bash
# crypto_edge_test.sh — Phase 4 gap 4 (agent side): encryption edge cases.
#
# Rounds: empty-template responses (every beacon), tiny payloads (getuid),
# and a large command output (multi-block GCM/ChaCha20 + big b64). Both the
# 16-byte key path (legacy GCM listeners) and the 32-byte key path (current
# forced-32B transports) must roundtrip. b64 padding variants are exercised
# implicitly: every task/response body b64-encodes arbitrary lengths.
#
# Usage: bash crypto_edge_test.sh [--listener TestHTTP] [--timeout 150]
set -u
LISTENER="TestHTTP"
TIMEOUT=150
while [ $# -gt 0 ]; do case "$1" in
  --listener) LISTENER="$2"; shift 2;;
  --timeout) TIMEOUT="$2"; shift 2;;
  *) echo "unknown arg $1"; exit 2;;
esac; done

cd "$(dirname "$0")/.."
python3 - "$LISTENER" "$TIMEOUT" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import e2e
listener, timeout = sys.argv[1], int(sys.argv[2])
binp = e2e.gen_agent(listener, "edge")
pid = e2e.launch(binp)
try:
    aid = e2e.wait_registered(listener, pid, timeout)
    # 1) small payload: 15-byte plaintext (single block)
    e2e.send_raw(aid, "getuid")
    e2e.console_poll(aid, '"a_completed":true', timeout)
    print("small payload roundtrip OK", flush=True)
    # 2) large output: several KB of console text (multi-block + long b64)
    e2e.send_raw(aid, "ls /usr/lib/x86_64-linux-gnu")
    msg = e2e.console_poll(aid, "Listing", timeout)
    txt = __import__("json").loads(msg).get("a_text", "")
    assert len(txt) > 1000, "expected >1KB output, got %d" % len(txt)
    print("large payload roundtrip OK (%d bytes)" % len(txt), flush=True)
    # 3) a second roundtrip on the SAME session (state/no-reuse check)
    e2e.send_raw(aid, "pwd")
    e2e.console_poll(aid, '"a_completed":true', timeout)
    print("third roundtrip OK", flush=True)
    print("PASS: crypto edges")
finally:
    e2e.kill(pid)
EOF
