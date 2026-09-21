#!/usr/bin/env bash
# arm64_smoke_test.sh — Phase 4 gap 9: aarch64 payload smoke via qemu.
#
# Generates an ARM64 agent and runs it under qemu-aarch64-static. Pass
# criteria: the connection loop starts and either connects cleanly (if a
# listener is reachable from qemu) or exits the connect cycle cleanly
# (connection-refused retry path) — i.e. no crash, no illegal instruction.
#
# Usage: bash arm64_smoke_test.sh [--listener TestHTTP] [--run-sec 15]
set -u
LISTENER="TestHTTP"
RUN_SEC=15
while [ $# -gt 0 ]; do case "$1" in
  --listener) LISTENER="$2"; shift 2;;
  --run-sec) RUN_SEC="$2"; shift 2;;
  *) echo "unknown arg $1"; exit 2;;
esac; done

cd "$(dirname "$0")/.."
command -v qemu-aarch64-static >/dev/null 2>&1 || {
  echo "SKIP: qemu-aarch64-static missing (apt install qemu-user-static)"
  exit 0
}

BIN=$(python3 - "$LISTENER" <<'EOF'
import sys
sys.path.insert(0, 'tools')
import json, base64, e2e
cfg = dict(e2e.AGENT_CFG); cfg["arch"] = "arm64"
r = e2e._auth_http("POST", "/agent/generate",
                  {"listener_name": [sys.argv[1]], "agent": "linux",
                   "config": json.dumps(cfg)})
if not r.get("ok"):
    print("GEN FAIL:", r); raise SystemExit(1)
fn, content = r["message"].split(":")
path = "/tmp/arm64_agent"
open(path, "wb").write(base64.b64decode(content))
print(path)
EOF
) || { echo "FAIL: arm64 generate failed"; exit 1; }
chmod +x "$BIN"

echo "running $BIN under qemu for ${RUN_SEC}s..."
timeout "$RUN_SEC" qemu-aarch64-static "$BIN" >/tmp/arm64_smoke.log 2>&1
RC=$?
# 124 = still running at timeout (connection loop alive); 0 = clean exit;
# anything else = crash / illegal instruction
if [ "$RC" -eq 124 ] || [ "$RC" -eq 0 ]; then
  echo "PASS: arm64 payload ran without crashing (rc=$RC)"
  head -3 /tmp/arm64_smoke.log
else
  echo "FAIL: arm64 payload exited rc=$RC"
  head -10 /tmp/arm64_smoke.log
  exit 1
fi
